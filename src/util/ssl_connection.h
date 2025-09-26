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

        // Explicitly delete copy operations (SSL objects can't be safely copied)
        ssl_connection(const ssl_connection&) = delete;
        ssl_connection& operator=(const ssl_connection&) = delete;

        // Move operations for safe resource transfer
        ssl_connection(ssl_connection&& other) noexcept;
        ssl_connection& operator=(ssl_connection&& other) noexcept;

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
