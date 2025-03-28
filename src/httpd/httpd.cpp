#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <thread>
#include <sstream>
#include <istream>
#include <mutex>
#include <algorithm>
#include <util/log.h>
#include <util/string_utils.h>
#include <util/ssl_connection.h>
#include <owl/locks.h>
#include "httpd.h"
#include "http_auth.h"
#include "http_context.h"
#include "http_request.h"
#include "http_response.h"
#include "http_content_type.h"
#include "controller.h"

namespace minerva
{

    httpd::httpd() : m_active_count(0),
                     m_request_count(0)
    {
    }

    void httpd::register_controller(const std::string & path, 
                                    controller * controller)
    {
        LOG_DEBUG("Registering controller " << path.c_str());
        std::unique_lock<std::mutex> lk(lock);
        controller_map[path] = controller;
    }

    void httpd::register_default_controller(controller * controller)
    {
        LOG_DEBUG("Registering default controller");
        std::unique_lock<std::mutex> lk(lock);
        m_default_controller = controller;
    }

    void httpd::clear_listeners()
    {
        std::unique_lock<std::mutex> lk(m_listener_lock);

        m_listeners.clear();
    }

    void httpd::add_listener(httpd::PROTOCOL protocol, int port)
    {
        std::unique_lock<std::mutex> lk(m_listener_lock);

        http_listener listener(port, protocol);

        m_listeners.push_back(listener);
    }

    void httpd::initialize()
    {
        // create handler thread pool
        handler_thread_pool = add_thread_pool(handler_count);

        // create listener thread
        add_thread(std::bind(&httpd::listener_thread_fn, this));

        // create keep alive thread
        add_thread(std::bind(&httpd::persist_thread_fn, this));
    }

