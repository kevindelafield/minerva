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
        auth_db(const std::string & realm,
                const std::string & webpass) :
            httpd::http_auth_db(realm), m_webpass(webpass)
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
        std::map<std::string, httpd::http_auth_user> m_user_map;
        std::mutex m_lock;
        bool m_initialized = false;
        std::string m_webpass;

        bool write_map() const;
    };
}
