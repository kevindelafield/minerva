#pragma once

#include <unordered_map>
#include <istream>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <atomic>
#include <ostream>
#include <deque>
#include <unordered_set>
#include <memory>
#include <owl/component.h>
#include <util/connection.h>
#include <util/time_utils.h>

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
        void stop() override;
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
        
        // Caller retains ownership of `db`; it must outlive this httpd
        // instance. Pass nullptr to disable auth.
        void auth_db(http_auth_db * db)
        {
            std::unique_lock<std::mutex> lk(lock);
            m_auth_db = db;
        }

        void get_cgi_log(std::ostream & db);

    private:
        class http_listener
        {
        public:
            http_listener() = default;

            http_listener(int p, PROTOCOL proto) : port(p), protocol(proto)
            {
            }

            PROTOCOL                    protocol = PROTOCOL::HTTP;
            int                         port     = 80;
            std::shared_ptr<connection> conn;
        };

        void start_listeners();

        thread_pool * handler_thread_pool;
        std::unordered_map<std::string, controller*> controller_map;
        // Guards controller_map and m_default_controller. controller_map is
        // populated only at initialization; the dispatch path takes a
        // shared_lock while callbacks hold the unique_lock.
        std::shared_mutex m_controller_lock;
        controller* m_default_controller = nullptr;
        std::atomic<unsigned long long> m_active_count;
        std::atomic<unsigned long long> m_request_count;
        // Non-owning. Owned by the caller of auth_db().
        http_auth_db * m_auth_db = nullptr;
        http_auth_nonce_store m_nonce_store;

        controller * get_default_controller();
        http_auth_db * get_auth_db();

        std::vector<http_listener> m_listeners;
        std::map<int, http_listener> m_listener_sockets;
        std::mutex m_listener_lock;
        std::condition_variable m_listener_cond;
        std::atomic<bool> m_hup{false};
        std::atomic<bool> m_waiting_hup{false};

        std::mutex m_log_lock;
        std::deque<std::string> m_cgi_log;
    
        // Map of currently-idle keep-alive connections, keyed by the
        // owning shared_ptr. Using shared_ptr (rather than a raw pointer
        // or fd) keeps the connection alive while the entry is in the
        // map and avoids fd-recycle races.
        std::map<std::shared_ptr<connection>,
                 std::tuple<std::shared_ptr<connection>,
                            struct sockaddr_storage,
                            socklen_t>> m_socket_map;

        void log(http_context & ctx, const std::string & date);

        // Factory method for creating connections with proper smart pointer management
        std::shared_ptr<connection> create_connection(int socket, PROTOCOL protocol);
        std::shared_ptr<connection> create_listener_connection(PROTOCOL protocol);

        bool accept(std::shared_ptr<connection> conn);
        
        bool shutdown(std::shared_ptr<connection> conn);

        void shutdown_write_async(std::shared_ptr<connection> conn);
        
        void put_back_connection(std::shared_ptr<connection> conn,
                                 const sockaddr_storage & addr, 
                                 socklen_t addr_len);

        void handle_request(std::shared_ptr<connection> conn, 
                            const struct sockaddr_storage & addr, 
                            socklen_t addr_len);
    
        // Set ctx.response() status to `code`, draining the request body
        // if necessary. Returns false if the body could not be drained
        // (caller should set abrt = true).
        bool finalize_error_response(http_context & ctx,
                                     http_response::http_response_code code);

        bool write_100_continue_header(http_context & ctx);

        bool authenticate(http_context & ctx, std::string & user);
    
        void listener_thread_fn();

        void persist_thread_fn();
    };
}
