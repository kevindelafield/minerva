#pragma once

#include <string>
#include <mutex>
#include <map>
#include <owl/http_auth.h>

namespace authdb
{
    class auth_db : public owl::http_auth_db
    {
    public:
        auth_db(const std::string & webpass) : m_webpass(webpass)
        {
        }
        
        ~auth_db() = default;
        
        bool initialize();

        bool set_user(const std::string & username,
                      const std::string & realm,
                      const std::string & password);

        bool delete_user(const std::string & username);

        bool clear();

        bool find_user(const std::string & username,
                       owl::http_auth_user & user) override;

    private:
        std::map<std::string, owl::http_auth_user> _user_map;
        std::mutex _lock;
        bool _initialized = false;
        std::string m_webpass;

        bool write_map() const;
        bool load_user_map(std::map<std::string,
                           owl::http_auth_user> & user_map);
    };
}
