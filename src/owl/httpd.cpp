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
#include "httpd.h"
#include "http_auth.h"
#include "http_context.h"
#include "http_request.h"
#include "http_response.h"
#include "http_content_type.h"
#include "controller.h"
#include "log.h"
#include "locks.h"
#include "string_utils.h"
#include "ssl_connection.h"

namespace owl
{

    httpd::httpd() : http_listener(std::make_shared<connection>(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)),
                     https_listener(std::dynamic_pointer_cast<connection>(std::make_shared<ssl_connection>(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0))),
                     m_realm(DIGEST_REALM),
                     m_active_count(0),
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

    void httpd::initialize()
    {
        Json::Value port_value;
        if (!get_setting("http.port", port_value))
        {
            set_setting("http.port", Json::Value(http_port_number));
        }
        else
        {
            http_port_number = port_value.asInt();
        }
        if (!get_setting("https.port", port_value))
        {
            set_setting("https.port", Json::Value(https_port_number));
        }
        else
        {
            https_port_number = port_value.asInt();
        }

        // create handler thread pool
        handler_thread_pool = add_thread_pool(handler_count);

        // create listener thread
        add_thread(std::bind(&httpd::listener_thread_fn, this));

        // create keep alive thread
        add_thread(std::bind(&httpd::persist_thread_fn, this));
    }

    void httpd::start()
    {
        Json::Value http_port;
        Json::Value https_port;
        if (!get_setting("http.port", http_port))
        {
            FATAL("failed to get http.port setting");
        }
        if (!get_setting("https.port", https_port))
        {
            FATAL("failed to get http.port setting");
        }
        http_port_number = http_port.asInt();
        https_port_number = https_port.asInt();

        LOG_INFO("start listening on http: " << http_port_number);
        LOG_INFO("start listening on https: " << https_port_number);

        if (http_listener->reuse_addr(true))
        {
            FATAL("Failed to reuse address");
        }
        if (http_listener->bind(http_port_number))
        {
            FATAL("Failed to bind to port");
        }
        if (http_listener->listen(max_queued_connections))
        {
            FATAL("Listen failed");
        }

        if (https_listener->reuse_addr(true))
        {
            FATAL("Failed to reuse address");
        }
        if (https_listener->bind(https_port_number))
        {
            FATAL("Failed to bind to port");
        }
        if (https_listener->listen(max_queued_connections))
        {
            FATAL("Listen failed");
        }

        LOG_DEBUG("Httpd listening on socket " << http_port_number);
        LOG_DEBUG("Httpd listening on socket " << https_port_number);

    }

    void httpd::release()
    {
    }

    void httpd::hup()
    {
        update_http_port(http_port_number);
        update_https_port(https_port_number);
    }

    bool httpd::update_http_port(int port)
    {
        LOG_INFO("updating http port: " << port);

        std::unique_lock<std::mutex> lk(lock);

        // update settings
        set_setting("http.port", Json::Value(port));
        if (!save_settings())
        {
            LOG_ERROR("failed to save http.port setting");
            return false;
        }

        m_new_http_port = port;
        while (!should_shutdown() && m_new_http_port != -1)
        {
            cond.wait(lk);
        }
        if (should_shutdown())
        {
            return false;
        }
        return true;
    }

    bool httpd::update_https_port(int port)
    {
        LOG_INFO("updating https port: " << port);

        std::unique_lock<std::mutex> lk(lock);

        // update settings
        set_setting("https.port", Json::Value(port));
        if (!save_settings())
        {
            LOG_ERROR("failed to save https.port setting");
            return false;
        }

        m_new_https_port = port;
        while (!should_shutdown() && m_new_https_port != -1)
        {
            cond.wait(lk);
        }
        if (should_shutdown())
        {
            return false;
        }
        return true;
    }

    Json::Value httpd::get_stats()
    {
        Json::Value v;

        std::unique_lock<std::mutex> lk(lock);

        v["idle"] = (Json::UInt64)m_socket_map.size();
        v["active"] = (Json::UInt64)m_active_count.load();
        v["requests"] = (Json::UInt64)m_request_count.load();

        return v;
    }
    
    void httpd::shutdown_write_async(std::shared_ptr<connection> conn)
    {
        schedule_job([this, conn]()
                     {
                         shutdown(conn);
                         conn->shutdown_write();
                     }, 0);
    }

    bool httpd::shutdown(std::shared_ptr<connection> conn)
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
                LOG_DEBUG_ERRNO("poll during terminate failed",
                                conn->get_last_error());
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
            std::map<int, std::tuple<std::shared_ptr<connection>, struct sockaddr_in, socklen_t>> map;
            std::vector<connection::shared_poll_fd> fds;
            std::vector<std::shared_ptr<connection>> to_close;
            
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
                        
                        to_close.push_back(conn);
                        
