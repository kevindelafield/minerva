#pragma once

#include <string>
#include <iostream>
#include <vector>

namespace minerva
{
    namespace crypto
    {

        bool base64_encode_openssl(std::vector<char> & in, std::string & out);

        bool base64_decode_openssl(const std::string & in, std::vector<char> & out);

        bool encrypt_openssl_aes128(const std::string password,
                                    std::istream & is,
                                    std::vector<char> & cipher_text);

        bool decrypt_openssl_aes128(const std::string password,
                                    std::vector<char> & cipher_text,
                                    std::ostream & os);

    }
}