    void httpd::start_listeners()
    {
        for (auto listener : m_listener_sockets)
        {
            listener.second.conn->shutdown_write();
            listener.second.conn->shutdown_read();
            delete listener.second.conn;
        }
        m_listener_sockets.clear();

        for (auto & listener : m_listeners)
        {
            connection * conn;
            if (listener.protocol == httpd::PROTOCOL::HTTP)
            {
                conn =
                    new connection(AF_INET6,
                                         SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            }
            else if (listener.protocol == httpd::PROTOCOL::HTTPS)
            {
                conn = static_cast<connection *>(new ssl_connection(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
            }
            else
            {
                LOG_ERROR("invalid http protocol: " << listener.protocol);
                continue;
            }

            assert(conn);

            if (!conn->reuse_addr(true))
            {
                FATAL_ERRNO("Failed to reuse address", errno);
            }
            if (!conn->reuse_addr6(true))
            {
                FATAL_ERRNO("Failed to reuse address 6", errno);
            }
            if (!conn->ipv6_only(false))
            {
                FATAL_ERRNO("Failed to set ipv6 only", errno);
            }


            static struct in6_addr any6addr = IN6ADDR_ANY_INIT;

            struct sockaddr_in6 addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin6_family = AF_INET6;
            addr.sin6_addr = any6addr;
            addr.sin6_port = htons(listener.port);
    
            if (!conn->bind((const sockaddr *)&addr, sizeof(addr)))
            {
                FATAL_ERRNO("Failed to bind to port " << listener.port, errno);
            }
            if (!conn->listen(max_queued_connections))
            {
                FATAL_ERRNO("Listen failed for port " << listener.port, errno);
            }

            listener.conn = conn;

            m_listener_sockets[conn->get_socket()] = listener;

            LOG_INFO("HTTPD listening on socket " << listener.port);
        }
    }

    void httpd::start()
    {
        component::start();

        start_listeners();
    }

    void httpd::release()
    {
        // close listeners
        for (auto listener : m_listener_sockets)
        {
            listener.second.conn->shutdown_write();
            listener.second.conn->shutdown_read();
            delete listener.second.conn;
        }
        m_listener_sockets.clear();

        // close open sockets
        for (auto it = m_socket_map.begin();
             it != m_socket_map.end();
             it++)
        {
            auto conn = std::get<0>(it->second);
            conn->shutdown();
            conn->shutdown_write();
            conn->shutdown_read();
            delete conn;
        }
        m_socket_map.clear();

        component::release();
    }

    void httpd::hup()
    {
        std::unique_lock<std::mutex> lk(m_listener_lock);

        m_hup = true;

        while (!m_waiting_hup)
        {
            m_listener_cond.wait(lk);
        }

        start_listeners();

        m_hup = false;

        m_listener_cond.notify_all();

        if (!m_auth_db->initialize())
        {
            LOG_ERROR("failed to reload HTTPD auth db");
        }
    }

    void httpd::shutdown_write_async(connection * conn)
    {
        schedule_job([this, conn]()
                     {
                         shutdown(conn);
                         conn->shutdown_write();
                         delete conn;
                     }, 0);
    }

    bool httpd::shutdown(connection * conn)
    {
        timer timer(true);

        while (!should_shutdown() && timer.get_elapsed_milliseconds() < 15000)
        {
            bool reading = false;

            auto status = conn->shutdown();
            switch (status)
            {
            case connection::CONNECTION_STATUS::CONNECTION_OK:
            {
                return true;
            }
            break;
            case connection::CONNECTION_STATUS::CONNECTION_WANTS_READ:
            {
                reading = true;
            }
            break;
            case connection::CONNECTION_STATUS::CONNECTION_WANTS_WRITE:
            {
                reading = false;
            }
            break;
            case connection::CONNECTION_STATUS::CONNECTION_CLOSED:
            {
                return false;
            }
            break;
            case connection::CONNECTION_STATUS::CONNECTION_ERROR:
            {
                LOG_DEBUG("failed to terminate connection");
                return false;
            }
            break;
            }

            bool read_flag = reading;
            bool write_flag = !reading;
            bool error_flag = true;

            int err = 0;
            while (err == 0 && !should_shutdown() && 
                   timer.get_elapsed_milliseconds() < 15000)
            {
                err = conn->poll(read_flag, write_flag, error_flag, 100);
            }
            if (err < 0)
            {
                LOG_DEBUG_ERRNO("poll during terminate failed", errno);
                return false;
            }
        }
        LOG_DEBUG("timeout attempting to terminate connection");
        return false;
    }

    void httpd::persist_thread_fn()
    {
        while (!should_shutdown())
        {
            std::map<int, std::tuple<connection *, struct sockaddr_in, socklen_t>> map;
            std::vector<connection::shared_poll_fd> fds;
            std::vector<connection *> to_close;
            
            // wait for sockets or a shutdown
            {
                std::unique_lock<std::mutex> lk(lock);
                while (!should_shutdown() && m_socket_map.size() == 0)
                {
                    cond.wait(lk);
                }
                if (should_shutdown())
                {
                    return;
                }

                // timeout anything older than 90 seconds

                std::vector<int> to_erase;

                for (auto it = m_socket_map.begin();
                     it != m_socket_map.end();
                     it++)
                {
                    auto conn = std::get<0>(it->second);

                    if (std::chrono::steady_clock::now() - conn->last_read >
                        std::chrono::seconds(90))
                    {
                        LOG_DEBUG("shutting down idle connection: " <<
                                  conn->get_socket());
                        
                        to_close.emplace_back(conn);
                        
                        to_erase.emplace_back(it->first);
                    }
                }

                for (auto s : to_erase)
                {
                    m_socket_map.erase(s);
                }

                map = m_socket_map;
            }

            std::for_each(map.begin(), map.end(),
                          [&fds](std::pair<int, std::tuple<connection *, struct sockaddr_in, socklen_t>> item)
                          {
                              fds.push_back(connection::shared_poll_fd(item.first,
                                                                       true,
                                                                       false,
                                                                       true));
                          });

            int err;
            int status = connection::poll(fds, 10, err);
            if (status == 0)
            {
                continue;
            }
            else if (status < 0)
            {
                if (err == EINTR)
                {
                    continue;
                }
                LOG_ERROR_ERRNO("Poll failed", err);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            
            {
                std::unique_lock<std::mutex> lk(lock);
                
                // handle any poll notices
                for (auto & it : fds)
                {
                    if (it.error)
                    {
                        auto c = map[it.socket];
                        
                        to_close.push_back(std::get<0>(c));
                        
                        m_socket_map.erase(std::get<0>(c)->get_socket());
                        
                    }
                    else if (it.read)
                    {
                        auto to_send = map[it.socket];
                        
                        auto socket = std::get<0>(to_send);

                        m_socket_map.erase(socket->get_socket());
                        
                        bool available;
                        bool success = 
                            socket->data_available(available);
                        if (success && available)
                        {
                            // queue up request
                            LOG_DEBUG("dispatching keep alive connection: " <<
                                      socket->get_socket());
                        
                            handler_thread_pool->queue_work_item([this, to_send] () {
                                    struct sockaddr_in addr = std::get<1>(to_send);
                                    
                                    this->handle_request(std::get<0>(to_send),
                                                         addr,
                                                         std::get<2>(to_send));
                                });
                        }
                        else
                        {
                            to_close.push_back(socket);
                        }
                    }
                }
            }
            schedule_job([to_close]()
                         {
                             for (auto it : to_close)
                             {
                                 it->shutdown();
                                 it->shutdown_write();
                                 it->shutdown_read();
                                 delete it;
                             }
                         }, 0);
            // shutdown connections
        }
    }
    
    void httpd::put_back_connection(connection * conn, 
                                    const sockaddr_in & addr, 
                                    socklen_t addr_len)
    {
        LOG_DEBUG("put back: " << conn->get_socket());

        std::unique_lock<std::mutex> lk(lock);
        m_socket_map[conn->get_socket()] = 
            std::make_tuple(conn, addr, addr_len);
        cond.notify_all();
    }

    void httpd::listener_thread_fn()
    {
        LOG_DEBUG("listening for http(s) connections");

        while (!should_shutdown())
        {
            std::vector<connection::shared_poll_fd> fds;

            {
                std::unique_lock<std::mutex> lk(m_listener_lock);

                while (m_hup)
                {
                    m_waiting_hup = true;
                    m_listener_cond.notify_all();
                    m_listener_cond.wait(lk);
                }

                m_waiting_hup = false;
            }

            for (auto & socket : m_listener_sockets)
            {
                fds.push_back(connection::shared_poll_fd(socket.first,
                                                         true,
                                                         false,
                                                         true));
            }
            
            int err;
            int status = connection::poll(fds, polling_period_ms, err);
            if (status == 0)
            {
                continue;
            }
            else if (status < 0)
            {
                if (err == EINTR)
                {
                    continue;
                }
                LOG_ERROR_ERRNO("Poll failed", err);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            for (auto & it : fds)
            {
                if (it.error)
                {
                    LOG_WARN("error on listener thread");
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
                
                if (!it.read)
                {
                    continue;
                }

                auto listener = m_listener_sockets[it.socket].conn;

                bool http =
                    m_listener_sockets[it.socket].protocol ==
                    PROTOCOL::HTTP;

                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);

                int s;
                if (!listener->accept(addr, addr_len, 
                                      SOCK_CLOEXEC | SOCK_NONBLOCK, s))
                {
                    if (errno == EAGAIN)
                    {
                        LOG_DEBUG("Accept EAGAIN");
                        continue;
                    }
                    else
                    {
                        LOG_DEBUG_ERRNO("Accept error", errno);
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        continue;
                    }
                }

                LOG_DEBUG("Accept Successful: " << http);
                
                // queue up request
                connection * conn = http ?
                    new connection(s) :
                    static_cast<connection *>(new ssl_connection(s));
                assert(conn);
                
                schedule_job([this, conn, addr, addr_len]()
                             {
                                 if (!accept(conn))
                                 {
                                     LOG_DEBUG("failed to establish tls session");
                                     delete conn;
                                 }
                                 else
                                 {
                                     // queue up request
                                     handler_thread_pool->queue_work_item([this, conn, addr, addr_len] () {
                                             this->handle_request(conn, addr, addr_len);
                                         });
                                 }
                             }, 0);
            }
        }

        
    }

    bool httpd::accept(connection * conn)
    {
        timer timer(true);
        bool accepted = false;
        bool done = false;
        while (!done &&
               !should_shutdown() &&
               timer.get_elapsed_milliseconds() < 20000)
        {
            LOG_DEBUG("ssl accepting...");
            auto ssl_status = conn->accept_ssl();
            switch (ssl_status)
            {
            case connection::CONNECTION_STATUS::CONNECTION_OK:
            {
                accepted = true;
                done = true;
                break;
            }
            break;
            case connection::CONNECTION_STATUS::CONNECTION_CLOSED:
            {
                LOG_DEBUG("connection closed during tls negotiation");
                done = true;
                break;
            }
            case connection::CONNECTION_STATUS::CONNECTION_ERROR:
            {
                LOG_DEBUG("error during tls negotiation");
                done = true;
                break;
            }
            case connection::CONNECTION_STATUS::CONNECTION_WANTS_READ:
            case connection::CONNECTION_STATUS::CONNECTION_WANTS_WRITE:
            {
                bool read = ssl_status == connection::CONNECTION_STATUS::CONNECTION_WANTS_READ;
                bool write = !read;
                bool error = true;

                LOG_DEBUG("wants read: " << read);

                int status = conn->poll(read, write, error, 100);
                if (status < 0)
                {
                    LOG_DEBUG("poll failed during tls negotiation");
                    done = true;
                    break;
                }
                if (status == 0)
                {
                    // retry on timeouts - seems to work better
                    continue;
                }
                if (error)
                {
                    LOG_DEBUG("socket error during tls negotiation");
                    done = true;
                    break;
                }
            }
            break;
            }
        }
        if (accepted)
        {
            LOG_DEBUG("accepted");
        }
        return accepted;
    }

    bool httpd::authenticate(http_context & ctx, std::string & user)
    {
        if (!m_auth_db)
        {
            return true;
        }

        // get auth header
        std::string auth_header;
        ctx.request().header(AUTH_HDR, auth_header);

        // basic header check
        bool success =
            authenticate_basic(ctx,
                               auth_header,
                               m_auth_db->realm(),
                               *m_auth_db,
                               user);

        // digest header check
        if (!success)
        {
            success = 
                authenticate_digest(ctx,
                                    auth_header,
                                    m_auth_db->realm(),
                                    *m_auth_db,
                                    user);
        }
        
        if (success)
        {
            LOG_DEBUG("authenticated request");
        }
        else
        {
            LOG_DEBUG("request authentication failed");
        }

        return success;
    }

    constexpr static size_t BUFFER_SIZE = 100*1024;

    void httpd::handle_request(connection * conn,
                               const struct sockaddr_in & addr, 
                               socklen_t addr_len)
    {
        LOG_DEBUG("Handling http request");

        m_active_count++;

        std::string client_ip;
        char * name = ::inet_ntoa(addr.sin_addr);
        if (name)
        {
            client_ip = name;
        }

        // allocate send and receive buffers on the stack

        http_context ctx(conn, [this]() {
            return should_shutdown();
        });

        ctx.client_ip(client_ip);
        ctx.client_addr(addr, addr_len);

        std::string date;

        // add date header
        {
            time_t tt;
            
            time(&tt);
            
            struct tm tm;
            
            gmtime_r(&tt, &tm);
            
            char tmp_date[200];
            
            strftime(tmp_date, 199, "%a, %b %d %Y %H:%M:%S GMT", &tm);
            tmp_date[199] = 0;

            date = tmp_date;

            ctx.response().add_header("Date", date);
        }

        bool abrt = false;
        char* first = nullptr;

        std::vector<char> buf;
        buf.reserve(BUFFER_SIZE);

        bool reading = true;

        while (true)
        {
            ssize_t remaining = BUFFER_SIZE - buf.size();
            if (remaining <= 0)
            {
                abrt = true;
                LOG_WARN("Http request overflow");
                break;
            }

            // check for shutdown
            if (should_shutdown())
            {
                abrt = true;
                break;
            }

            // check for aggregate timeout
            if (ctx.timed_out())
            {
                LOG_WARN("Receive timeout");
                abrt = true;
                break;
            }

            ssize_t read;

            bool read_flag = reading;
            bool write_flag = !reading;
            bool error_flag = true;
            int poll_status = 
                conn->poll(read_flag, write_flag, error_flag, 100);

            if (poll_status < 0)
            {
                LOG_WARN_ERRNO("Poll error", errno);
                abrt = true;
                break;
            }
            else if (poll_status == 0)
            {
                // timeout
                continue;
            }
            else if (error_flag)
            {
                LOG_WARN("Poll read socket error");
                abrt = true;
                break;
            }
            else
            {
                char tmpbuf[10*1024];

                auto status = 
                    conn->read(tmpbuf,
                               std::min(static_cast<unsigned long>(remaining),
                                        sizeof(tmpbuf)), read);
                switch (status)
                {
                case connection::CONNECTION_ERROR:
                {
                    abrt = true;
                    LOG_WARN_ERRNO("Http client timeout or socket read error",
                                   errno);
                }
                break;
                case connection::CONNECTION_CLOSED:
                {
                    abrt = true;
                    // don't log warnings for close after keep-alive
                    LOG_WARN("Http client disconnected unexpectedly");
                }
                break;
                case connection::CONNECTION_OK:
                {
                    if (read == 0)
                    {
                        abrt = true;
                        // don't log warnings for close after keep-alive
                        LOG_WARN("Http client disconnected unexpectedly");
                    }
                    else
                    {
                        buf.insert(buf.end(), tmpbuf, tmpbuf+read);
                        abrt = false;
                    }
                }
                break;
                case connection::CONNECTION_WANTS_READ:
                {
                    reading = true;
                    abrt = false;
                }
                break;
                case connection::CONNECTION_WANTS_WRITE:
                {
                    reading = false;
                    abrt = false;
                }
                break;
                }
            }

            if (abrt)
            {
                break;
            }

            // header not found yet
            if (first == nullptr)
            {
                // handle case where haven't received the full header and
                // we can't null terminate because the buffer is full
                if (buf.size() >= BUFFER_SIZE)
                {
                    LOG_WARN("Http request overflow.");
                    abrt = true;
                    break;
                }

                // Process received data
                buf.push_back(0);
                first = std::strstr(&buf[0], "\r\n\r\n");
                buf.pop_back();
                if (nullptr == first)
                {
                    // Still haven't received headers
                    continue;
                }
                first = first + 4; // Skip past headers to body

                size_t header_length = first - &buf[0];

                LOG_DEBUG("Found http request header");

                // parse the header and prep the request stream
                if (!ctx.request().parse_header(buf, header_length))
                {
                    LOG_WARN("Error parsing http request header");
                    abrt = true;
                    break;
                }

                // continue
                abrt = false;
                break;
            }
        }

        // Only respond if the connection was not aborted
        if (!abrt)
        {
            ctx.response().is_http11(ctx.request().is_http11());
            
            LOG_DEBUG("Request url: "<< ctx.request().path().c_str());

            // find the root path
            std::stringstream ss(ctx.request().path());
            std::string root;
            controller * controller = nullptr;

            if (controller::next_path_segment(ss, root))
            {
                // find the controller
                LOG_DEBUG("Looking for controller " << root);
                std::unique_lock<std::mutex> lk(lock);
                auto it = controller_map.find(root);
                if (it != controller_map.end())
                {
                    controller = it->second;
                }
            }

            // no controller found - try default controller
            if (!controller)
            {
                controller = m_default_controller;
            }

            if (!controller)
            {
                std::string user;
                if (!authenticate(ctx, user))
                {
                    if (ctx.request().continue_100() && 
                        !ctx.request().has_overflow())
                    {
                        ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED);
                    }
                    else
                    {
                        if (ctx.request().null_body_read())
                        {
                            ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED);
                        }
                        else
                        {
                            abrt = true;
                            LOG_WARN("Failed to read body");
                        }
                    }
                }
                else
                {
                    if (ctx.request().continue_100() &&
                        !ctx.request().has_overflow())
                    {
                        ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_NOT_FOUND);
                    }
                    else
                    {
                        // controller not found
                        LOG_DEBUG("Failed to find controller");
                        if (ctx.request().null_body_read())
                        {
                            ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_NOT_FOUND);
                        }
                        else
                        {
                            abrt = true;
                            LOG_WARN("Failed to read body");
                        }
                    }
                }
            }
            
            std::string operation;
            controller::next_path_segment(ss, operation);

            // process request
            if (controller)
            {
                std::string user;

                if (controller->require_authorization() && 
                    (!authenticate(ctx, user) || 
                     !controller->auth_callback(user, operation)))
                {
                    LOG_DEBUG("Authorization for HTTP request failed: " << 
                                 user << " " <<
                                 ctx.request().method_as_string() << " " <<
                                 ctx.request().path());
                    if (ctx.request().continue_100() &&
                        !ctx.request().has_overflow())
                    {
                        ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED);
                    }
                    else
                    {
                        if (ctx.request().null_body_read())
                        {
                            ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED);
                        }
                        else
                        {
                            abrt = true;
                            LOG_WARN("Failed to read body");
                        }
                    }
                }
                else
                {
                    ctx.username(user);
                    if (ctx.request().continue_100() && 
                        !ctx.request().has_overflow())
                    {
                        if (!write_100_continue_header(ctx))
                        {
                            LOG_WARN("failed to write 100 continue header");
                            abrt = true;
                        }
                    }
                    if (!abrt)
                    {
                        LOG_DEBUG("Found rest controller - executing");

                        LOG_INFO("Handling HTTP request: " << 
                                 user << " " <<
                                 ctx.request().method_as_string() << " " <<
                                 ctx.request().path());
                        // execute request handler
                        try
                        {
                            controller->handle_request(ctx, operation);
                            if (!ctx.request().null_body_read())
                            {
                                abrt = true;
                                LOG_WARN("failed to read full body");
                            }
                        }
                        catch (http_exception & e)
                        {
                            LOG_ERROR("HTTP exception: " << e.what());
                            abrt = true;
                        }
                        catch (std::exception & e)
                        {
                            LOG_ERROR("std exception: " << e.what());
                            if (ctx.request().null_body_read())
                            {
                                ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_INT_SERVER_ERR);
                            }
                            else
                            {
                                LOG_WARN("failed to read full body");
                                abrt = true;
                            }
                        }
                    }
                }
            }
        
