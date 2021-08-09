#pragma once

#include <unordered_map>
#include <istream>
#include <memory>
#include <mutex>
#include <set>
#include <atomic>
#include <ostream>
#include <deque>
#include "component.h"
#include "http_request.h"
#include "http_response.h"
#include "http_auth.h"
#include "connection.h"
#include "time_utils.h"
#include "nillable.h"

namespace owl
{

    class component;

    class controller;

    class httpd : public component
    {
    public:
        httpd();
        virtual ~httpd() = default;
        
        constexpr static const char * CRLF = "\r\n";
    
        constexpr static char DIGEST_REALM[] = "Alarm.com";

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
    
        Json::Value get_stats() override;
    
        void register_controller(const std::string & path, 
                                 controller * controller);

        void register_default_controller(controller * controller);
        
        void realm(const std::string & realm)
        {
            std::unique_lock<std::mutex> lk(lock);
            m_realm = realm;
        }

        std::string realm()
        {
            std::unique_lock<std::mutex> lk(lock);
            return m_realm;
        }

        int http_port() const
        {
            return http_port_number;
        }

        int https_port() const
        {
            return https_port_number;
        }

        bool update_http_port(int port);

        bool update_https_port(int port);

        void auth_db(http_auth_db * db)
        {
            m_auth_db = db;
        }

        void get_cgi_log(std::ostream & db);

    private:
        std::shared_ptr<thread_pool> handler_thread_pool;
        std::unordered_map<std::string, controller*> controller_map;
        controller* m_default_controller = nullptr;
        std::string m_realm;
        int m_new_http_port = -1;
        int m_new_https_port = -1;
        int http_port_number = 80;
        int https_port_number = 443;
        std::atomic<unsigned long long> m_active_count;
        std::atomic<unsigned long long> m_request_count;
        http_auth_db * m_auth_db = nullptr;

        std::mutex m_log_lock;
        std::deque<std::string> m_cgi_log;
    
        std::shared_ptr<connection> http_listener;
        std::shared_ptr<connection> https_listener;
    
        std::map<int, std::tuple<std::shared_ptr<connection>, struct sockaddr_in, socklen_t>> m_socket_map;

        void log(http_context & ctx, const std::string & date);

        void handle_http_port_update();
        
        void handle_https_port_update();

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