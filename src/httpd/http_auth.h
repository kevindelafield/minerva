#pragma once

#include <string>
#include "http_context.h"
#include "http_request.h"

namespace minerva
{
    class http_auth_user
    {
    public:
        http_auth_user() = default;

        http_auth_user(const std::string & username,
                       const std::string & realm,
                       const std::string & hash) :
            m_user(username), m_realm(realm), m_hash(hash)
        {
        }

        ~http_auth_user() = default;
        http_auth_user(const http_auth_user & user) = default;

        std::string user() const
        {
            return m_user;
        }

        std::string realm() const
        {
            return m_realm;
        }

        std::string hash() const
        {
            return m_hash;
        }

    private:
        std::string m_user;
        std::string m_realm;
        std::string m_hash;
    };

    class http_auth_db {
    public:

        http_auth_db(const std::string & realm) : m_realm(realm)
        {
        }
        ~http_auth_db() = default;

        const std::string & realm() const
        {
            return m_realm;
        }

        virtual bool initialize() {
            return true;
        };

        virtual bool find_user(const std::string & username,
                               http_auth_user & user) = 0;

    private:
        std::string m_realm;
    };
    
    constexpr const char * AUTH_HDR = "Authorization";
    constexpr const char * BASIC_HDR = "Basic";
    constexpr const char * DIGEST_HDR = "Digest";
    constexpr const char * AUTHENTICATE_HDR = "WWW-Authenticate";
    constexpr const char * AWS4_HDR = "AWS4-HMAC-SHA256";

    bool authenticate_basic(http_context & ctx,
                            const std::string & authHdr,
                            const std::string & realm,
                            http_auth_db & db,
                            std::string & user);
    
    bool authenticate_digest(http_context & ctx,
                             const std::string & authHdr,
                             const std::string & realm,
                             http_auth_db & db,
                             std::string & user);

    std::string digest_hash_md5(const std::string & username,
                                const std::string & realm,
                                const std::string & password);
}

