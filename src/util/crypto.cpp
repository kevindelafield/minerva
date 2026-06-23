#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include <openssl/kdf.h>
#include <sstream>
#include <cassert>
#include <iterator>
#include <cstring>
#include <vector>
#include "crypto.h"
#include "log.h"

namespace minerva
{
    namespace
    {
        // Common parameters for the PBKDF2-based encrypt/decrypt pair.
        constexpr int    PBKDF2_ITERATIONS = 100000;
        constexpr size_t SALT_LEN          = 8;
        constexpr size_t AES128_KEY_LEN    = 16;
        constexpr size_t AES256_KEY_LEN    = 32;
        constexpr size_t AES_IV_LEN        = 16;
        constexpr size_t HMAC_KEY_LEN      = 32;
        constexpr size_t HMAC_TAG_LEN      = 32;
        constexpr size_t MAX_AES_KEY_LEN   = AES256_KEY_LEN;
        constexpr char   SALT_HEADER[]     = "Salted__"; // 8 bytes (no NUL)
        constexpr size_t SALT_HEADER_LEN   = 8;

        // Derive AES key, IV and HMAC key from a single PBKDF2 invocation so
        // we never have two independent KDF calls (this matches the convention
        // used by `openssl enc -pbkdf2 -md sha256`).
        bool derive_keys(const std::string & password,
                         const unsigned char * salt, size_t salt_len,
                         unsigned char * aes_key, size_t aes_key_len,
                         unsigned char * iv,      size_t iv_len,
                         unsigned char * hmac_key, size_t hmac_key_len)
        {
            std::vector<unsigned char> material(aes_key_len + iv_len + hmac_key_len);
            int status = PKCS5_PBKDF2_HMAC(password.c_str(),
                                           static_cast<int>(password.length()),
                                           salt, static_cast<int>(salt_len),
                                           PBKDF2_ITERATIONS, EVP_sha256(),
                                           static_cast<int>(material.size()),
                                           material.data());
            if (status != 1)
            {
                OPENSSL_cleanse(material.data(), material.size());
                LOG_ERROR("PBKDF2 key derivation failed");
                return false;
            }

            std::memcpy(aes_key,  material.data(),                        aes_key_len);
            std::memcpy(iv,       material.data() + aes_key_len,          iv_len);
            std::memcpy(hmac_key, material.data() + aes_key_len + iv_len, hmac_key_len);

            OPENSSL_cleanse(material.data(), material.size());
            return true;
        }
    }

