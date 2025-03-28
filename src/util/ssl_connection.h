#pragma once

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "connection.h"

namespace minerva
{
    class ssl_connection : public connection
    {
    public:
        static void init(const char * cert_file,
                         const char * key_file);

        static void destroy();

        ssl_connection(int socket);
        ssl_connection(int family, int socktype, int protocol);
        virtual ~ssl_connection();

        CONNECTION_STATUS accept_ssl() override;

        CONNECTION_STATUS shutdown() override;

        CONNECTION_STATUS read(char* buf, size_t length, ssize_t & read) override;

        CONNECTION_STATUS write(const char* buf, size_t length, ssize_t & written) override;

    private:
        static SSL_CTX *m_ssl_ctx;
        SSL *m_ssl;
        BIO *m_bio;
    };
}
