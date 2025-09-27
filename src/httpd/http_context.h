#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <util/time_utils.h>
#include <optional>
#include <util/connection.h>
#include "http_request.h"
#include "http_response.h"

namespace minerva
{
    class http_context
    {
    public:

        http_context(std::shared_ptr<connection> conn, std::function<bool()> sdCb)
        : m_request(this), m_response(this), m_conn(conn), m_timer(true),
          m_sd_cb(sdCb)
            {
                std::memset(&m_client_addr_len, 0, sizeof(m_client_addr_len));
            }

        ~http_context() = default;

        std::optional<std::function<void()>> post_command() const
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

        void post_command(const std::optional<std::function<void()>> & cmd)
        {
            m_post_command = cmd;
        }

        http_request & request()
        {
            return m_request;
        }

        http_response & response()
        {
            return m_response;
        }

        connection * conn() const
        {
            return m_conn.get();
        }

        bool should_shutdown() const
        {
            return m_sd_cb();
        }

        int timeout() const
        {
            return m_timeout_msecs;
        }

        void timeout(int msecs)
        {
            m_timeout_msecs = msecs;
        }

        int get_elapsed_milliseconds()
        {
            return m_timer.get_elapsed_milliseconds();
        }

        bool timed_out() const
        {
            return m_timer.get_elapsed_milliseconds() > m_timeout_msecs;
        }

    private:
        const int DEFAULT_TIMEOUT = 60000;
        http_request m_request;
        http_response m_response;
        std::shared_ptr<connection> m_conn;
        std::string m_username;
        std::string m_client_ip;
        struct sockaddr_in m_client_addr;
        socklen_t m_client_addr_len;
        int m_timeout_msecs = DEFAULT_TIMEOUT;
        minerva::timer m_timer;
        std::function<bool()> m_sd_cb;
        std::optional<std::function<void()>> m_post_command;

    };
}
