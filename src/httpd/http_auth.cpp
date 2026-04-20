#define OPENSSL_API_COMPAT 0x10100000L

#include <sstream>
#include <cstring>
#include <regex>
#include <set>
#include <cassert>
#include <vector>
#include <vector>
#include <fstream>
#include <random>
#include <climits>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <time.h>
#include <stdlib.h>
#include <util/log.h>
#include <util/base64.h>
#include <util/string_utils.h>
#include "http_auth.h"

namespace minerva
{
    enum http_auth_digest_type {
        HTTP_AUTH_DIGEST_NONE       = 0
        ,HTTP_AUTH_DIGEST_SESS       = 0x01
        ,HTTP_AUTH_DIGEST_MD5        = 0x02
        ,HTTP_AUTH_DIGEST_SHA256     = 0x04
    };

    inline http_auth_digest_type operator|=(http_auth_digest_type & a,
                                            http_auth_digest_type b)
    {
        return (http_auth_digest_type&)((int&)a |= (int)b);
    }

    static const int MAX_BASIC_AUTH_HDR_LEN = 256;

    // Securely zero a std::string's contents. Uses OPENSSL_cleanse so the
    // compiler cannot elide the write.
    static void secure_zero_string(std::string & s)
    {
        if (!s.empty())
        {
            OPENSSL_cleanse(&s[0], s.size());
        }
        s.clear();
    }

    const int HTTP_AUTH_DIGEST_MD5_BINLEN = 16; /* MD5_DIGEST_LENGTH */
    const int HTTP_AUTH_DIGEST_SHA256_BINLEN = 32; /* SHA256_DIGEST_LENGTH */

#define ITOSTRING_LENGTH (2 + (8 * sizeof(int) * 31 + 99) / 100)

#define CONST_STR_LEN(x) x, (x) ? sizeof(x) - 1 : 0

    static const char hex_chars_lc[] = "0123456789abcdef";

    // Constant-time comparison to prevent timing attacks
    static bool secure_compare(const std::vector<char>& a, const std::vector<char>& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        
        volatile unsigned char result = 0;
        for (size_t i = 0; i < a.size(); i++) {
            result |= (a[i] ^ b[i]);
        }
        return result == 0;
    }

    // Constant-time string comparison to prevent timing attacks
    static bool secure_compare_strings(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        
        volatile unsigned char result = 0;
        for (size_t i = 0; i < a.size(); i++) {
            result |= (a[i] ^ b[i]);
        }
        return result == 0;
    }

    static char* utostr(char * const buf_end, unsigned int val) {
        char *cur = buf_end;
        do {
            int mod = val % 10;
            val /= 10;
            /* prepend digit mod */
            *(--cur) = (char) ('0' + mod);
        } while (0 != val);
        return cur;
    }

    static char* itostr(char * const buf_end, int val) {
        /* absolute value not defined for INTMAX_MIN, but can take absolute
         * value of any negative number via twos complement cast to unsigned.
         * negative sign is prepended after (now unsigned) value is converted
         * to string */
        unsigned int uval = val >= 0 ? (unsigned int)val : ((unsigned int)~val) + 1;
        char *cur = utostr(buf_end, uval);
        if (val < 0) *(--cur) = '-';

        return cur;
    }

    static void tohex(char *buf, size_t buf_len, const char *s, size_t s_len) {
        for (size_t i = 0; i < s_len; ++i) {
            buf[2*i]   = hex_chars_lc[(s[i] >> 4) & 0x0F];
            buf[2*i+1] = hex_chars_lc[s[i] & 0x0F];
        }
        buf[2*s_len] = '\0';
    }

    static void itostrn(char *buf, size_t buf_len, int val) {
        char p_buf[ITOSTRING_LENGTH];
        char* const p_buf_end = p_buf + sizeof(p_buf);
        char* str = p_buf_end - 1;
        *str = '\0';

        str = itostr(str, val);

        memcpy(buf, str, p_buf_end - str);
    }

