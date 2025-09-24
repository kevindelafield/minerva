#pragma once

#include <unordered_map>
#include <istream>
#include <mutex>
#include <set>
#include <atomic>
#include <ostream>
#include <deque>
#include <unordered_set>
#include <memory>
#include <owl/component.h>
#include <util/connection.h>
#include <util/time_utils.h>
#include <util/nillable.h>
#include "http_request.h"
#include "http_response.h"
#include "http_auth.h"

namespace minerva
{

    class component;

    class controller;

    class httpd : public component
    {
    public:

        enum PROTOCOL
        {
            HTTP,
            HTTPS
        };

        httpd();
        virtual ~httpd() = default;
        
        constexpr static const char * CRLF = "\r\n";
    
        const int handler_count = 5;
        const int max_queued_connections = 20;
        const int polling_period_ms = 500;
    
        void initialize() override;
        void start() override;
        void release() override;
        void hup() override;
    
        constexpr static char NAME[] = "HTTPD";
    
        std::string name() override
        {
            return NAME;
        };
    
        void clear_listeners();

        void add_listener(PROTOCOL protocol, int port);

        void register_controller(const std::string & path, 
                                 controller * controller);

        void register_default_controller(controller * controller);
        
        void auth_db(http_auth_db * db)
        {
            m_auth_db = db;
        }

        void get_cgi_log(std::ostream & db);

    private:
        class http_listener
        {
        public:
            http_listener()
            {
            }

            http_listener(int p, PROTOCOL proto) : port(p), protocol(proto)
            {
            }

            http_listener(const http_listener & other)
            {
                protocol = other.protocol;
                port = other.port;
                conn = other.conn;
            }

            http_listener & operator=(const http_listener & other)
            {
                if (&other != this)
                {
                    protocol = other.protocol;
                    port = other.port;
                    conn = other.conn;
                }
                return *this;
            }

            PROTOCOL protocol = PROTOCOL::HTTP;
            int port = 80;
            std::shared_ptr<connection> conn;
        };

        void start_listeners();

        thread_pool * handler_thread_pool;
        std::unordered_map<std::string, controller*> controller_map;
        controller* m_default_controller = nullptr;
        std::atomic<unsigned long long> m_active_count;
        std::atomic<unsigned long long> m_request_count;
        http_auth_db * m_auth_db = nullptr;

        std::vector<http_listener> m_listeners;
        std::map<int, http_listener> m_listener_sockets;
        std::mutex m_listener_lock;
        std::condition_variable m_listener_cond;
        std::atomic<bool> m_hup{false};
        std::atomic<bool> m_waiting_hup{false};

        std::mutex m_log_lock;
        std::deque<std::string> m_cgi_log;
    
        std::map<int, std::tuple<std::shared_ptr<connection>, struct sockaddr_in, socklen_t>> m_socket_map;

        void log(http_context & ctx, const std::string & date);

        // Factory method for creating connections with proper smart pointer management
        std::shared_ptr<connection> create_connection(int socket, PROTOCOL protocol);
        std::shared_ptr<connection> create_listener_connection(PROTOCOL protocol);

        bool accept(std::shared_ptr<connection> conn);
        
        bool shutdown(std::shared_ptr<connection> conn);

        void shutdown_write_async(std::shared_ptr<connection> conn);
        
        void put_back_connection(std::shared_ptr<connection> conn,
                                 const sockaddr_in & addr, 
                                 socklen_t addr_len);

        void handle_request(std::shared_ptr<connection> conn, 
                            const struct sockaddr_in & addr, 
                            socklen_t addr_len);
    
        bool write_100_continue_header(http_context & ctx);

        bool authenticate(http_context & ctx, std::string & user);
    
        void listener_thread_fn();

        void persist_thread_fn();
    };
}