                        m_socket_map.erase(it);
                    }
                }
                map = m_socket_map;
            }

            std::for_each(map.begin(), map.end(),
                          [&fds](std::pair<int, std::tuple<std::shared_ptr<connection>, struct sockaddr_in, socklen_t>> item)
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
                             }
                         }, 0);
            // shutdown connections
        }
    }
    
    void httpd::put_back_connection(std::shared_ptr<connection> conn, 
                                    const sockaddr_in & addr, 
                                    socklen_t addr_len)
    {
        LOG_DEBUG("put back: " << conn->get_socket());

        std::unique_lock<std::mutex> lk(lock);
        m_socket_map[conn->get_socket()] = 
            std::make_tuple(conn, addr, addr_len);
        cond.notify_all();
    }

    void httpd::handle_http_port_update()
    {
        int new_port = m_new_http_port;
        
        if (new_port > -1)
        {
            LOG_INFO("binding to new http port: " << new_port);

            std::unique_lock<std::mutex> lk(lock);

            if (new_port == http_port_number)
            {
                LOG_INFO("same http port number - won't rebind: " << new_port);
                m_new_http_port = -1;
                cond.notify_all();
                return;
            }

            http_port_number = m_new_http_port;
            m_new_http_port = -1;
            
            http_listener =
                std::make_shared<connection>(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

            LOG_INFO("start listening on http: " << http_port_number);

            if (http_listener->reuse_addr(true))
            {
                FATAL("Failed to reuse address");
            }
            if (http_listener->bind(http_port_number))
            {
                FATAL("Failed to bind to port");
            }
            if (http_listener->listen(max_queued_connections))
            {
                FATAL("Listen failed");
            }
            
            cond.notify_all();
        }
    }

    void httpd::handle_https_port_update()
    {
        int new_port = m_new_https_port;
        
        if (new_port > -1)
        {
            LOG_INFO("binding to new https port: " << new_port);

            std::unique_lock<std::mutex> lk(lock);

            if (new_port == https_port_number)
            {
                LOG_INFO("same https port number - won't rebind: " << new_port);
                m_new_https_port = -1;
                cond.notify_all();
                return;
            }

            https_port_number = m_new_https_port;
            m_new_https_port = -1;
            
            https_listener =
                std::make_shared<connection>(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

            LOG_INFO("start listening on https: " << https_port_number);

            if (https_listener->reuse_addr(true))
            {
                FATAL("Failed to reuse address");
            }
            if (https_listener->bind(https_port_number))
            {
                FATAL("Failed to bind to port");
            }
            if (https_listener->listen(max_queued_connections))
            {
                FATAL("Listen failed");
            }
            
            cond.notify_all();
        }
    }

    void httpd::listener_thread_fn()
    {
        LOG_DEBUG("listening for http(s) connections");

        while (!should_shutdown())
        {
            // update ports if necessary
            handle_http_port_update();
            handle_https_port_update();

            
            std::vector<connection::shared_poll_fd> fds;
            fds.push_back(connection::shared_poll_fd(http_listener->get_socket(),
                                                     true,
                                                     false,
                                                     true));
            fds.push_back(connection::shared_poll_fd(https_listener->get_socket(),
                                                     true,
                                                     false,
                                                     true));

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

                bool http = it.socket == http_listener->get_socket();

                auto listener = http ? http_listener : https_listener;

                struct sockaddr_in addr;
                socklen_t addr_len = sizeof(addr);

                int s = listener->accept(addr, addr_len, 
                                         SOCK_CLOEXEC | SOCK_NONBLOCK);
                if (s < 0)
                {
                    int err = listener->get_last_error();
                    if (err == EAGAIN)
                    {
                        LOG_DEBUG("Accept EAGAIN");
                        continue;
                    }
                    else
                    {
                        LOG_DEBUG_ERRNO("Accept error", listener->get_last_error()); 
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        continue;
                    }
                }
                LOG_DEBUG("Accept Successful");
                
                // queue up request
                auto conn = http ?
                    std::make_shared<connection>(s) :
                    std::make_shared<ssl_connection>(s);
                assert(conn);
                
                schedule_job([this, conn, addr, addr_len]()
                             {
                                 if (!accept(conn))
                                 {
                                     LOG_DEBUG("failed to establish tls session");
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

    bool httpd::accept(std::shared_ptr<connection> conn)
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
                LOG_DEBUG_ERRNO("error during tls negotiation",
                                conn->get_last_error());
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

        // digest header check
        bool success = authenticate_digest(ctx,
                                           auth_header,
                                           realm(),
                                           *m_auth_db,
                                           user);
        // basic header check
        // if (!success)
        // {
        //     success = 
        //         authenticate_basic(ctx,
        //                            auth_header,
        //                            realm(),
        //                            *m_auth_db,
        //                            user);
        // }

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

    void httpd::handle_request(std::shared_ptr<connection> conn,
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
                LOG_WARN_ERRNO("Poll error", conn->get_last_error());
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
                                    conn->get_last_error());
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