    // Helper function to parse digest key-value pairs
    static bool parse_digest_keyvalue(const std::string& digest_str, std::map<std::string, std::string>& params) {
        std::stringstream ss(digest_str);
        std::string part;
        
        // Skip the initial part (should be eaten by caller)
        while (std::getline(ss, part, ',')) {
            // Trim whitespace
            part.erase(0, part.find_first_not_of(" \t"));
            part.erase(part.find_last_not_of(" \t") + 1);
            
            if (part.empty()) continue;
            
            // Find the '=' separator
            size_t pos = part.find('=');
            if (pos == std::string::npos || pos == 0 || pos == part.size() - 1) {
                continue; // Skip malformed parts
            }
            
            std::string key = part.substr(0, pos);
            std::string value = part.substr(pos + 1);
            
            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes from value if present
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            
            params[key] = value;
        }
        
        return true;
    }

    static void buffer_append_uint_hex(std::stringstream & str,
                                       unsigned int value) {
        unsigned int shift = 0;

        {
            unsigned int copy = value;
            do {
                copy >>= 8;
                shift += 8; /* counting bits */
            } while (0 != copy);
        }

        while (shift > 0) {
            shift -= 4;
            str << hex_chars_lc[(value >> shift) & 0x0F];
        }
    }

    static bool digest_mutate_sha256(http_auth_digest_type algo,
                                     const std::string & digest,
                                     const std::string & method,
                                     const std::string & uri,
                                     const std::string & nonce,
                                     const std::string & cnonce,
                                     const std::string & nc,
                                     const std::string & qop,
                                     std::vector<char> & dig)
    {
        SHA256_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_SHA256_BINLEN*2+1];
        char a2[HTTP_AUTH_DIGEST_SHA256_BINLEN*2+1];
        char digest_a[HTTP_AUTH_DIGEST_SHA256_BINLEN];

        if (digest.size() != HTTP_AUTH_DIGEST_SHA256_BINLEN*2)
        {
            LOG_WARN("invalid sha256 digest length: " << digest.size());
            return false;
        }
        std::memcpy(a1, digest.data(), HTTP_AUTH_DIGEST_SHA256_BINLEN*2);
        a1[HTTP_AUTH_DIGEST_SHA256_BINLEN*2] = '\0';

