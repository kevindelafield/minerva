#include <sstream>
#include <cstring>
#include <regex>
#include <set>
#include <cassert>
#include <vector>
#include <vector>
#include <fstream>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <time.h>
#include <stdlib.h>
#include <owl/log.h>
#include <owl/base64.h>
#include <owl/string_utils.h>
#include "http_auth.h"

namespace httpd
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

    const int HTTP_AUTH_DIGEST_MD5_BINLEN = 16; /* MD5_DIGEST_LENGTH */
    const int HTTP_AUTH_DIGEST_SHA256_BINLEN = 32; /* SHA256_DIGEST_LENGTH */

#define ITOSTRING_LENGTH (2 + (8 * sizeof(int) * 31 + 99) / 100)

#define CONST_STR_LEN(x) x, (x) ? sizeof(x) - 1 : 0

    static const char hex_chars_lc[] = "0123456789abcdef";

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

    static void digest_mutate_sha256(http_auth_digest_type algo,
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

        assert(digest.size() == HTTP_AUTH_DIGEST_SHA256_BINLEN*2);
        std::strncpy(a1, digest.c_str(), sizeof(a1));

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

        digest_mutate_sha256(algo, std::string(a1), method, uri,
                             nonce, cnonce, nc, qop, dig);
    }

    static void digest_mutate_md5(http_auth_digest_type algo,
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

        assert(digest.size() == HTTP_AUTH_DIGEST_MD5_BINLEN*2);
        
        std::strncpy(a1, digest.c_str(), sizeof(a1));

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

        digest_mutate_md5(algo, std::string(a1), 
                          method, uri, nonce, cnonce, nc, qop, dig);
    }

    static void digest_nonce_md5(std::stringstream & str, time_t cur_ts, int rnd)
    {
        MD5_CTX ctx;
        unsigned char h[HTTP_AUTH_DIGEST_MD5_BINLEN];
        char hh[HTTP_AUTH_DIGEST_MD5_BINLEN*2+1];
        MD5_Init(&ctx);
        itostrn(hh, sizeof(hh), cur_ts);
        MD5_Update(&ctx, (unsigned char *)hh, strlen(hh));
        itostrn(hh, sizeof(hh), rnd);
        MD5_Update(&ctx, (unsigned char *)hh, strlen(hh));
        MD5_Final(h, &ctx);
        tohex(hh, sizeof(hh), (const char *)h, sizeof(h));
        hh[sizeof(hh)-1] = 0;
        str << hh;
    }

    static void digest_nonce_sha256(std::stringstream & str,
                                    time_t cur_ts,
                                    int rnd)
    {
        SHA256_CTX ctx;
        unsigned char h[HTTP_AUTH_DIGEST_SHA256_BINLEN];
        char hh[HTTP_AUTH_DIGEST_SHA256_BINLEN*2+1];
        SHA256_Init(&ctx);
        itostrn(hh, sizeof(hh), cur_ts);
        SHA256_Update(&ctx, (unsigned char *)hh, strlen(hh));
        itostrn(hh, sizeof(hh), rnd);
        SHA256_Update(&ctx, (unsigned char *)hh, strlen(hh));
        SHA256_Final(h, &ctx);
        tohex(hh, sizeof(hh), (const char *)h, sizeof(h));
        hh[sizeof(hh)-1] = 0;
        str << hh;
    }

    static bool digest_hex2bin(const std::string & hexstr, std::vector<char> & bin)
    {
        /* validate and transform 32-byte MD5 hex string to 16-byte binary MD5,
         * or 64-byte SHA-256 or SHA-512-256 hex string to 32-byte binary digest */
        if (hexstr.size() > (bin.size() << 1))
        {
            return false;
        }

        for (int i = 0, ilen = (int)hexstr.size(); i < ilen; i+=2) {
            int hi = hexstr[i];
            int lo = hexstr[i+1];
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

    static void set_digest_auth_header(http_context & ctx, const std::string & realm)
    {
        time_t ts = time(nullptr);
        int rnd = static_cast<int>(random());

        std::stringstream str;

        str << DIGEST_HDR;
        str << " realm=\"" << realm << "\"";
        str << ", nonce=\"";
        buffer_append_uint_hex(str, static_cast<unsigned int>(ts));
        str << ":";
        digest_nonce_md5(str, ts, static_cast<int>(rnd));
        str << "\"";
        str << ", qop=\"auth\"";

        ctx.response().add_header("WWW-Authenticate", str.str());
    }

    bool authenticate_digest(http_context & ctx,
                             const std::string & authHdr,
                             const std::string & in_realm, 
                             http_auth_db & db,
                             std::string & user)
    {
        std::stringstream ss(authHdr);
    
        std::string part;
    
        // get digest header
        ss >> part;
        if (part != DIGEST_HDR)
        {
            LOG_DEBUG("Not a digest auth header");
            set_digest_auth_header(ctx, in_realm);
            return false;
        }

        http_auth_digest_type algo = HTTP_AUTH_DIGEST_NONE;
        int algo_len = 0;
        std::string username;
        std::string realm;
        std::string uri;
        std::string algorithm;
        std::string qop;
        std::string cnonce;
        std::string nonce;
        std::string nc;
        std::string response;

        std::getline(ss, part, ',');
        while (ss)
        {
            std::regex regex("^(.*)=(.*)$");
            std::smatch match;
            std::regex_search(part, match, regex);
            if (match.size() != 3)
            {
                LOG_WARN("Invalid digest auth part: " << part);
                set_digest_auth_header(ctx, in_realm);
                return false;
            }

            size_t pos = part.find_first_of('=');

            if (pos == std::string::npos || pos == part.size() - 1 || pos == 0)
            {
                LOG_WARN("Invalid digest auth part: " << part);
                set_digest_auth_header(ctx, in_realm);
                return false;
            }

            std::string key = part.substr(0, pos);
            std::string value = part.substr(pos+1);
            owl::trim(key, " ");
            owl::trim(value, "\" ");

            if (key == "username")
            {
                username = value;
            }
            else if (key == "realm")
            {
                realm = value;
            }
            else if (key == "uri")
            {
                uri = value;
            }
            else if (key == "algorithm")
            {
                algorithm = value;
                if (!digest_algorithm_parse(value, algo, algo_len))
                {
                    LOG_WARN("unsupported algorithm: " << algorithm);
                    set_digest_auth_header(ctx, in_realm);
                    return false;
                }
            }
            else if (key == "qop")
            {
                qop = value;
            }
            else if (key == "nonce")
            {
                nonce = value;
            }
            else if (key == "cnonce")
            {
                cnonce = value;
            }
            else if (key == "nc")
            {
                nc = value;
            }
            else if (key == "response")
            {
                response = value;
            }
            else
            {
                LOG_WARN("unknown digest part: " << part);
            }
            std::getline(ss, part, ',');
        }

        if (username.empty() ||
            realm.empty() ||
            nonce.empty() ||
            uri.empty() ||
            (!qop.empty() && (nc.empty() || cnonce.empty())) ||
            response.empty())
        {
            LOG_WARN("missing digest field");
            set_digest_auth_header(ctx, in_realm);
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
            set_digest_auth_header(ctx, in_realm);
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
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        if ((algo & HTTP_AUTH_DIGEST_SESS) && (nonce.empty() || cnonce.empty()))
        {
            LOG_WARN("missing digest algorithm field: " << algorithm);
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        if (realm != entry.realm())
        {
            LOG_WARN("invalid realm: " << realm << " expected: " << entry.realm());
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        std::vector<char> rdigest;
        rdigest.resize(algo_len);

        if (!digest_hex2bin(response, rdigest))
        {
            LOG_WARN("invalid digest response: " << response);
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        if (!qop.empty() && qop == "auth-int")
        {
            LOG_WARN("unsupported digest qop: " << qop);
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        std::vector<char> digest;

        if (algo & HTTP_AUTH_DIGEST_MD5)
        {
            digest_mutate_md5(algo,
                              entry.hash(),
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
            digest_mutate_sha256(algo,
                                 entry.hash(),
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
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        if (rdigest.size() != digest.size())
        {
            LOG_WARN("invalid digest size: " << rdigest.size());
            set_digest_auth_header(ctx, entry.realm());
            return false;
        }

        if (rdigest != digest)
        {
            LOG_WARN("invalid digest password for user: " << username);
            set_digest_auth_header(ctx, entry.realm());
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
        if (!owl::from64tobits(part, buf))
        {
            LOG_ERROR("Failed to base64 decode HTTP Basic auth header");
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }
        
        // look for the separator in the decoded credentials
        auto pos = buf.find(':');
        if (pos == std::string::npos)
        {
            LOG_ERROR("Basic credentials not formatted correctly");
            ctx.response().add_header("WWW-Authenticate", "Basic");
            return false;
        }
        
        // extract username and password
        std::string uname(buf.substr(0, pos));
        std::string pwd(buf.substr(pos+1));
    
        user = uname;

        std::string hash = 
            digest_hash_md5(uname, realm, pwd);
        bool found_user = false;

        http_auth_user entry;
        if (db.find_user(uname, entry))
        {
            return hash == entry.hash();
        }

        ctx.response().add_header("WWW-Authenticate", "Basic");
        return false;
    }
}
