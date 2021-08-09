#include <stdio.h>
#include <fstream>
#include <owl/safe_ofstream.h>
#include <owl/log.h>
#include "www_auth_db.h"

using namespace owl;

namespace www
{
    static bool load_user_map(std::map<std::string, http_auth_user> & user_map)
    {
        std::ifstream is("/etc/webpass.txt");

        if (!is.is_open())
        {
            LOG_WARN("failed to open webpass.txt");
            return true;
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
                LOG_ERROR("invalid webpass.txt");
                return false;
            }
            std::getline(ss, realm, ':');
            if (!ss)
            {
                LOG_ERROR("invalid webpass.txt");
                return false;
            }
            std::getline(ss, hash, ':');
            if (!ss)
            {
                LOG_ERROR("invalid webpass.txt");
                return false;
            }

            user_map[user] = http_auth_user(user, realm, hash);

            std::getline(is, line);
        }

        return true;
    }

    www_auth_db::www_auth_db()
    {
    }

    bool www_auth_db::initialize()
    {
        std::unique_lock<std::mutex> lk(_lock);

        if (_initialized)
        {
            return true;
        }
        
        if (!load_user_map(_user_map))
        {
            LOG_ERROR("failed to load http user db");            
            return false;
        }

        _initialized = true;

        return true;
    }

    bool www_auth_db::find_user(const std::string & username,
                                http_auth_user & user)
    {
        assert(_initialized);

        std::unique_lock<std::mutex> lk(_lock);

        auto it = _user_map.find(username);
        if (it != _user_map.end())
        {
            user = it->second;
            return true;
        }
        return false;
    }

    bool www_auth_db::write_map() const
    {
        safe_ofstream os("/adc/persistent/settings/webpass.txt");

        if (!os.is_open())
        {
            LOG_ERROR("failed to open webpass.txt");
            return false;
        }

        for (auto & x : _user_map)
        {
            os << x.second.user() << ":" << x.second.realm() << ":" << x.second.hash() << std::endl;
        }

        if (!os.commit())
        {
            LOG_ERROR("failed to write webpass.txt");
            return false;
        }

        return true;
    }

    bool www_auth_db::set_user(const std::string & username,
                               const std::string & realm,
                               const std::string & password)
    {
        assert(_initialized);

        LOG_DEBUG("setting web user: " << username);

        std::unique_lock<std::mutex> lk(_lock);

        std::string hash = digest_hash_md5(username, realm, password);

        _user_map[username] = http_auth_user(username, realm, hash);

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }

    bool www_auth_db::delete_user(const std::string & username)
    {
        assert(_initialized);

        std::unique_lock<std::mutex> lk(_lock);

        auto it = _user_map.find(username);
        if (it == _user_map.end())
        {
            return true;
        }

        _user_map.erase(it);

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }

    bool www_auth_db::clear()
    {
        assert(_initialized);

        std::unique_lock<std::mutex> lk(_lock);

        _user_map.clear();

        if (!write_map())
        {
            LOG_ERROR("failed to write web user db");
            return false;
        }

        return true;
    }
}