        /* calculate H(A2) */
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, (unsigned char *)method.c_str(), method.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, (unsigned char *)uri.c_str(), uri.size());
        SHA256_Final((unsigned char *)digest_a, &ctx);
        tohex(a2, sizeof(a2), (const char *)digest_a, sizeof(digest_a));

        /* calculate response */
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, (unsigned char *)a1, sizeof(a1)-1);
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, (unsigned char *)nonce.c_str(), nonce.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        if (!qop.empty()) {
            SHA256_Update(&ctx, (unsigned char *)nc.c_str(), nc.size());
            SHA256_Update(&ctx, CONST_STR_LEN(":"));
            SHA256_Update(&ctx, (unsigned char *)cnonce.c_str(), cnonce.size());
            SHA256_Update(&ctx, CONST_STR_LEN(":"));
            SHA256_Update(&ctx, (unsigned char *)qop.c_str(), qop.size());
            SHA256_Update(&ctx, CONST_STR_LEN(":"));
        }
        SHA256_Update(&ctx, (unsigned char *)a2, sizeof(a2)-1);
        SHA256_Final((unsigned char *)digest_a, &ctx);

        std::copy(&digest_a[0], &digest_a[0] + HTTP_AUTH_DIGEST_SHA256_BINLEN,
                  std::back_inserter(dig));
        return true;
    }

    static void digest_mutate_sha256(http_auth_digest_type algo,
                                     const std::string & username,
                                     const std::string & realm,
                                     const std::string & password,
                                     const std::string & method,
                                     const std::string & uri,
                                     const std::string & nonce,
                                     const std::string & cnonce,
                                     const std::string & nc,
                                     const std::string & qop,
                                     std::vector<char> & dig)
    {
        SHA256_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_SHA256_BINLEN*2+1];

        char digest[HTTP_AUTH_DIGEST_SHA256_BINLEN];

        SHA256_Init(&ctx);
        SHA256_Update(&ctx, username.c_str(), username.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, realm.c_str(), realm.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, password.c_str(), password.size());
        SHA256_Final((unsigned char *)digest, &ctx);

        if (algo & HTTP_AUTH_DIGEST_SESS)
        {
            SHA256_Init(&ctx);
            tohex(a1, sizeof(a1), (const char *)digest, sizeof(digest));
            SHA256_Update(&ctx, (unsigned char *)a1, sizeof(a1)-1);
            SHA256_Update(&ctx, CONST_STR_LEN(":"));
            SHA256_Update(&ctx, (unsigned char *)nonce.c_str(), nonce.size());
            SHA256_Update(&ctx, CONST_STR_LEN(":"));
            SHA256_Update(&ctx, (unsigned char *)cnonce.c_str(), cnonce.size());
            SHA256_Final((unsigned char *)digest, &ctx);
        }
    
        tohex(a1, sizeof(a1), digest, sizeof(digest));

        (void)digest_mutate_sha256(algo, std::string(a1), method, uri,
                                   nonce, cnonce, nc, qop, dig);
    }

    static bool digest_mutate_md5(http_auth_digest_type algo,
                                  const std::string & digest,
                                  const std::string & method,
                                  const std::string & uri,
                                  const std::string & nonce,
                                  const std::string & cnonce,
                                  const std::string & nc,
                                  const std::string & qop,
                                  std::vector<char> & dig)
    {
        MD5_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];
        char a2[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];
        char digest_a[HTTP_AUTH_DIGEST_MD5_BINLEN];

        if (digest.size() != HTTP_AUTH_DIGEST_MD5_BINLEN*2)
        {
            LOG_WARN("invalid md5 digest length: " << digest.size());
            return false;
        }
        std::memcpy(a1, digest.data(), HTTP_AUTH_DIGEST_MD5_BINLEN*2);
        a1[HTTP_AUTH_DIGEST_MD5_BINLEN*2] = '\0';

        /* calculate H(A2) */
        MD5_Init(&ctx);
        MD5_Update(&ctx, (unsigned char *)method.c_str(), method.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, (unsigned char *)uri.c_str(), uri.size());
        MD5_Final((unsigned char *)digest_a, &ctx);
        tohex(a2, sizeof(a2), digest_a, sizeof(digest_a));

        /* calculate response */
        MD5_Init(&ctx);
        MD5_Update(&ctx, (unsigned char *)a1, sizeof(a1)-1);
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, (unsigned char *)nonce.c_str(), nonce.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        if (!qop.empty())
        {
            MD5_Update(&ctx, (unsigned char *)nc.c_str(), nc.size());
            MD5_Update(&ctx, CONST_STR_LEN(":"));
            MD5_Update(&ctx, (unsigned char *)cnonce.c_str(), cnonce.size());
            MD5_Update(&ctx, CONST_STR_LEN(":"));
            MD5_Update(&ctx, (unsigned char *)qop.c_str(), qop.size());
            MD5_Update(&ctx, CONST_STR_LEN(":"));
        }
        MD5_Update(&ctx, (unsigned char *)a2, sizeof(a2)-1);
        MD5_Final((unsigned char *)digest_a, &ctx);

        std::copy(&digest_a[0], &digest_a[0] + HTTP_AUTH_DIGEST_MD5_BINLEN,
                  std::back_inserter(dig));
        return true;
    }

    std::string digest_hash_md5(const std::string & username,
                                const std::string & realm,
                                const std::string & password)
    {
        MD5_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];
        char a2[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];

        char digest[HTTP_AUTH_DIGEST_MD5_BINLEN];

        MD5_Init(&ctx);
        MD5_Update(&ctx, username.c_str(), username.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, realm.c_str(), realm.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, password.c_str(), password.size());
        MD5_Final((unsigned char *)digest, &ctx);

        tohex(a1, sizeof(a1), digest, sizeof(digest));

        return std::string(a1);
    }

    std::string digest_hash_sha256(const std::string & username,
                                   const std::string & realm,
                                   const std::string & password)
    {
        SHA256_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_SHA256_BINLEN*2+1];
        char digest[HTTP_AUTH_DIGEST_SHA256_BINLEN];

        SHA256_Init(&ctx);
        SHA256_Update(&ctx, username.c_str(), username.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, realm.c_str(), realm.size());
        SHA256_Update(&ctx, CONST_STR_LEN(":"));
        SHA256_Update(&ctx, password.c_str(), password.size());
        SHA256_Final((unsigned char *)digest, &ctx);

        tohex(a1, sizeof(a1), digest, sizeof(digest));

        return std::string(a1);
    }

    static void digest_mutate_md5(http_auth_digest_type algo,
                                  const std::string & username,
                                  const std::string & realm,
                                  const std::string & password,
                                  const std::string & method,
                                  const std::string & uri,
                                  const std::string & nonce,
                                  const std::string & cnonce,
                                  const std::string & nc,
                                  const std::string & qop,
                                  std::vector<char> & dig)
    {
        MD5_CTX ctx;
        char a1[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];
        char a2[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];

        char digest[HTTP_AUTH_DIGEST_MD5_BINLEN];

        MD5_Init(&ctx);
        MD5_Update(&ctx, username.c_str(), username.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, realm.c_str(), realm.size());
        MD5_Update(&ctx, CONST_STR_LEN(":"));
        MD5_Update(&ctx, password.c_str(), password.size());
        MD5_Final((unsigned char *)digest, &ctx);

        if (algo & HTTP_AUTH_DIGEST_SESS) {
            MD5_Init(&ctx);
            /* http://www.rfc-editor.org/errata_search.php?rfc=2617
             * Errata ID: 1649 */
            tohex(a1, sizeof(a1), digest, sizeof(digest));
            MD5_Update(&ctx, (unsigned char *)a1, sizeof(a1)-1);
            MD5_Update(&ctx, CONST_STR_LEN(":"));
            MD5_Update(&ctx, (unsigned char *)nonce.c_str(), nonce.size());
            MD5_Update(&ctx, CONST_STR_LEN(":"));
            MD5_Update(&ctx, (unsigned char *)cnonce.c_str(), cnonce.size());
            MD5_Final((unsigned char *)digest, &ctx);
        }

        tohex(a1, sizeof(a1), digest, sizeof(digest));

        (void)digest_mutate_md5(algo, std::string(a1), 
                                method, uri, nonce, cnonce, nc, qop, dig);
    }

    static bool digest_hex2bin(const std::string & hexstr, std::vector<char> & bin)
    {
        // Require exact length: 32 hex chars -> 16 bytes (MD5),
        // or 64 hex chars -> 32 bytes (SHA-256). Odd-length is invalid.
        if ((hexstr.size() & 1u) != 0u)
        {
            return false;
        }
        if (hexstr.size() != (bin.size() << 1))
        {
            return false;
        }

        for (int i = 0, ilen = (int)hexstr.size(); i < ilen; i+=2) {
            int hi = (unsigned char)hexstr[i];
            int lo = (unsigned char)hexstr[i+1];
            if ('0' <= hi && hi <= '9')
                hi -= '0';
            else if ((hi |= 0x20), 'a' <= hi && hi <= 'f')
                hi += -'a' + 10;
            else
                return false;
            if ('0' <= lo && lo <= '9')
                lo -= '0';
            else if ((lo |= 0x20), 'a' <= lo && lo <= 'f')
                lo += -'a' + 10;
            else
                return false;
            bin[(i >> 1)] = (unsigned char)((hi << 4) | lo);
        }
        return true;
    }

    static bool digest_algorithm_parse(const std::string & s,
                                       http_auth_digest_type & dalgo,
                                       int & dlen) {
        if (s.empty()) {
            dalgo = HTTP_AUTH_DIGEST_MD5;
            dlen  = HTTP_AUTH_DIGEST_MD5_BINLEN;
            return true;
        }
    
        auto len = s.size();
        if (len > 5
            && (s[len-5]       ) == '-'
            && (s[len-4] | 0x20) == 's'
            && (s[len-3] | 0x20) == 'e'
            && (s[len-2] | 0x20) == 's'
            && (s[len-1] | 0x20) == 's') {
            dalgo = HTTP_AUTH_DIGEST_SESS;
            len -= 5;
        }
        else
        {
            dalgo = HTTP_AUTH_DIGEST_NONE;
        }
    
        if (3 == len
            && 'm' == (s[0] | 0x20)
            && 'd' == (s[1] | 0x20)
            && '5' == (s[2]       ))
        {
            dalgo |= HTTP_AUTH_DIGEST_MD5;
            dlen   = HTTP_AUTH_DIGEST_MD5_BINLEN;
            return true;
        }
        else if (len >= 7
                 && 's' == (s[0] | 0x20)
                 && 'h' == (s[1] | 0x20)
                 && 'a' == (s[2] | 0x20)
                 && '-' == (s[3]       ))
        {
            if (len == 7 && s[4] == '2' && s[5] == '5' && s[6] == '6') {
                dalgo |= HTTP_AUTH_DIGEST_SHA256;
                dlen   = HTTP_AUTH_DIGEST_SHA256_BINLEN;
                return true;
            }
        }
        return false;
    }

    static void set_digest_auth_header(http_context & ctx,
                                       const std::string & realm,
                                       http_auth_nonce_store & store,
                                       bool stale = false)
    {
        // Issue ONE nonce that both algorithm challenges share, so we don't
        // double our nonce-store footprint per challenge.
        std::string nonce = store.issue(ctx.client_ip(), realm);

        auto build = [&](const char * algo_name) {
            std::stringstream str;
            str << DIGEST_HDR;
            str << " realm=\"" << realm << "\"";
            str << ", nonce=\"" << nonce << "\"";
            str << ", qop=\"auth\"";
            str << ", algorithm=" << algo_name;
            if (stale)
            {
                str << ", stale=true";
            }
            return str.str();
        };

        // Per RFC 7616 §3.7, a server MAY offer multiple algorithm choices
        // by sending multiple WWW-Authenticate headers; the client picks
        // the strongest one it supports.  We list SHA-256 first as the
        // preferred choice.
        ctx.response().add_header("WWW-Authenticate", build("SHA-256"));
        ctx.response().add_header("WWW-Authenticate", build("MD5"));
    }

    // ---- http_auth_nonce_store ------------------------------------------

    http_auth_nonce_store::http_auth_nonce_store()
    {
        if (RAND_bytes(m_secret, sizeof(m_secret)) != 1)
        {
            // RAND_bytes failure is exceedingly unlikely but is fatal for
            // authentication security; abort rather than fall back to a
            // predictable secret.
            FATAL("RAND_bytes failed when seeding HTTP digest nonce secret");
        }
    }

    std::string http_auth_nonce_store::compute_mac_hex(
        const std::string & ts_hex,
        const std::string & client_ip,
        const std::string & realm) const
    {
        unsigned char mac[EVP_MAX_MD_SIZE];
        unsigned int mac_len = 0;

        std::string buf;
        buf.reserve(ts_hex.size() + client_ip.size() + realm.size() + 2);
        buf.append(ts_hex);
        buf.push_back(':');
        buf.append(client_ip);
        buf.push_back(':');
        buf.append(realm);

        if (HMAC(EVP_sha256(), m_secret, sizeof(m_secret),
                 reinterpret_cast<const unsigned char *>(buf.data()),
                 buf.size(),
                 mac, &mac_len) == nullptr || mac_len == 0)
        {
            return std::string();
        }

        char hex[EVP_MAX_MD_SIZE * 2 + 1];
        tohex(hex, sizeof(hex),
              reinterpret_cast<const char *>(mac), mac_len);
        return std::string(hex, mac_len * 2);
    }

    void http_auth_nonce_store::prune_locked(std::time_t now)
    {
        // Drop any state at the front of the FIFO that has expired.
        while (!m_order.empty())
        {
            auto it = m_state.find(m_order.front());
            if (it == m_state.end())
            {
                m_order.pop_front();
                continue;
            }
            if (it->second.second > now)
            {
                break;
            }
            m_state.erase(it);
            m_order.pop_front();
        }
        // Hard cap: drop oldest until under the limit.
        while (m_order.size() > m_max_entries)
        {
            m_state.erase(m_order.front());
            m_order.pop_front();
        }
    }

    std::string http_auth_nonce_store::issue(const std::string & client_ip,
                                             const std::string & realm)
    {
        std::time_t now = std::time(nullptr);

        std::stringstream ts_ss;
        buffer_append_uint_hex(ts_ss, static_cast<unsigned int>(now));
        std::string ts_hex = ts_ss.str();

        std::string mac_hex = compute_mac_hex(ts_hex, client_ip, realm);
        if (mac_hex.empty())
        {
            return std::string();
        }

        std::string nonce = ts_hex + ":" + mac_hex;

        std::lock_guard<std::mutex> lk(m_lock);
        prune_locked(now);
        // Track the nonce with no nc usage yet.
        if (m_state.emplace(nonce,
                            std::make_pair(0ul, now + m_max_age)).second)
        {
            m_order.push_back(nonce);
        }
        return nonce;
    }

    http_auth_nonce_store::validate_result
    http_auth_nonce_store::validate(const std::string & nonce,
                                    const std::string & nc_hex,
                                    const std::string & client_ip,
                                    const std::string & realm)
    {
        // Split nonce into ts:mac.
        size_t colon = nonce.find(':');
        if (colon == std::string::npos ||
            colon == 0 ||
            colon == nonce.size() - 1)
        {
            return validate_result::INVALID;
        }
        std::string ts_hex = nonce.substr(0, colon);
        std::string mac_hex = nonce.substr(colon + 1);

        // Sanity bounds before crypto.
        if (ts_hex.size() > 16 || mac_hex.size() != 64)
        {
            return validate_result::INVALID;
        }
        for (char c : ts_hex)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
            {
                return validate_result::INVALID;
            }
        }
        for (char c : mac_hex)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
            {
                return validate_result::INVALID;
            }
        }

        // Recompute and constant-time compare.
        std::string expected_mac = compute_mac_hex(ts_hex, client_ip, realm);
        if (expected_mac.size() != mac_hex.size() ||
            !secure_compare_strings(expected_mac, mac_hex))
        {
            return validate_result::INVALID;
        }

        // Parse timestamp and check window.
        unsigned long long ts_val = 0;
        try
        {
            ts_val = std::stoull(ts_hex, nullptr, 16);
        }
        catch (const std::exception &)
        {
            return validate_result::INVALID;
        }
        std::time_t ts = static_cast<std::time_t>(ts_val);
        std::time_t now = std::time(nullptr);
        if (now < ts || (now - ts) > m_max_age)
        {
            return validate_result::STALE;
        }

        // Parse nc if provided.
        unsigned long nc = 0;
        bool have_nc = !nc_hex.empty();
        if (have_nc)
        {
            if (nc_hex.size() > 8)
            {
                return validate_result::INVALID;
            }
            for (char c : nc_hex)
            {
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                {
                    return validate_result::INVALID;
                }
            }
            try
            {
                nc = std::stoul(nc_hex, nullptr, 16);
            }
            catch (const std::exception &)
            {
                return validate_result::INVALID;
            }
            if (nc == 0)
            {
                return validate_result::INVALID;
            }
        }

        // Replay check.
        std::lock_guard<std::mutex> lk(m_lock);
        prune_locked(now);

        auto it = m_state.find(nonce);
        if (it == m_state.end())
        {
            // Not a nonce we issued (or it was pruned). Treat as stale so the
            // client retries with a fresh challenge transparently.
            return validate_result::STALE;
        }

        unsigned long & last_nc = it->second.first;
        if (have_nc)
        {
            if (nc <= last_nc)
            {
                return validate_result::REPLAY;
            }
            last_nc = nc;
        }
        else
        {
            // No qop/nc: the nonce may be used only once.
            if (last_nc != 0)
            {
                return validate_result::REPLAY;
            }
            last_nc = 1;
        }
        return validate_result::OK;
    }

    bool authenticate_digest(http_context & ctx,
                             const std::string & authHdr,
                             const std::string & in_realm,
                             http_auth_db & db,
                             http_auth_nonce_store & nonce_store,
                             std::string & user)
    {
        std::stringstream ss(authHdr);
    
        std::string part;
    
        // get digest header
        ss >> part;
        if (part != DIGEST_HDR)
        {
            LOG_DEBUG("Not a digest auth header");
            set_digest_auth_header(ctx, in_realm, nonce_store);
            return false;
        }

        // Get remaining authorization header content for parsing
        std::string digest_params_str;
        std::getline(ss, digest_params_str);

        // Parse digest parameters using helper function
        std::map<std::string, std::string> digest_params;
        if (!parse_digest_keyvalue(digest_params_str, digest_params))
        {
            LOG_WARN("failed to parse digest parameters");
            set_digest_auth_header(ctx, in_realm, nonce_store);
            return false;
        }

        // Extract required parameters
        const std::string& username = digest_params["username"];
        const std::string& realm = digest_params["realm"];
        const std::string& nonce = digest_params["nonce"];
        const std::string& uri = digest_params["uri"];
        const std::string& qop = digest_params["qop"];
        const std::string& cnonce = digest_params["cnonce"];
        const std::string& nc = digest_params["nc"];
        const std::string& response = digest_params["response"];
        const std::string& algorithm = digest_params["algorithm"];

        // Parse algorithm if specified
        http_auth_digest_type algo = HTTP_AUTH_DIGEST_NONE;
        int algo_len = 0;
        if (!algorithm.empty() && !digest_algorithm_parse(algorithm, algo, algo_len))
        {
            LOG_WARN("unsupported algorithm: " << algorithm);
            set_digest_auth_header(ctx, in_realm, nonce_store);
            return false;
        }

        if (username.empty() ||
            realm.empty() ||
            nonce.empty() ||
            uri.empty() ||
            (!qop.empty() && (nc.empty() || cnonce.empty())) ||
            response.empty())
        {
            LOG_WARN("missing digest field");
            set_digest_auth_header(ctx, in_realm, nonce_store);
            return false;
        }

        bool found_user = false;

        http_auth_user entry;
        if (db.find_user(username, entry))
        {
            found_user = true;
        }

        if (!found_user)
        {
            LOG_WARN("invalid digest user: " << username);
            set_digest_auth_header(ctx, in_realm, nonce_store);
            return false;
        }

        if (algorithm.empty())
        {
            algo = HTTP_AUTH_DIGEST_MD5;
            algo_len = HTTP_AUTH_DIGEST_MD5_BINLEN;
        }

        if (!(algo & ~HTTP_AUTH_DIGEST_SESS))
        {
            LOG_WARN("missing digest algorithm");
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        if ((algo & HTTP_AUTH_DIGEST_SESS) && (nonce.empty() || cnonce.empty()))
        {
            LOG_WARN("missing digest algorithm field: " << algorithm);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        if (realm != entry.realm())
        {
            LOG_WARN("invalid realm: " << realm << " expected: " << entry.realm());
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        std::vector<char> rdigest;
        rdigest.resize(algo_len);

        if (!digest_hex2bin(response, rdigest))
        {
            LOG_WARN("invalid digest response: " << response);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        if (!qop.empty() && qop == "auth-int")
        {
            LOG_WARN("unsupported digest qop: " << qop);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        std::vector<char> digest;
        bool digest_ok = false;

        if (algo & HTTP_AUTH_DIGEST_MD5)
        {
            const std::string & user_hash = entry.hash_md5();
            if (user_hash.empty())
            {
                LOG_WARN("user '" << username
                         << "' has no MD5 credential for digest auth");
                set_digest_auth_header(ctx, entry.realm(), nonce_store);
                return false;
            }
            digest_ok = digest_mutate_md5(algo,
                                          user_hash,
                                          ctx.request().method_as_string(),
                                          uri,
                                          nonce,
                                          cnonce,
                                          nc,
                                          qop,
                                          digest);
        }
        else if (algo & HTTP_AUTH_DIGEST_SHA256)
        {
            const std::string & user_hash = entry.hash_sha256();
            if (user_hash.empty())
            {
                LOG_WARN("user '" << username
                         << "' has no SHA-256 credential for digest auth");
                set_digest_auth_header(ctx, entry.realm(), nonce_store);
                return false;
            }
            digest_ok = digest_mutate_sha256(algo,
                                             user_hash,
                                             ctx.request().method_as_string(),
                                             uri,
                                             nonce,
                                             cnonce,
                                             nc,
                                             qop,
                                             digest);
        }
        else
        {
            LOG_WARN("unsupported digest algorithm: " << algo);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        if (!digest_ok)
        {
            LOG_WARN("digest computation failed for user: " << username);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        if (rdigest.size() != digest.size())
        {
            LOG_WARN("invalid digest size: " << rdigest.size());
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        // Use constant-time comparison to prevent timing attacks
        if (!secure_compare(rdigest, digest))
        {
            LOG_WARN("invalid digest password for user: " << username);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        // Credentials are valid. Now validate the server-issued nonce and
        // enforce nonce-count replay protection. Done after the password
        // check so an attacker cannot probe nonce state without knowing the
        // password.
        auto nv = nonce_store.validate(nonce, nc, ctx.client_ip(),
                                       entry.realm());
        switch (nv)
        {
        case http_auth_nonce_store::validate_result::OK:
            break;
        case http_auth_nonce_store::validate_result::STALE:
            LOG_DEBUG("stale nonce for user: " << username);
            set_digest_auth_header(ctx, entry.realm(), nonce_store, true);
            return false;
        case http_auth_nonce_store::validate_result::REPLAY:
            LOG_WARN("replayed digest nonce for user: " << username);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        case http_auth_nonce_store::validate_result::INVALID:
        default:
            LOG_WARN("invalid digest nonce for user: " << username);
            set_digest_auth_header(ctx, entry.realm(), nonce_store);
            return false;
        }

        user = username;

        return true;
    }

    bool authenticate_basic(http_context & ctx,
                            const std::string & authHdr,
                            const std::string & realm,
                            http_auth_db & db,
                            std::string & user)
    {
        std::stringstream ss(authHdr);
    
        std::string part;
    
        // get basic header
        ss >> part;
        if (part != BASIC_HDR)
        {
            LOG_DEBUG("Not a basic auth header");
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }
    
        // get the encoded credentials
        ss >> part;
    
        if (part.size() > MAX_BASIC_AUTH_HDR_LEN)
        {
            LOG_WARN("Encoded basic credentials are longer than allowed");
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }
    
        // base 64 decode the credentials
        std::string buf;
        // Base64 decode the basic auth string
        if (!from64tobits(part, buf))
        {
            LOG_ERROR("Failed to base64 decode HTTP Basic auth header");
            secure_zero_string(part);
            secure_zero_string(buf);
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }
        // base64 source no longer needed
        secure_zero_string(part);

        // look for the separator in the decoded credentials
        auto pos = buf.find(':');
        if (pos == std::string::npos)
        {
            LOG_ERROR("Basic credentials not formatted correctly");
            secure_zero_string(buf);
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }

        // extract username and password
        std::string uname(buf.substr(0, pos));
        std::string pwd(buf.substr(pos+1));
        // decoded "user:pass" no longer needed
        secure_zero_string(buf);

        user = uname;

        std::string hash =
            digest_hash_md5(uname, realm, pwd);

        // Clear sensitive password data from memory
        secure_zero_string(pwd);

        bool result = false;
        http_auth_user entry;
        if (db.find_user(uname, entry))
        {
            // Use constant-time comparison to prevent timing attacks
            result = secure_compare_strings(hash, entry.hash());
        }

        // Clear hash from memory after use
        secure_zero_string(hash);

        if (!result) {
            ctx.response().add_header("WWW-Authenticate", "Basic");
        }

        return result;
    }
}
