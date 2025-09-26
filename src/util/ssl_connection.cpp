#include <cassert>
#include <errno.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "ssl_connection.h"

namespace minerva
{
    static void log_ssl_errors()
    {
        unsigned long ssl_err = ERR_get_error();
        while (ssl_err != 0)
        {
            char buf[2048];
            ERR_error_string_n(ssl_err, buf, sizeof(buf));
            LOG_ERROR("ssl error: " << buf);
            ssl_err = ERR_get_error();
        }
        ERR_clear_error(); // Ensure complete error queue cleanup
    }

    SSL_CTX * ssl_connection::m_ssl_ctx = nullptr;

    ssl_connection::ssl_connection(int socket) : connection(socket)
    {
        if (!m_ssl_ctx)
        {
            throw std::runtime_error("SSL context not initialized - call ssl_connection::init() first");
        }
        
        m_bio = BIO_new_socket(socket, BIO_NOCLOSE);
        if (!m_bio)
        {
            log_ssl_errors();
            throw std::runtime_error("Failed to create BIO for SSL connection");
        }
        
        m_ssl = SSL_new(m_ssl_ctx);
        if (!m_ssl)
        {
            BIO_free(m_bio);
            log_ssl_errors();
            throw std::runtime_error("Failed to create SSL object");
        }
        
        SSL_set_bio(m_ssl, m_bio, m_bio);
    }

    ssl_connection::ssl_connection(int family, int socktype, int protocol) :
        connection(family, socktype, protocol)
    {
        if (!m_ssl_ctx)
        {
            throw std::runtime_error("SSL context not initialized - call ssl_connection::init() first");
        }
        
        m_bio = BIO_new_socket(socket, BIO_NOCLOSE);
        if (!m_bio)
        {
            log_ssl_errors();
            throw std::runtime_error("Failed to create BIO for SSL connection");
        }
        
        m_ssl = SSL_new(m_ssl_ctx);
        if (!m_ssl)
        {
            BIO_free(m_bio);
            log_ssl_errors();
            throw std::runtime_error("Failed to create SSL object");
        }
        
        SSL_set_bio(m_ssl, m_bio, m_bio);
    }

    ssl_connection::~ssl_connection()
    {
        if (m_ssl)
        {
            SSL_free(m_ssl);  // This also frees the associated BIO
            m_ssl = nullptr;
            m_bio = nullptr;  // Just for clarity
        }
    }

    ssl_connection::ssl_connection(ssl_connection&& other) noexcept
        : connection(std::move(other)),  // Move base class
          m_ssl(other.m_ssl),           // Transfer SSL ownership
          m_bio(other.m_bio)            // Transfer BIO ownership
    {
        // Invalidate source object
        other.m_ssl = nullptr;
        other.m_bio = nullptr;
    }

    ssl_connection& ssl_connection::operator=(ssl_connection&& other) noexcept
    {
        if (this != &other)
        {
            // Clean up existing resources first
            if (m_ssl)
            {
                SSL_free(m_ssl);  // This also frees m_bio
                m_ssl = nullptr;
                m_bio = nullptr;
            }

            // Move base class
            connection::operator=(std::move(other));

            // Transfer SSL resources
            m_ssl = other.m_ssl;
            m_bio = other.m_bio;

            // Invalidate source
            other.m_ssl = nullptr;
            other.m_bio = nullptr;
        }
        return *this;
    }

    ssl_connection::CONNECTION_STATUS ssl_connection::shutdown()
    {
        int status = SSL_shutdown(m_ssl);
        if (status < 1)
        {
            int ssl_status = SSL_get_error(m_ssl, status);

            switch (ssl_status)
            {
            case SSL_ERROR_WANT_READ:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_READ;
            }
            break;
            case SSL_ERROR_WANT_WRITE:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_WRITE;
            }
            break;
            case SSL_ERROR_SYSCALL:
            {
                log_ssl_errors();

                if (status == 0)
                {
                    return CONNECTION_STATUS::CONNECTION_CLOSED;
                }
                else if (status == -1)
                {
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
                else
                {
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
            }
            break;
            default:
            {
                log_ssl_errors();
                return CONNECTION_STATUS::CONNECTION_ERROR;
            }
            break;
            }
        }

        return CONNECTION_STATUS::CONNECTION_OK;
    }