    bool base64_encode_openssl(const std::vector<char> & in, std::string & out)
    {
        out.clear();

        BIO * b64 = BIO_new(BIO_f_base64());
        if (!b64)
        {
            LOG_ERROR("BIO_new(BIO_f_base64) failed");
            return false;
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // no embedded newlines

        BIO * mem = BIO_new(BIO_s_mem());
        if (!mem)
        {
            BIO_free(b64);
            LOG_ERROR("BIO_new(BIO_s_mem) failed");
            return false;
        }
        BIO * bio = BIO_push(b64, mem);

        if (!in.empty())
        {
            int written = BIO_write(bio, in.data(), static_cast<int>(in.size()));
            if (written <= 0 || static_cast<size_t>(written) != in.size())
            {
                LOG_ERROR("BIO_write failed during base64 encode");
                BIO_free_all(bio);
                return false;
            }
        }
        if (BIO_flush(bio) != 1)
        {
            LOG_ERROR("BIO_flush failed during base64 encode");
            BIO_free_all(bio);
            return false;
        }

        BUF_MEM * ptr = nullptr;
        BIO_get_mem_ptr(bio, &ptr);
        if (ptr && ptr->length > 0)
        {
            out.assign(ptr->data, ptr->length);
        }

        BIO_free_all(bio);
        return true;
    }

    bool base64_decode_openssl(const std::string & in, std::vector<char> & out)
    {
        out.clear();

        if (in.empty())
        {
            return true;
        }

        // Upper bound: every 4 input chars decode to 3 output bytes.
        size_t max_out = (in.size() / 4 + 1) * 3;
        out.resize(max_out);

        BIO * mem = BIO_new_mem_buf(in.data(), static_cast<int>(in.size()));
        if (!mem)
        {
            LOG_ERROR("BIO_new_mem_buf failed");
            out.clear();
            return false;
        }

        BIO * b64 = BIO_new(BIO_f_base64());
        if (!b64)
        {
            BIO_free(mem);
            LOG_ERROR("BIO_new(BIO_f_base64) failed");
            out.clear();
            return false;
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

        BIO * bio = BIO_push(b64, mem);

        int len = BIO_read(bio, out.data(), static_cast<int>(out.size()));
        BIO_free_all(bio);

        if (len < 0)
        {
            LOG_ERROR("BIO_read failed during base64 decode");
            out.clear();
            return false;
        }

        out.resize(static_cast<size_t>(len));
        return true;
    }

    namespace
    {
        // Shared encrypt implementation parametrized by AES variant.
        bool encrypt_impl(const EVP_CIPHER * cipher_alg,
                          size_t aes_key_len,
                          const std::string & password,
                          std::istream & is,
                          std::vector<char> & cipher)
        {
            cipher.clear();

            std::vector<char> plain_text;
            is >> std::noskipws;
            std::copy(std::istream_iterator<char>(is),
                      std::istream_iterator<char>(),
                      std::back_inserter(plain_text));

            unsigned char salt[SALT_LEN];
            if (RAND_bytes(salt, SALT_LEN) != 1)
            {
                LOG_ERROR("failed to generate salt");
                OPENSSL_cleanse(plain_text.data(), plain_text.size());
                return false;
            }

            unsigned char aes_key[MAX_AES_KEY_LEN];
            unsigned char iv[AES_IV_LEN];
            unsigned char hmac_key[HMAC_KEY_LEN];
            if (!derive_keys(password, salt, SALT_LEN,
                             aes_key,  aes_key_len,
                             iv,       sizeof(iv),
                             hmac_key, sizeof(hmac_key)))
            {
                OPENSSL_cleanse(plain_text.data(), plain_text.size());
                return false;
            }

            EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
            {
                LOG_ERROR("EVP_CIPHER_CTX_new failed");
                OPENSSL_cleanse(aes_key,  sizeof(aes_key));
                OPENSSL_cleanse(iv,       sizeof(iv));
                OPENSSL_cleanse(hmac_key, sizeof(hmac_key));
                OPENSSL_cleanse(plain_text.data(), plain_text.size());
                return false;
            }

            bool ok = false;
            std::vector<char> cipher_text(plain_text.size() + AES_IV_LEN);
            int total = 0;
            int len = 0;

            do
            {
                if (EVP_EncryptInit_ex(ctx, cipher_alg, nullptr,
                                       aes_key, iv) != 1)
                {
                    LOG_WARN("failed to initialize encryption");
                    break;
                }

                if (EVP_EncryptUpdate(ctx,
                                      reinterpret_cast<unsigned char *>(cipher_text.data()),
                                      &len,
                                      reinterpret_cast<const unsigned char *>(plain_text.data()),
                                      static_cast<int>(plain_text.size())) != 1)
                {
                    LOG_WARN("failed to update during encrypt");
                    break;
                }
                total = len;

                if (EVP_EncryptFinal_ex(ctx,
                                        reinterpret_cast<unsigned char *>(cipher_text.data() + total),
                                        &len) != 1)
                {
                    LOG_WARN("failed to finalize during encrypt");
                    break;
                }
                total += len;
                cipher_text.resize(static_cast<size_t>(total));

                // HMAC-SHA256 over: SALT_HEADER || salt || ciphertext
                std::vector<unsigned char> mac_input;
                mac_input.reserve(SALT_HEADER_LEN + SALT_LEN + cipher_text.size());
                mac_input.insert(mac_input.end(), SALT_HEADER, SALT_HEADER + SALT_HEADER_LEN);
                mac_input.insert(mac_input.end(), salt, salt + SALT_LEN);
                mac_input.insert(mac_input.end(),
                                 reinterpret_cast<const unsigned char *>(cipher_text.data()),
                                 reinterpret_cast<const unsigned char *>(cipher_text.data()) + cipher_text.size());

                unsigned char tag[HMAC_TAG_LEN];
                unsigned int  tag_len = 0;
                if (HMAC(EVP_sha256(), hmac_key, sizeof(hmac_key),
                         mac_input.data(), mac_input.size(),
                         tag, &tag_len) == nullptr || tag_len != HMAC_TAG_LEN)
                {
                    LOG_ERROR("HMAC computation failed");
                    OPENSSL_cleanse(mac_input.data(), mac_input.size());
                    break;
                }
                OPENSSL_cleanse(mac_input.data(), mac_input.size());

                cipher.reserve(SALT_HEADER_LEN + SALT_LEN + cipher_text.size() + HMAC_TAG_LEN);
                cipher.insert(cipher.end(), SALT_HEADER, SALT_HEADER + SALT_HEADER_LEN);
                cipher.insert(cipher.end(),
                              reinterpret_cast<const char *>(salt),
                              reinterpret_cast<const char *>(salt) + SALT_LEN);
                cipher.insert(cipher.end(), cipher_text.begin(), cipher_text.end());
                cipher.insert(cipher.end(),
                              reinterpret_cast<const char *>(tag),
                              reinterpret_cast<const char *>(tag) + HMAC_TAG_LEN);

                ok = true;
            } while (0);

            EVP_CIPHER_CTX_free(ctx);
            OPENSSL_cleanse(aes_key,  sizeof(aes_key));
            OPENSSL_cleanse(iv,       sizeof(iv));
            OPENSSL_cleanse(hmac_key, sizeof(hmac_key));
            OPENSSL_cleanse(plain_text.data(), plain_text.size());
            if (!cipher_text.empty())
            {
                OPENSSL_cleanse(cipher_text.data(), cipher_text.size());
            }
            if (!ok)
            {
                cipher.clear();
            }
            return ok;
        }

        // Shared decrypt implementation parametrized by AES variant.
        bool decrypt_impl(const EVP_CIPHER * cipher_alg,
                          size_t aes_key_len,
                          const std::string & password,
                          const std::vector<char> & decoded,
                          std::ostream & os)
        {
            constexpr size_t MIN_LEN =
                SALT_HEADER_LEN + SALT_LEN + AES_IV_LEN + HMAC_TAG_LEN;
            if (decoded.size() < MIN_LEN)
            {
                LOG_WARN("ciphertext too short");
                return false;
            }
            if (std::memcmp(decoded.data(), SALT_HEADER, SALT_HEADER_LEN) != 0)
            {
                LOG_WARN("missing salt header during decrypt");
                return false;
            }

            const unsigned char * salt =
                reinterpret_cast<const unsigned char *>(decoded.data() + SALT_HEADER_LEN);

            const size_t ct_offset = SALT_HEADER_LEN + SALT_LEN;
            const size_t ct_len    = decoded.size() - ct_offset - HMAC_TAG_LEN;
            const unsigned char * ct =
                reinterpret_cast<const unsigned char *>(decoded.data() + ct_offset);
            const unsigned char * tag =
                reinterpret_cast<const unsigned char *>(decoded.data() + ct_offset + ct_len);

            unsigned char aes_key[MAX_AES_KEY_LEN];
            unsigned char iv[AES_IV_LEN];
            unsigned char hmac_key[HMAC_KEY_LEN];
            if (!derive_keys(password, salt, SALT_LEN,
                             aes_key,  aes_key_len,
                             iv,       sizeof(iv),
                             hmac_key, sizeof(hmac_key)))
            {
                return false;
            }

            // Verify HMAC before decryption (encrypt-then-MAC).
            unsigned char expected[HMAC_TAG_LEN];
            unsigned int  expected_len = 0;
            {
                std::vector<unsigned char> mac_input;
                mac_input.reserve(ct_offset + ct_len);
                mac_input.insert(mac_input.end(),
                                 reinterpret_cast<const unsigned char *>(decoded.data()),
                                 reinterpret_cast<const unsigned char *>(decoded.data()) + ct_offset);
                mac_input.insert(mac_input.end(), ct, ct + ct_len);

                const unsigned char * rc =
                    HMAC(EVP_sha256(), hmac_key, sizeof(hmac_key),
                         mac_input.data(), mac_input.size(),
                         expected, &expected_len);
                OPENSSL_cleanse(mac_input.data(), mac_input.size());

                if (rc == nullptr || expected_len != HMAC_TAG_LEN)
                {
                    LOG_ERROR("HMAC computation failed during decrypt");
                    OPENSSL_cleanse(aes_key,  sizeof(aes_key));
                    OPENSSL_cleanse(iv,       sizeof(iv));
                    OPENSSL_cleanse(hmac_key, sizeof(hmac_key));
                    OPENSSL_cleanse(expected, sizeof(expected));
                    return false;
                }
            }

            if (CRYPTO_memcmp(expected, tag, HMAC_TAG_LEN) != 0)
            {
                LOG_WARN("HMAC mismatch (wrong password or tampered ciphertext)");
                OPENSSL_cleanse(aes_key,  sizeof(aes_key));
                OPENSSL_cleanse(iv,       sizeof(iv));
                OPENSSL_cleanse(hmac_key, sizeof(hmac_key));
                OPENSSL_cleanse(expected, sizeof(expected));
                return false;
            }
            OPENSSL_cleanse(expected, sizeof(expected));
            OPENSSL_cleanse(hmac_key, sizeof(hmac_key));

            EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
            {
                LOG_WARN("failed to create cipher context for decrypt");
                OPENSSL_cleanse(aes_key, sizeof(aes_key));
                OPENSSL_cleanse(iv,      sizeof(iv));
                return false;
            }

            bool ok = false;
            std::vector<char> plain_text(ct_len);
            int total = 0;
            int len = 0;

            do
            {
                if (EVP_DecryptInit_ex(ctx, cipher_alg, nullptr,
                                       aes_key, iv) != 1)
                {
                    LOG_WARN("failed to init cipher context for decrypt");
                    break;
                }

                if (EVP_DecryptUpdate(ctx,
                                      reinterpret_cast<unsigned char *>(plain_text.data()),
                                      &len,
                                      ct, static_cast<int>(ct_len)) != 1)
                {
                    LOG_WARN("failed to update cipher context for decrypt");
                    break;
                }
                total = len;

                if (EVP_DecryptFinal_ex(ctx,
                                        reinterpret_cast<unsigned char *>(plain_text.data() + total),
                                        &len) != 1)
                {
                    LOG_WARN("failed to finalize cipher context for decrypt");
                    break;
                }
                total += len;
                plain_text.resize(static_cast<size_t>(total));

                os.write(plain_text.data(), plain_text.size());
                ok = os.good();
                if (!ok)
                {
                    LOG_WARN("ostream write failed during decrypt");
                }
            } while (0);

            EVP_CIPHER_CTX_free(ctx);
            OPENSSL_cleanse(aes_key, sizeof(aes_key));
            OPENSSL_cleanse(iv,      sizeof(iv));
            if (!plain_text.empty())
            {
                OPENSSL_cleanse(plain_text.data(), plain_text.size());
            }
            return ok;
        }
    } // anonymous namespace

    bool encrypt_openssl_aes128(const std::string & password,
                                std::istream & is,
                                std::vector<char> & cipher)
    {
        return encrypt_impl(EVP_aes_128_cbc(), AES128_KEY_LEN,
                            password, is, cipher);
    }

    bool decrypt_openssl_aes128(const std::string & password,
                                const std::vector<char> & decoded,
                                std::ostream & os)
    {
        return decrypt_impl(EVP_aes_128_cbc(), AES128_KEY_LEN,
                            password, decoded, os);
    }

    bool encrypt_openssl_aes256(const std::string & password,
                                std::istream & is,
                                std::vector<char> & cipher)
    {
        return encrypt_impl(EVP_aes_256_cbc(), AES256_KEY_LEN,
                            password, is, cipher);
    }

    bool decrypt_openssl_aes256(const std::string & password,
                                const std::vector<char> & decoded,
                                std::ostream & os)
    {
        return decrypt_impl(EVP_aes_256_cbc(), AES256_KEY_LEN,
                            password, decoded, os);
    }
} // namespace minerva