            if (!abrt)
            {
                // send application response
                if (ctx.response().chunked())
                {
                    if (!ctx.response().flush_final_chunk())
                    {
                        LOG_WARN("failed to flush final chunk to HTTP client");
                        abrt = true;
                    }
                    else if (ctx.request().keep_alive())
                    {
                        put_back_connection(conn, addr, addr_len);
                    }
                    else
                    {
                        shutdown_write_async(conn);
                    }
                }
                else
                {
                    // send header
                    if (!ctx.response().write_header())
                    {
                        LOG_WARN("Error writing http response header");
                        abrt = true;
                    }
                    else
                    {
                        std::istream & is = ctx.response().response_stream();
                        is.seekg(0, is.end);
                        LOG_DEBUG("Response length: " << is.tellg());
                        if (is.tellg() > 0)
                        {
                            if (!ctx.response().send_buffer(is))
                            {
                                LOG_WARN("Failed to send data to HTTP Client");
                                abrt = true;
                            }
                            else // full response sent - shut write
                            {
                                log(ctx, date);

                                LOG_DEBUG("handled request");
                                if (ctx.request().keep_alive())
                                {
                                    put_back_connection(conn, addr, addr_len);
                                }
                                else
                                {
                                    shutdown_write_async(conn);
                                }
                            }
                        }
                        else // no response to send - shut write
                        {
                            log(ctx, date);

                            LOG_DEBUG("handled request");
                            if (ctx.request().keep_alive())
                            {
                                put_back_connection(conn, addr, addr_len);
                            }
                            else
                            {
                                shutdown_write_async(conn);
                            }
                        }
                    }
                }

                if (ctx.post_command().has_value())
                {
                    ctx.post_command().value()();
                }
            }
        }

        if (abrt)
        {
            // request was aborted - full shutdown
            LOG_WARN("aborting HTTP connection");
            conn->shutdown_write();
            conn->shutdown_read();
            delete conn;
        }

        m_active_count--;
        m_request_count++;
    }

    void httpd::log(http_context & ctx, const std::string & date)
    {
        std::stringstream ss;
        ss << 
            ctx.response().status_code() << " " <<
            ctx.request().method_as_string() << " " <<
            ctx.request().path();
        if (ctx.request().query_string().empty())
        {
            ss << " ";
        }
        else
        {
            ss << "?" << ctx.request().query_string() << " ";
        }
        ss << "duration=" << ctx.get_elapsed_milliseconds() << "(ms) ";
        ss << date;
    
        std::unique_lock<std::mutex> lk(m_log_lock);

        m_cgi_log.push_back(ss.str());
        if (m_cgi_log.size() > 1000)
        {
            m_cgi_log.pop_front();
        }

    }

    void httpd::get_cgi_log(std::ostream & os)
    {
        std::unique_lock<std::mutex> lk(m_log_lock);
        for (auto & i : m_cgi_log)
        {
            os << i << std::endl;
        }
    }

    bool httpd::write_100_continue_header(http_context & ctx)
    {
        std::stringstream os;
        // write header
        if (ctx.response().is_http11())
        {
            os << "HTTP/1.1 ";
        }
        else
        {
            os << "HTTP/1.0 ";
        }
        os << "100 continue";
        os << CRLF;
        os << CRLF;
        os.flush();
        // check for write failures
        if (os.fail())
        {
            return false;
        }
        return ctx.response().send_buffer(os);
    }
}