    void ssl_connection::init(const char * cert_file,
                              const char * key_file)
    {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        m_ssl_ctx = SSL_CTX_new(TLS_server_method());
        if (!m_ssl_ctx)
        {
            log_ssl_errors();
            throw std::runtime_error("Failed to create SSL context");
        }

        SSL_CTX_set_min_proto_version(m_ssl_ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(m_ssl_ctx, TLS1_3_VERSION);

        // Security hardening options
        SSL_CTX_set_options(m_ssl_ctx, 
                          SSL_OP_NO_SSLv3 |                    // Disable SSL 3.0 (vulnerable)
                          SSL_OP_NO_TLSv1 |                    // Disable TLS 1.0 (vulnerable)  
                          SSL_OP_NO_TLSv1_1 |                  // Disable TLS 1.1 (vulnerable)
                          SSL_OP_NO_COMPRESSION |               // Prevent CRIME attacks
                          SSL_OP_CIPHER_SERVER_PREFERENCE |     // Server chooses cipher
                          SSL_OP_SINGLE_DH_USE |                // Forward secrecy (ephemeral DH)
                          SSL_OP_SINGLE_ECDH_USE);              // Forward secrecy (ephemeral ECDH)

        // Set security level (level 2 = 112-bit minimum security, RSA 2048+)
        SSL_CTX_set_security_level(m_ssl_ctx, 2);

//        SSL_CTX_set_ecdh_auto(m_ssl_ctx, 1);

        int status =
            SSL_CTX_use_certificate_file(m_ssl_ctx, cert_file, 
                                         SSL_FILETYPE_PEM);
        if (status != 1)
        {
            log_ssl_errors();
            SSL_CTX_free(m_ssl_ctx);
            m_ssl_ctx = nullptr;
            throw std::runtime_error("Failed to load certificate file");
        }
        
        status =
            SSL_CTX_use_PrivateKey_file(m_ssl_ctx, key_file, SSL_FILETYPE_PEM);
        if (status != 1)
        {
            log_ssl_errors();
            SSL_CTX_free(m_ssl_ctx);
            m_ssl_ctx = nullptr;
            throw std::runtime_error("Failed to load private key file");
        }
        
        // Verify private key matches certificate
        if (SSL_CTX_check_private_key(m_ssl_ctx) != 1)
        {
            log_ssl_errors();
            SSL_CTX_free(m_ssl_ctx);
            m_ssl_ctx = nullptr;
            throw std::runtime_error("Private key does not match certificate");
        }
    }

    void ssl_connection::destroy()
    {
        if (m_ssl_ctx)
        {
            SSL_CTX_free(m_ssl_ctx);
            m_ssl_ctx = nullptr;
        }
    }

    connection::CONNECTION_STATUS ssl_connection::accept_ssl()
    {
        int status = SSL_accept(m_ssl);
        if (status < 1)
        {
            int ssl_status = SSL_get_error(m_ssl, status);

            switch (ssl_status)
            {
            case SSL_ERROR_WANT_READ:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_READ;
            }
            break;
            case SSL_ERROR_WANT_WRITE:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_WRITE;
            }
            break;
            case SSL_ERROR_SYSCALL:
            {
                log_ssl_errors();

                if (status == 0)
                {
                    return CONNECTION_STATUS::CONNECTION_CLOSED;
                }
                else if (status == -1)
                {
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
                else
                {
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
            }
            break;
            default:
            {
                log_ssl_errors();
                return CONNECTION_STATUS::CONNECTION_ERROR;
            }
            break;
            }
        }

        int version = SSL_version(m_ssl);
        std::string tlsv;
        switch (version)
        {
        case TLS1_VERSION:
            tlsv = "TLS1.0";
            break;
        case TLS1_1_VERSION:
            tlsv = "TLS1.1";
            break;
        case TLS1_2_VERSION:
            tlsv = "TLS1.2";
            break;
        case TLS1_3_VERSION:
            tlsv = "TLS1.3";
            break;
        default:
            tlsv = "TLS version unknown";
            break;
        }
        LOG_INFO("accept with cipher: " << SSL_get_cipher_list(m_ssl, 0) <<
                 " " << tlsv);
        return CONNECTION_STATUS::CONNECTION_OK;
    }

    connection::CONNECTION_STATUS ssl_connection::read(char* buf,
                                                       size_t length,
                                                       ssize_t & read)
    {
        int status = SSL_read(m_ssl, buf, length);
        if (status < 1)
        {
            int ssl_status = SSL_get_error(m_ssl, status);

            switch (ssl_status)
            {
            case SSL_ERROR_WANT_READ:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_READ;
            }
            break;
            case SSL_ERROR_WANT_WRITE:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_WRITE;
            }
            break;
            case SSL_ERROR_SYSCALL:
            {
                log_ssl_errors();

                if (status == 0)
                {
                    return CONNECTION_STATUS::CONNECTION_CLOSED;
                }
                else if (status == -1)
                {
                    LOG_ERROR_ERRNO("ssl syscall error: ", errno);
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
                else
                {
                    LOG_ERROR("ssl error");
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
            }
            break;
            default:
            {
                log_ssl_errors();
                LOG_ERROR("ssl unknown error");
                return CONNECTION_STATUS::CONNECTION_ERROR;
            }
            break;
            }
        }

        read = status;
        last_read = std::chrono::steady_clock::now();
        return CONNECTION_STATUS::CONNECTION_OK;
    }

    connection::CONNECTION_STATUS ssl_connection::write(const char* buf,
                                                        size_t length,
                                                        ssize_t & written)
    {
        int status = SSL_write(m_ssl, buf, length);
        if (status < 1)
        {
            int ssl_status = SSL_get_error(m_ssl, status);

            switch (ssl_status)
            {
            case SSL_ERROR_WANT_READ:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_READ;
            }
            break;
            case SSL_ERROR_WANT_WRITE:
            {
                return CONNECTION_STATUS::CONNECTION_WANTS_WRITE;
            }
            break;
            case SSL_ERROR_SYSCALL:
            {
                log_ssl_errors();

                if (status == 0)
                {
                    return CONNECTION_STATUS::CONNECTION_CLOSED;
                }
                else
                {
                    return CONNECTION_STATUS::CONNECTION_ERROR;
                }
            }
            break;
            default:
            {
                log_ssl_errors();
                return CONNECTION_STATUS::CONNECTION_ERROR;
            }
            break;
            }
        }

        written = status;
        last_write = std::chrono::steady_clock::now();
        return CONNECTION_STATUS::CONNECTION_OK;
    }
}

