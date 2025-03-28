#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <sstream>
#include <cassert>
#include <iterator>
#include <cstring>
#include <vector>
#include "crypto.h"
#include "log.h"

namespace util
{

    bool base64_encode_openssl(std::vector<char> & in, std::string & out)
    {
        BIO * b64 = BIO_new(BIO_f_base64());
        assert(b64);
        BIO * bio = BIO_new(BIO_s_mem());
        assert(bio);
        bio = BIO_push(b64, bio);
        assert(bio);

        BIO_write(bio, in.data(), in.size());
        BIO_flush(bio);

        char buf[2048];
        std::vector<char> bufv;
        bufv.reserve(4 / 3 * in.size() + 2);

        int len = BIO_ctrl_pending(bio);
        if (len > -1)
        {
            BUF_MEM *ptr;
            BIO_get_mem_ptr(bio, &ptr);
            out = std::string(reinterpret_cast<char *>(ptr->data), len);
        }
        else
        {
            out = std::string();
        }
        BIO_free_all(bio);

        return true;
    }

    bool base64_decode_openssl(const std::string & in, std::vector<char> & out)
    {
        // calc padding
        size_t padding = 0;
        if (in.size() > 1 && in[in.size()-1] == '=' && in[in.size()-2] == '=')
        {
            padding = 2;
        }
        else if (in.size() > 0 && in[in.size()-1] == '=')
        {
            padding = 1;
        }

        size_t decode_len = (in.size()*3)/4 - padding;

        out.clear();
        out.resize(decode_len);
    
        BIO * bio = BIO_new_mem_buf(in.c_str(), in.size());
        assert(bio);
        BIO * b64 = BIO_new(BIO_f_base64());
        assert(b64);
        bio = BIO_push(b64, bio);
        assert(bio);
    
        size_t len = BIO_read(bio, out.data(), in.size());
        out.resize(len);
        BIO_free_all(bio);

        return true;
    }

    bool encrypt_openssl_aes128(const std::string password,
                                std::istream & is,
                                std::vector<char> & cipher)
    {
        std::vector<char> plain_text;

        is >> std::noskipws;

        std::copy(std::istream_iterator<char>(is),
                  std::istream_iterator<char>(),
                  std::back_inserter(plain_text));              

        // generate salt
        std::vector<char> salt(8);
        
        LOG_DEBUG("generating salt");

        int status =
            RAND_priv_bytes(reinterpret_cast<unsigned char *>(salt.data()),
                            salt.size());
        if (status != 1)
        {
            LOG_ERROR("failed to generate salt");
            return false;
        }
        
        std::vector<char> key(32);
        std::vector<char> iv(32);

        LOG_DEBUG("generating key");

        status =
            EVP_BytesToKey(EVP_aes_128_cbc(), EVP_sha256(),
                           reinterpret_cast<unsigned char *>(salt.data()),
                           reinterpret_cast<const unsigned char *>(password.c_str()),
                           password.length(), 1,
                           reinterpret_cast<unsigned char *>(key.data()),
                           reinterpret_cast<unsigned char *>(iv.data()));
        if (!status)
        {
            LOG_ERROR("failed to generate key");
            return false;
        }
        
        EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
        assert(ctx);

        LOG_DEBUG("encrypt init");

        status =
            EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                               reinterpret_cast<unsigned char *>(key.data()),
                               reinterpret_cast<unsigned char *>(iv.data()));
        if (status != 1)
        {
            LOG_WARN("failed to initialize encryption");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        EVP_CIPHER_CTX_set_key_length(ctx, EVP_MAX_KEY_LENGTH);

        std::vector<char> cipher_text(plain_text.size() + key.size());

        int total;

        int len;

        LOG_DEBUG("encrypt update");

        status = EVP_EncryptUpdate(ctx,
                                   reinterpret_cast<unsigned char *>(cipher_text.data()),
                                   &len,
                                   reinterpret_cast<unsigned char *>(plain_text.data()),
                                   plain_text.size());
        if (status != 1)
        {
            LOG_WARN("failed to update during encrypt");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        
        total = len;

        LOG_DEBUG("encrypt final");

        status = EVP_EncryptFinal_ex(ctx,
                                     reinterpret_cast<unsigned char *>(cipher_text.data() + len), &len);
        if (status != 1)
        {
            LOG_WARN("failed to finalize during encrypt");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        
        total += len;

        cipher_text.resize(total);

        EVP_CIPHER_CTX_free(ctx);

        cipher.clear();
        cipher.reserve(16+cipher_text.size());

        // add salt
        std::string salted("Salted__");

        for (auto & it : salted)
        {
            cipher.push_back(it);
        }

        for (auto & it : salt)
        {
            cipher.push_back(it);
        }

        // add cipher text
        std::copy(cipher_text.begin(), cipher_text.end(),
                  std::back_inserter(cipher));

        return true;
    }

    bool decrypt_openssl_aes128(const std::string password,
                                std::vector<char> & decoded,
                                std::ostream & os)
    {
        if (decoded.size() < 16)
        {
            LOG_WARN("no salt");
            return false;
        }
        std::string salt_hdr(decoded.data(), 8);
        if (salt_hdr != "Salted__")
        {
            LOG_WARN("no salt during decrypt");
            return false;
        }
        std::vector<char> salt;
        std::copy(decoded.begin() + 8, decoded.begin() + 16,
                  std::back_inserter(salt));

    
        std::vector<char> key(32);
        std::vector<char> iv(32);

        LOG_DEBUG("generating key");
        int status =
            EVP_BytesToKey(EVP_aes_128_cbc(), EVP_sha256(),
                           reinterpret_cast<unsigned char *>(salt.data()),
                           reinterpret_cast<const unsigned char *>(password.c_str()),
                           password.length(), 1,
                           reinterpret_cast<unsigned char *>(key.data()),
                           reinterpret_cast<unsigned char *>(iv.data()));
        if (!status)
        {
            LOG_ERROR("failed to generate key");
            return false;
        }

        std::vector<char> cipher_text;

        cipher_text.reserve(decoded.size() - 16);

        std::copy(decoded.begin() + 16, decoded.end(),
                  std::back_inserter(cipher_text));

        std::vector<char> plain_text(cipher_text.size());

        EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
        {
            LOG_WARN("failed to create cipher context for decrypt");
            return false;
        }

        LOG_DEBUG("decrypt init");
        status =
            EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                               reinterpret_cast<unsigned char *>(key.data()),
                               reinterpret_cast<unsigned char *>(iv.data()));
        if (status != 1)
        {
            LOG_WARN("failed to init cipher context for decrypt");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        EVP_CIPHER_CTX_set_key_length(ctx, EVP_MAX_KEY_LENGTH);

        int total;

        LOG_DEBUG("decrypt update");
        int len;
        status =
            EVP_DecryptUpdate(ctx,
                              reinterpret_cast<unsigned char *>(plain_text.data()),
                              &len,
                              reinterpret_cast<unsigned char *>(cipher_text.data()),
                              cipher_text.size());
        if (status != 1)
        {
            LOG_WARN("faild to update cipher context for decrypt");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        total = len;

        LOG_DEBUG("decrypt final");
        status =
            EVP_DecryptFinal_ex(ctx,
                                reinterpret_cast<unsigned char *>(plain_text.data() + len),
                                &len);
        if (status != 1)
        {
            LOG_WARN("faild to finalize cipher context for decrypt");
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
    
        total += len;

        plain_text.resize(total);

        for (auto & it : plain_text)
        {
            os << it;
        }

        EVP_CIPHER_CTX_free(ctx);

        return true;
    }
}
