#pragma once

#include <string>
#include <mutex>
#include <map>
#include <httpd/http_auth.h>

namespace authdb
{
    class auth_db : public httpd::http_auth_db
    {
    public:
        auth_db(const std::string & webpass) : m_webpass(webpass)
        {
        }
        
        ~auth_db() = default;
        
        bool initialize() override;

        bool set_user(const std::string & username,
                      const std::string & realm,
                      const std::string & password);

        bool delete_user(const std::string & username);

        bool clear();

        bool find_user(const std::string & username,
                       httpd::http_auth_user & user) override;

    private:
        std::map<std::string, httpd::http_auth_user> _user_map;
        std::mutex _lock;
        bool _initialized = false;
        std::string m_webpass;

        bool write_map() const;
    };
}
