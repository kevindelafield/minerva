#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <deque>
#include <ctime>
#include "http_context.h"
#include "http_request.h"

namespace minerva
{
    class http_auth_user
    {
    public:
        http_auth_user() = default;

        // Legacy single-hash constructor.  The hash is taken to be the
        // MD5(user:realm:password) credential equivalent.
        http_auth_user(const std::string & username,
                       const std::string & realm,
                       const std::string & hash) :
            m_user(username), m_realm(realm), m_hash_md5(hash)
        {
        }

        // Full constructor carrying both MD5 and SHA-256 credential
        // equivalents.  Either hash may be empty if that algorithm is not
        // supported for this user.
        http_auth_user(const std::string & username,
                       const std::string & realm,
                       const std::string & hash_md5,
                       const std::string & hash_sha256) :
            m_user(username), m_realm(realm),
            m_hash_md5(hash_md5), m_hash_sha256(hash_sha256)
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

        // Backwards-compatible accessor; equivalent to hash_md5().
        std::string hash() const
        {
            return m_hash_md5;
        }

        std::string hash_md5() const
        {
            return m_hash_md5;
        }

        std::string hash_sha256() const
        {
            return m_hash_sha256;
        }

    private:
        std::string m_user;
        std::string m_realm;
        std::string m_hash_md5;
        std::string m_hash_sha256;
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

    // Server-side store that issues opaque digest nonces and validates them
    // against the issuing server, including replay protection via the
    // RFC 7616 nonce-count (nc) value.
    //
    // Nonce wire format:  "<ts_hex>:<hmac_sha256_hex>"
    //   where hmac is HMAC-SHA256(server_secret, ts_hex || ":" || client_ip
    //                                              || ":" || realm).
    //
    // The server secret is regenerated on construction; on process restart
    // all outstanding nonces become invalid (clients re-auth automatically).
    class http_auth_nonce_store
    {
    public:
        http_auth_nonce_store();

        // Issue a fresh nonce bound to (client_ip, realm).
        std::string issue(const std::string & client_ip,
                          const std::string & realm);

        enum class validate_result
        {
            OK,
            INVALID,    // malformed or HMAC mismatch
            STALE,      // valid signature but expired
            REPLAY      // nc not strictly increasing (or nonce reused without nc)
        };

        // Validate a client-supplied nonce. nc_hex may be empty if the client
        // did not send a qop/nc pair.
        validate_result validate(const std::string & nonce,
                                 const std::string & nc_hex,
                                 const std::string & client_ip,
                                 const std::string & realm);

        void max_age_seconds(int seconds) { m_max_age = seconds; }

    private:
        std::string compute_mac_hex(const std::string & ts_hex,
                                    const std::string & client_ip,
                                    const std::string & realm) const;

        void prune_locked(std::time_t now);

        unsigned char m_secret[32];
        int m_max_age = 300;          // 5 minutes
        size_t m_max_entries = 4096;
        std::mutex m_lock;
        // nonce -> (last_nc_seen, expire_ts).  last_nc_seen == 0 means never
        // used with a qop/nc; for nonces without qop, we set last_nc_seen=1
        // on first use to flag the nonce as consumed.
        std::unordered_map<std::string,
                           std::pair<unsigned long, std::time_t>> m_state;
        std::deque<std::string> m_order;
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
                             http_auth_nonce_store & nonce_store,
                             std::string & user);

    std::string digest_hash_md5(const std::string & username,
                                const std::string & realm,
                                const std::string & password);

    std::string digest_hash_sha256(const std::string & username,
                                   const std::string & realm,
                                   const std::string & password);
}

