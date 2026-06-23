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

    // Maps an OpenSSL return code (from SSL_read/SSL_write/SSL_accept/
    // SSL_shutdown) onto our CONNECTION_STATUS.  `rc` is the original return
    // value from the SSL_* call so SSL_get_error() can be invoked correctly.
    static connection::CONNECTION_STATUS map_ssl_error(SSL * ssl, int rc)
    {
        int ssl_status = SSL_get_error(ssl, rc);
        switch (ssl_status)
        {
        case SSL_ERROR_WANT_READ:
            return connection::CONNECTION_WANTS_READ;
        case SSL_ERROR_WANT_WRITE:
            return connection::CONNECTION_WANTS_WRITE;
        case SSL_ERROR_ZERO_RETURN:
            // Peer sent a clean close_notify
            return connection::CONNECTION_CLOSED;
        case SSL_ERROR_SYSCALL:
        {
            int saved_errno = errno;
            // rc == 0 means EOF observed in violation of protocol;
            // rc == -1 means a real syscall error.  Either way, an
            // errno of 0 indicates the peer simply went away.
            if (rc == 0 || saved_errno == 0)
            {
                return connection::CONNECTION_CLOSED;
            }
            if (saved_errno == ECONNRESET || saved_errno == EPIPE ||
                saved_errno == ENOTCONN)
            {
                return connection::CONNECTION_CLOSED;
            }
            log_ssl_errors();
            LOG_DEBUG_ERRNO("ssl syscall error", saved_errno);
            return connection::CONNECTION_ERROR;
        }
        case SSL_ERROR_SSL:
        default:
            log_ssl_errors();
            return connection::CONNECTION_ERROR;
        }
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
        // SSL_shutdown returns:
        //   1  -> bidirectional shutdown complete
        //   0  -> close_notify sent; peer's close_notify not yet received.
        //         Caller should call us again (typically after waiting for
        //         readability) to drive the second half.
        //   <0 -> consult SSL_get_error()
        int status = SSL_shutdown(m_ssl);
        if (status == 1)
        {
            return CONNECTION_STATUS::CONNECTION_OK;
        }
        if (status == 0)
        {
            // Need a second call after the peer responds.  Asking the caller
            // to wait for readability matches OpenSSL's recommendation.
            return CONNECTION_STATUS::CONNECTION_WANTS_READ;
        }
        return map_ssl_error(m_ssl, status);
    }

    void ssl_connection::init(const char * cert_file,
                              const char * key_file)
    {
        if (m_ssl_ctx)
        {
            // Idempotent guard: refuse to leak the prior context.
            throw std::runtime_error("ssl_connection::init called more than once");
        }

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
            return map_ssl_error(m_ssl, status);
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
        // Log the *negotiated* cipher (the previous code logged the first
        // configured cipher, which is not what was actually selected).
        const SSL_CIPHER * cipher = SSL_get_current_cipher(m_ssl);
        const char * cipher_name = cipher ? SSL_CIPHER_get_name(cipher) : "(none)";
        LOG_INFO("accept with cipher: " << cipher_name << " " << tlsv);
        return CONNECTION_STATUS::CONNECTION_OK;
    }

    connection::CONNECTION_STATUS ssl_connection::read(char* buf,
                                                       size_t length,
                                                       ssize_t & read)
    {
        if (length == 0)
        {
            read = 0;
            return CONNECTION_STATUS::CONNECTION_OK;
        }

        int status = SSL_read(m_ssl, buf, length);
        if (status < 1)
        {
            read = 0;
            return map_ssl_error(m_ssl, status);
        }

        read = status;
        last_read = std::chrono::steady_clock::now();
        return CONNECTION_STATUS::CONNECTION_OK;
    }

    connection::CONNECTION_STATUS ssl_connection::write(const char* buf,
                                                        size_t length,
                                                        ssize_t & written)
    {
        if (length == 0)
        {
            written = 0;
            return CONNECTION_STATUS::CONNECTION_OK;
        }

        int status = SSL_write(m_ssl, buf, length);
        if (status < 1)
        {
            written = 0;
            return map_ssl_error(m_ssl, status);
        }

        written = status;
        last_write = std::chrono::steady_clock::now();
        return CONNECTION_STATUS::CONNECTION_OK;
    }

    bool ssl_connection::pending() const
    {
        // SSL_pending reports plaintext already decrypted and buffered inside
        // OpenSSL.  Such data will not make the socket fd readable, so callers
        // must read it instead of waiting on poll().
        return m_ssl && SSL_pending(m_ssl) > 0;
    }
}

