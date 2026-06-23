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

    std::shared_ptr<connection> httpd::create_connection(int socket, PROTOCOL protocol)
    {
        switch (protocol) {
            case PROTOCOL::HTTP:
                return std::make_shared<connection>(socket);
            case PROTOCOL::HTTPS:
                return std::make_shared<ssl_connection>(socket);
            default:
                LOG_ERROR("Invalid HTTP protocol: " << static_cast<int>(protocol));
                return nullptr;
        }
    }

    std::shared_ptr<connection> httpd::create_listener_connection(PROTOCOL protocol)
    {
        switch (protocol) {
            case PROTOCOL::HTTP:
                return std::make_shared<connection>(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            case PROTOCOL::HTTPS:
                return std::make_shared<ssl_connection>(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            default:
                LOG_ERROR("Invalid HTTP protocol: " << static_cast<int>(protocol));
                return nullptr;
        }
    }

    void httpd::register_controller(const std::string & path, 
                                    controller * controller)
    {
        LOG_DEBUG("Registering controller " << path.c_str());
        std::unique_lock<std::shared_mutex> lk(m_controller_lock);
        controller_map[path] = controller;
    }

    void httpd::register_default_controller(controller * controller)
    {
        LOG_DEBUG("Registering default controller");
        std::unique_lock<std::shared_mutex> lk(m_controller_lock);
        m_default_controller = controller;
    }

    controller * httpd::get_default_controller()
    {
        std::shared_lock<std::shared_mutex> lk(m_controller_lock);
        return m_default_controller;
    }

    http_auth_db * httpd::get_auth_db()
    {
        std::unique_lock<std::mutex> lk(lock);
        return m_auth_db;
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
        for (auto & listener : m_listener_sockets)
        {
            listener.second.conn->shutdown_write();
            listener.second.conn->shutdown_read();
            // Smart pointer automatically cleans up
        }
        m_listener_sockets.clear();

        for (auto & listener : m_listeners)
        {
            auto conn = create_listener_connection(listener.protocol);
            if (!conn)
            {
                LOG_ERROR("Failed to create listener connection for port "
                          << listener.port);
                continue;
            }

            if (!conn->reuse_addr(true))
            {
                LOG_ERROR_ERRNO("Failed to reuse address on port "
                                << listener.port, errno);
                continue;
            }
            if (!conn->reuse_addr6(true))
            {
                LOG_ERROR_ERRNO("Failed to reuse address 6 on port "
                                << listener.port, errno);
                continue;
            }
            if (!conn->ipv6_only(false))
            {
                LOG_ERROR_ERRNO("Failed to set ipv6 only on port "
                                << listener.port, errno);
                continue;
            }


            static struct in6_addr any6addr = IN6ADDR_ANY_INIT;

            struct sockaddr_in6 addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin6_family = AF_INET6;
            addr.sin6_addr = any6addr;
            addr.sin6_port = htons(listener.port);

            if (!conn->bind((const sockaddr *)&addr, sizeof(addr)))
            {
                LOG_ERROR_ERRNO("Failed to bind to port " << listener.port,
                                errno);
                continue;
            }
            if (!conn->listen(max_queued_connections))
            {
                LOG_ERROR_ERRNO("Listen failed for port " << listener.port,
                                errno);
                continue;
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

    void httpd::stop()
    {
        // Wake any thread waiting on hup so it observes shutdown.
        {
            std::unique_lock<std::mutex> lk(m_listener_lock);
            m_hup = false;
            m_listener_cond.notify_all();
        }

        // Close listener fds so the listener thread's poll/accept returns
        // immediately rather than waiting for the polling period to elapse.
        // Note: the connection objects themselves remain owned by
        // m_listener_sockets and will be released in release(); we only need
        // to half-close the descriptors here so the kernel signals POLLHUP /
        // returns ECONNABORTED on accept().
        {
            std::unique_lock<std::mutex> lk(m_listener_lock);
            for (auto & listener : m_listener_sockets)
            {
                listener.second.conn->shutdown_write();
                listener.second.conn->shutdown_read();
            }
        }

        // Wake persist thread (it sleeps on `cond` when m_socket_map is
        // empty).  component::stop() already notifies cond + the visor's
        // shutdown condvar; do it explicitly here too so this method is
        // safe to call directly.
        {
            std::unique_lock<std::mutex> lk(lock);
            cond.notify_all();
        }

        component::stop();
    }

    void httpd::release()
    {
        // At this point the component_visor has joined all threads added via
        // add_thread() (listener_thread_fn, persist_thread_fn) AND has
        // waited on the handler thread pool, so no other thread is touching
        // m_listener_sockets or m_socket_map.  We still take the locks for
        // defense in depth in case the shutdown ordering ever changes.
        {
            std::unique_lock<std::mutex> lk(m_listener_lock);
            for (auto & listener : m_listener_sockets)
            {
                listener.second.conn->shutdown_write();
                listener.second.conn->shutdown_read();
                // Smart pointer automatically cleans up
            }
            m_listener_sockets.clear();
        }

        {
            std::unique_lock<std::mutex> lk(lock);
            for (auto it = m_socket_map.begin();
                 it != m_socket_map.end();
                 it++)
            {
                auto conn = std::get<0>(it->second);
                conn->shutdown();
                conn->shutdown_write();
                conn->shutdown_read();
                // Smart pointer automatically cleans up
            }
            m_socket_map.clear();
        }

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

        if (auto * db = get_auth_db())
        {
            if (!db->initialize())
            {
                LOG_ERROR("failed to reload HTTPD auth db");
            }
        }
    }
    void httpd::shutdown_write_async(std::shared_ptr<connection> conn)
    {
        schedule_job([this, conn]()
                     {
                         shutdown(conn);
                         conn->shutdown_write();
                         // Smart pointer automatically manages cleanup
                     }, 0);
    }

    bool httpd::shutdown(std::shared_ptr<connection> conn)
    {
        timer t;

        while (!should_shutdown() && t.get_elapsed_milliseconds() < 15000)
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
                   t.get_elapsed_milliseconds() < 15000)
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
            // Local snapshot of the map keyed by connection identity. We
            // also build a parallel fd->shared_ptr lookup so the poll loop
            // can route revents back to the right entry without trusting
            // the (potentially recycled) fd value.
            std::map<std::shared_ptr<connection>,
                     std::tuple<std::shared_ptr<connection>,
                                struct sockaddr_storage,
                                socklen_t>> map;
            std::vector<connection::shared_poll_fd> fds;
            std::vector<std::shared_ptr<connection>> to_close;
            std::map<int, std::shared_ptr<connection>> fd_to_conn;

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

                std::vector<std::shared_ptr<connection>> to_erase;

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

                for (auto & key : to_erase)
                {
                    m_socket_map.erase(key);
                }

                map = m_socket_map;
            }

            for (auto & item : map)
            {
                int fd = item.first->get_socket();
                if (fd < 0)
                {
                    continue;
                }
                fd_to_conn[fd] = item.first;
                fds.push_back(connection::shared_poll_fd(fd, true,
                                                         false, true));
            }
            if (fds.empty())
            {
                continue;
            }

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
                    auto fd_it = fd_to_conn.find(it.socket);
                    if (fd_it == fd_to_conn.end())
                    {
                        continue;
                    }
                    std::shared_ptr<connection> key = fd_it->second;
                    auto map_it = map.find(key);
                    if (map_it == map.end())
                    {
                        continue;
                    }

                    if (it.error)
                    {
                        to_close.push_back(std::get<0>(map_it->second));
                        m_socket_map.erase(key);
                    }
                    else if (it.read)
                    {
                        auto to_send = map_it->second;
                        auto socket = std::get<0>(to_send);

                        m_socket_map.erase(key);

                        bool available;
                        bool success =
                            socket->data_available(available);
                        if (success && available)
                        {
                            // queue up request
                            LOG_DEBUG("dispatching keep alive connection: " <<
                                      socket->get_socket());

                            handler_thread_pool->queue_work_item([this, to_send] () {
                                    struct sockaddr_storage addr = std::get<1>(to_send);

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
                                 // Smart pointer automatically cleans up
                             }
                         }, 0);
            // shutdown connections
        }
    }
    
    void httpd::put_back_connection(std::shared_ptr<connection> conn,
                                    const sockaddr_storage & addr, 
                                    socklen_t addr_len)
    {
        LOG_DEBUG("put back: " << conn->get_socket());

        std::unique_lock<std::mutex> lk(lock);
        m_socket_map[conn] =
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

                struct sockaddr_storage addr;
                memset(&addr, 0, sizeof(addr));
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
                auto conn = create_connection(s, http ? PROTOCOL::HTTP : PROTOCOL::HTTPS);
                
                schedule_job([this, conn, addr, addr_len]()
                             {
                                 if (!accept(conn))
                                 {
                                     LOG_DEBUG("failed to establish tls session");
                                     // Smart pointer automatically cleans up
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
        timer t;
        bool accepted = false;
        bool done = false;
        while (!done &&
               !should_shutdown() &&
               t.get_elapsed_milliseconds() < 20000)
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
        http_auth_db * auth_db = get_auth_db();
        if (!auth_db)
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
                               auth_db->realm(),
                               *auth_db,
                               user);

        // digest header check
        if (!success)
        {
            success = 
                authenticate_digest(ctx,
                                    auth_header,
                                    auth_db->realm(),
                                    *auth_db,
                                    m_nonce_store,
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

    void httpd::handle_request(std::shared_ptr<connection> conn,
                               const struct sockaddr_storage & addr, 
                               socklen_t addr_len)
    {
        LOG_DEBUG("Handling http request");

        m_active_count++;

        std::string client_ip;
        char ip_buf[INET6_ADDRSTRLEN] = {0};
        const char * name = nullptr;
        if (addr.ss_family == AF_INET)
        {
            const auto * a = reinterpret_cast<const sockaddr_in *>(&addr);
            name = ::inet_ntop(AF_INET, &a->sin_addr,
                               ip_buf, sizeof(ip_buf));
        }
        else if (addr.ss_family == AF_INET6)
        {
            const auto * a = reinterpret_cast<const sockaddr_in6 *>(&addr);
            name = ::inet_ntop(AF_INET6, &a->sin6_addr,
                               ip_buf, sizeof(ip_buf));
        }
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
            struct tm tm = minerva::gmtime(tt);

            char tmp_date[200];
            strftime(tmp_date, sizeof(tmp_date),
                     "%a, %d %b %Y %H:%M:%S GMT", &tm);
            tmp_date[sizeof(tmp_date) - 1] = 0;

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

                // Length-aware search for end-of-headers (NUL-safe)
                static const char eoh[4] = { '\r', '\n', '\r', '\n' };
                auto eoh_it = std::search(buf.begin(), buf.end(),
                                          eoh, eoh + 4);
                if (eoh_it == buf.end())
                {
                    // Still haven't received headers
                    continue;
                }
                size_t header_length = (eoh_it - buf.begin()) + 4;
                first = &buf[0] + header_length;

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
                std::shared_lock<std::shared_mutex> lk(m_controller_lock);
                auto it = controller_map.find(root);
                if (it != controller_map.end())
                {
                    controller = it->second;
                }
            }

            // no controller found - try default controller
            if (!controller)
            {
                controller = get_default_controller();
            }

            if (!controller)
            {
                std::string user;
                auto code = authenticate(ctx, user)
                    ? http_response::http_response_code::HTTP_RETCODE_NOT_FOUND
                    : http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED;
                if (code == http_response::http_response_code::HTTP_RETCODE_NOT_FOUND)
                {
                    LOG_DEBUG("Failed to find controller");
                }
                if (!finalize_error_response(ctx, code))
                {
                    abrt = true;
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
                    if (!finalize_error_response(ctx, http_response::http_response_code::HTTP_RETCODE_UNAUTHORIZED))
                    {
                        abrt = true;
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
                            if (!finalize_error_response(ctx, http_response::http_response_code::HTTP_RETCODE_INT_SERVER_ERR))
                            {
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
            // Smart pointer automatically cleans up
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

    bool httpd::finalize_error_response(http_context & ctx,
                                        http_response::http_response_code code)
    {
        auto & req = ctx.request();
        // If the client said "Expect: 100-continue" and hasn't started
        // sending the body, we can respond immediately without draining.
        if (req.continue_100() && !req.has_overflow())
        {
            ctx.response().status_code(code);
            return true;
        }
        if (req.null_body_read())
        {
            ctx.response().status_code(code);
            return true;
        }
        LOG_WARN("Failed to read body");
        return false;
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