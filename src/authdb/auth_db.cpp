#include <stdio.h>
#include <fstream>
#include <owl/safe_ofstream.h>
#include <owl/log.h>
#include "auth_db.h"

using namespace owl;

namespace authdb
{
    bool auth_db::initialize()
    {
        std::unique_lock<std::mutex> lk(m_lock);

        m_user_map.clear();

        std::ifstream is(m_webpass);

        if (!is.is_open())
        {
            LOG_WARN("failed to open auth db " << m_webpass);
            return false;
        }

        std::string line;

        std::getline(is, line);
        while (is)
        {
            std::stringstream ss(line);
            std::string user;
            std::string realm;
            std::string hash;
            std::getline(ss, user, ':');
            if (!ss)
            {
                LOG_ERROR("invalid webpass db " << m_webpass);
                return false;
            }
            std::getline(ss, realm, ':');
            if (!ss)
            {
                LOG_ERROR("invalid webpass db " << m_webpass);
                return false;
            }
            std::getline(ss, hash, ':');
            if (!ss)
            {
                LOG_ERROR("invalid webpass db " << m_webpass);
                return false;
            }

            m_user_map[user] = httpd::http_auth_user(user, realm, hash);

            std::getline(is, line);
        }

        m_initialized = true;

        return true;
    }

    bool auth_db::find_user(const std::string & username,
                            httpd::http_auth_user & user)
    {
        assert(m_initialized);

        std::unique_lock<std::mutex> lk(m_lock);

        auto it = m_user_map.find(username);
        if (it != m_user_map.end())
        {
            user = it->second;
            return true;
        }
        return false;
    }

    bool auth_db::write_map() const
    {
        safe_ofstream os(m_webpass);

        if (!os.is_open())
        {
            LOG_ERROR("failed to open webpass db " << m_webpass);
            return false;
        }

        for (auto & x : m_user_map)
        {
            os << x.second.user() << ":" << x.second.realm() << ":" << x.second.hash() << std::endl;
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
        assert(m_initialized);

        LOG_DEBUG("setting web user: " << username);

        std::unique_lock<std::mutex> lk(m_lock);

        std::string hash = httpd::digest_hash_md5(username, realm, password);

        m_user_map[username] = httpd::http_auth_user(username, realm, hash);

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }

    bool auth_db::delete_user(const std::string & username)
    {
        assert(m_initialized);

        std::unique_lock<std::mutex> lk(m_lock);

        auto it = m_user_map.find(username);
        if (it == m_user_map.end())
        {
            return true;
        }

        m_user_map.erase(it);

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }

    bool auth_db::clear()
    {
        assert(m_initialized);

        std::unique_lock<std::mutex> lk(m_lock);

        m_user_map.clear();

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }
}
