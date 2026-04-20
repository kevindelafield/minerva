#include <stdio.h>
#include <cerrno>
#include <cctype>
#include <fstream>
#include <sstream>
#include <openssl/crypto.h>
#include <util/safe_ofstream.h>
#include <util/log.h>
#include "auth_db.h"

namespace minerva
{
    static bool contains_separator(const std::string & s)
    {
        return s.find(':') != std::string::npos ||
               s.find('\r') != std::string::npos ||
               s.find('\n') != std::string::npos ||
               s.find('\0') != std::string::npos;
    }

    static void secure_zero(std::string & s)
    {
        if (!s.empty())
        {
            OPENSSL_cleanse(&s[0], s.size());
        }
        s.clear();
    }

    bool auth_db::initialize()
    {
        // Load into a temporary map first; only swap into m_user_map on
        // full success so a partial/corrupt file does not destroy the live
        // credential database.
        std::map<std::string, minerva::http_auth_user> staged;

        std::ifstream is(m_webpass);
        if (!is.is_open())
        {
            // A missing file is treated as an empty database so callers
            // (e.g. the shield CLI) can bootstrap the very first user
            // without having to pre-create the file.  Any other open
            // failure (permissions, EIO, ...) is still fatal.
            if (errno == ENOENT)
            {
                LOG_INFO("auth db " << m_webpass
                         << " does not exist; starting empty");
                m_initialized.store(true, std::memory_order_release);
                return true;
            }
            LOG_WARN("failed to open auth db " << m_webpass);
            return false;
        }

        auto is_hex = [](const std::string & s) {
            for (char c : s)
            {
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            return !s.empty();
        };

        std::string line;
        size_t lineno = 0;
        while (std::getline(is, line))
        {
            ++lineno;

            // Strip trailing CR (CRLF files) and skip blank/comment lines.
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            // Supported on-disk formats:
            //   user:realm:md5hex                 (legacy, MD5 only)
            //   user:realm:md5hex:sha256hex       (current; either hash may
            //                                      be empty, but not both)
            std::stringstream ss(line);
            std::string user;
            std::string realm;
            std::string md5_hash;
            std::string sha256_hash;
            if (!std::getline(ss, user, ':') ||
                !std::getline(ss, realm, ':') ||
                !std::getline(ss, md5_hash, ':'))
            {
                LOG_ERROR("invalid webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            // Optional 4th column: SHA-256 hash.
            std::getline(ss, sha256_hash);

            if (user.empty() || realm.empty())
            {
                LOG_ERROR("empty user/realm in webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            if (md5_hash.empty() && sha256_hash.empty())
            {
                LOG_ERROR("user '" << user
                          << "' has no hash in webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            if (!md5_hash.empty() && !is_hex(md5_hash))
            {
                LOG_ERROR("non-hex MD5 hash in webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            if (!sha256_hash.empty() && !is_hex(sha256_hash))
            {
                LOG_ERROR("non-hex SHA-256 hash in webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            // Sanity: enforce expected hash lengths if present.
            if (!md5_hash.empty() && md5_hash.size() != 32)
            {
                LOG_ERROR("MD5 hash wrong length in webpass db " << m_webpass
                          << " at line " << lineno);
                return false;
            }
            if (!sha256_hash.empty() && sha256_hash.size() != 64)
            {
                LOG_ERROR("SHA-256 hash wrong length in webpass db "
                          << m_webpass << " at line " << lineno);
                return false;
            }
            if (realm != http_auth_db::realm())
            {
                LOG_WARN("user '" << user << "' has realm '" << realm
                         << "' which differs from db realm '"
                         << http_auth_db::realm()
                         << "'; this user will not authenticate via digest");
            }

            staged[user] = minerva::http_auth_user(user, realm,
                                                   md5_hash, sha256_hash);
        }

        // Atomically swap in the new map.
        {
            std::unique_lock<std::mutex> lk(m_lock);
            m_user_map.swap(staged);
        }
        m_initialized.store(true, std::memory_order_release);

        return true;
    }

    bool auth_db::find_user(const std::string & username,
                            minerva::http_auth_user & user)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            LOG_WARN("auth_db::find_user called before initialize()");
            return false;
        }

        std::unique_lock<std::mutex> lk(m_lock);

        auto it = m_user_map.find(username);
        if (it != m_user_map.end())
        {
            user = it->second;
            return true;
        }
        return false;
    }

    bool auth_db::write_map_locked() const
    {
        minerva::safe_ofstream os(m_webpass);

        if (!os.is_open())
        {
            LOG_ERROR("failed to open webpass db " << m_webpass);
            return false;
        }

        for (auto & x : m_user_map)
        {
            // Defense in depth: refuse to serialize entries that would
            // corrupt the on-disk colon-delimited format.
            if (contains_separator(x.second.user()) ||
                contains_separator(x.second.realm()) ||
                contains_separator(x.second.hash_md5()) ||
                contains_separator(x.second.hash_sha256()))
            {
                LOG_ERROR("refusing to serialize user with invalid chars: "
                          << x.first);
                return false;
            }
            // 4-column format: user:realm:md5hex:sha256hex.  Either hash
            // column may be empty (legacy entries lacking SHA-256), but not
            // both -- initialize() rejects that on read.
            os << x.second.user() << ":" << x.second.realm() << ":"
               << x.second.hash_md5() << ":" << x.second.hash_sha256()
               << std::endl;
        }

        if (!os.commit())
        {
            LOG_ERROR("failed to write to webpass db " << m_webpass);
            return false;
        }

        return true;
    }

    bool auth_db::set_user(const std::string & username,
                           const std::string & realm,
                           const std::string & password)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            LOG_ERROR("auth_db::set_user called before initialize()");
            return false;
        }

        if (username.empty() || realm.empty())
        {
            LOG_ERROR("auth_db::set_user requires non-empty username/realm");
            return false;
        }
        if (contains_separator(username) || contains_separator(realm))
        {
            LOG_ERROR("auth_db::set_user rejected colon/CR/LF/NUL in "
                      "username or realm");
            return false;
        }
        if (realm != http_auth_db::realm())
        {
            LOG_WARN("auth_db::set_user user '" << username
                     << "' realm '" << realm << "' differs from db realm '"
                     << http_auth_db::realm()
                     << "'; this user will not authenticate via digest");
        }

        LOG_DEBUG("setting web user: " << username);

        // Compute both hashes so that either MD5 or SHA-256 digest auth
        // challenges can be satisfied for this user.
        std::string md5_hash =
            minerva::digest_hash_md5(username, realm, password);
        std::string sha256_hash =
            minerva::digest_hash_sha256(username, realm, password);

        bool ok = false;
        {
            std::unique_lock<std::mutex> lk(m_lock);
            m_user_map[username] = minerva::http_auth_user(username, realm,
                                                           md5_hash,
                                                           sha256_hash);
            ok = write_map_locked();
        }
        // The credential-equivalent hashes should not linger in memory.
        secure_zero(md5_hash);
        secure_zero(sha256_hash);

        if (!ok)
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }
        return true;
    }

    bool auth_db::delete_user(const std::string & username)
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            LOG_ERROR("auth_db::delete_user called before initialize()");
            return false;
        }

        std::unique_lock<std::mutex> lk(m_lock);

        auto it = m_user_map.find(username);
        if (it == m_user_map.end())
        {
            return true;
        }

        m_user_map.erase(it);

        if (!write_map_locked())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }

    bool auth_db::clear()
    {
        if (!m_initialized.load(std::memory_order_acquire))
        {
            LOG_ERROR("auth_db::clear called before initialize()");
            return false;
        }

        std::unique_lock<std::mutex> lk(m_lock);

        m_user_map.clear();

        if (!write_map_locked())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }
}
