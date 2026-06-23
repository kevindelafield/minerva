#pragma once

#include <string>
#include <istream>
#include <ostream>
#include <vector>

namespace minerva
{
    bool base64_encode_openssl(const std::vector<char> & in, std::string & out);

    bool base64_decode_openssl(const std::string & in, std::vector<char> & out);

    // Encrypts the contents of `is` with AES-128-CBC + HMAC-SHA256
    // (encrypt-then-MAC).  Key, IV and MAC key are derived from `password`
    // and a random per-call salt via PBKDF2-HMAC-SHA256.  Output layout is:
    //   "Salted__"(8) || salt(8) || ciphertext || HMAC-SHA256 tag(32)
    bool encrypt_openssl_aes128(const std::string & password,
                                std::istream & is,
                                std::vector<char> & cipher_text);

    // Decrypts a buffer produced by encrypt_openssl_aes128.  Verifies the
    // HMAC tag before decrypting; returns false if the password is wrong or
    // the ciphertext has been tampered with.
    bool decrypt_openssl_aes128(const std::string & password,
                                const std::vector<char> & cipher_text,
                                std::ostream & os);

    // AES-256-CBC variant of encrypt_openssl_aes128.  Same on-disk layout
    // and HMAC-SHA256 authentication; uses a 32-byte AES key.  Note that
    // the AES-128 and AES-256 outputs are NOT interchangeable -- you must
    // decrypt with the same variant used to encrypt.
    bool encrypt_openssl_aes256(const std::string & password,
                                std::istream & is,
                                std::vector<char> & cipher_text);

    // Decrypts a buffer produced by encrypt_openssl_aes256.
    bool decrypt_openssl_aes256(const std::string & password,
                                const std::vector<char> & cipher_text,
                                std::ostream & os);
}
