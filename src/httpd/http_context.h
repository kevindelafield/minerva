#pragma once

#include <cstring>
#include <functional>
#include <owl/time_utils.h>
#include <owl/nillable.h>
#include <owl/connection.h>
#include "http_request.h"
#include "http_response.h"

namespace httpd
{
    class http_context
    {
    private:
        const int DEFAULT_TIMEOUT = 60000;
        http_request _request;
        http_response _response;
        std::shared_ptr<owl::connection> _conn;
        std::string m_username;
        std::string m_client_ip;
        struct sockaddr_in m_client_addr;
        socklen_t m_client_addr_len;
        int _timeout_msecs = DEFAULT_TIMEOUT;
        owl::timer _timer;
        std::function<bool()> _sd_cb;
        owl::nillable<std::function<void()>> m_post_command;

    public:

        http_context(std::shared_ptr<owl::connection> conn, std::function<bool()> sdCb)
        : _request(this), _response(this), _conn(conn), _timer(true),
            _sd_cb(sdCb)
            {
                std::memset(&m_client_addr_len, 0, sizeof(m_client_addr_len));
            }

        ~http_context() = default;

        owl::nillable<std::function<void()>> post_command() const
        {
            return m_post_command;
        }

        void client_addr(const struct sockaddr_in & addr,
                         socklen_t addr_len)
        {
            m_client_addr = addr;
            m_client_addr_len = addr_len;
        }

        void client_ip(const std::string & ip)
        {
            m_client_ip = ip;
        }

        std::string client_ip() const
        {
            return m_client_ip;
        }

        bool get_server_ip(std::string & ip);

        void username(const std::string & user)
        {
            m_username = user;
        }

        std::string username() const
        {
            return m_username;
        }

        void post_command(const owl::nillable<std::function<void()>> & cmd)
        {
            m_post_command = cmd;
        }

        http_request & request()
        {
            return _request;
        }

        http_response & response()
        {
            return _response;
        }

        std::shared_ptr<owl::connection> conn() const
        {
            return _conn;
        }

        bool should_shutdown() const
        {
            return _sd_cb();
        }

        int timeout() const
        {
            return _timeout_msecs;
        }

        void timeout(int msecs)
        {
            _timeout_msecs = msecs;
        }

        int get_elapsed_milliseconds()
        {
            return _timer.get_elapsed_milliseconds();
        }

        bool timed_out() const
        {
            return _timer.get_elapsed_milliseconds() > _timeout_msecs;
        }

    };
}
