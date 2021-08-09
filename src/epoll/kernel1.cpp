#include <map>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <regex>
#include <thread>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ovhttpd/log.h>
#include <ovhttpd/thread_pool.h>
#include "kernel1.h"
#include "kernel3.h"

namespace epoll
{

    bool kernel1::same_address(struct sockaddr & addr,
                               int port)
    {
        for (auto & x : local_addresses)
        {
            if (addr.sa_family == x.sa_family && port == listen_port)
            {
                if (addr.sa_family == AF_INET)
                {
                    struct sockaddr_in *a1 =
                        reinterpret_cast<struct sockaddr_in*>(&addr);
                    struct sockaddr_in *a2 =
                        reinterpret_cast<struct sockaddr_in*>(&x);
                    if (!std::memcmp(&a1->sin_addr, &a2->sin_addr,
                                     sizeof(a1->sin_addr)))
                    {
                        return true;
                    }
                }
                else if (addr.sa_family == AF_INET6)
                {
                    struct sockaddr_in6 *a1 =
                        reinterpret_cast<struct sockaddr_in6*>(&addr);
                    struct sockaddr_in6 *a2 =
                        reinterpret_cast<struct sockaddr_in6*>(&x);
                    if (!std::memcmp(&a1->sin6_addr, &a2->sin6_addr,
                                     sizeof(a1->sin6_addr)))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void kernel1::accept_handler()
    {
        LOG_DEBUG("accept handler waiting...");

        while (!should_shutdown())
        {
            int status;
            struct epoll_event event;
            memset(&event, 0, sizeof(event));

            do
            {
                status = epoll_wait(epoll_accept_fd, &event, 1, -1);
                if (should_shutdown())
                {
                    LOG_DEBUG("kernel1 accept handler shutting down");
                    return;
                }
                if (status < 0)
                {
                    if (errno == EINTR)
                    {
                        if (should_shutdown())
                        {
                            return;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    FATAL_ERRNO("epoll wait failed", errno);
                }
            }
            while (status < 1);

            LOG_DEBUG("handling accept triggered");
            bool cont = true;
            while (cont)
            {
                int s = listen_socket.accept();
                if (s < 0)
                {
                    cont = false;
                    // do nothing - just exit loop
                    if (listen_socket.get_last_error() != EAGAIN)
                    {
                        accept_fail_counter++;
                        LOG_WARN_ERRNO("accept failed",
                                       listen_socket.get_last_error());
                    }
                }
                else 
                {
                    accept_counter++;
                    handle_accept(s);
                }
            }

            // listen for read
            modify_read_epoll(epoll_accept_fd, listen_socket.get_socket(),
                              true, true);
        }
    }

    void kernel1::notify_read(int fd, int events)
    {
        LOG_DEBUG("enter: "<<fd);
        lock.lock();
    
        // get read read state
        std::shared_ptr<kernel1_read_state> read_state;
    
        auto it = read_map.find(fd);
        if (it != read_map.end())
        {
            read_state = std::get<1>(*it);
        }
    
        lock.unlock();
    
        if (read_state)
        {
            // if it's not a late event- handle it
            if (events & EPOLLRDHUP || events & EPOLLHUP || events & EPOLLERR)
            {
                read_state->accepted_socket->shutdown_write();
                read_state->accepted_socket->shutdown_read();
                delete_epoll(epoll_read_fd,
                             read_state->accepted_socket->get_socket());
                lock.lock();
                read_map.erase(fd);
                lock.unlock();
            }
            else
            {
                LOG_DEBUG("retry: "<<fd);
                retry_read_header(read_state);
            }
        }
        LOG_DEBUG("exit: "<<fd);
    }

    void kernel1::notify_write(int fd, int events)
    {
        LOG_DEBUG("enter: "<<fd);
        std::shared_ptr<kernel1_write_state> write_state;

        lock.lock();
        // get header write state
        auto it = write_map.find(fd);
        if (it != write_map.end())
        {
            write_state = std::get<1>(*it);
        }
        lock.unlock();
        
        // if it's not a late event, handle it
        if (write_state)
        {
            // if it's not a late event- handle it
            if (events & EPOLLHUP || events & EPOLLERR)
            {
                write_state->accepted_socket->shutdown_write();
                write_state->accepted_socket->shutdown_read();
                delete_epoll(epoll_write_fd,
                             write_state->accepted_socket->get_socket());
                lock.lock();
                write_map.erase(fd);
                lock.unlock();
            }
            else
            {
                LOG_DEBUG("retry: "<<fd);
                retry_write_header(write_state);
            }
        }
        LOG_DEBUG("exit: "<<fd);
    }

    void kernel1::read_handler()
    {
        LOG_DEBUG("read handler waiting...");

        while (!should_shutdown())
        {
            int status;
            struct epoll_event events[EPOLL_EVENT_SIZE];
            memset(events, 0, sizeof(events[0]) * EPOLL_EVENT_SIZE);

            do
            {
                status = epoll_wait(epoll_read_fd, events, EPOLL_EVENT_SIZE, -1);
                if (should_shutdown())
                {
                    LOG_DEBUG("kernel1 read handler shutting down");
                    return;
                }
                if (status < 0)
                {
                    if (errno == EINTR)
                    {
                        if (should_shutdown())
                        {
                            return;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    FATAL_ERRNO("epoll wait failed", errno);
                }
            }
            while (status < 1);

            LOG_DEBUG("read handler triggered");
        
            // status > 0
            read_tpool->begin_queue_work_item();
            for (int i=0; i<status; i++)
            {
                int s = events[i].data.fd;
                int mask = events[i].events;
                read_tpool->queue_work_item_batch([this, s, mask] {
                        this->notify_read(s, mask);
                    });
            }
            read_tpool->end_queue_work_item();
        }
    }

    void kernel1::write_handler()
    {
        LOG_DEBUG("write handler waiting...");

        while (!should_shutdown())
        {
            int status;
            struct epoll_event events[EPOLL_EVENT_SIZE];
            memset(events, 0, sizeof(events[0]) * EPOLL_EVENT_SIZE);

            do
            {
                status = epoll_wait(epoll_write_fd, events, EPOLL_EVENT_SIZE, -1);
                if (should_shutdown())
                {
                    LOG_DEBUG("kernel1 write handler shutting down");
                    return;
                }
                if (status < 0)
                {
                    if (errno == EINTR)
                    {
                        if (should_shutdown())
                        {
                            return;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    FATAL_ERRNO("epoll wait failed", errno);
                }
            }
            while (status < 1);

            LOG_DEBUG("write handler triggered");
        
            // status > 0
            write_tpool->begin_queue_work_item();
            for (int i=0; i<status; i++)
            {
                int s = events[i].data.fd;
                int mask = events[i].events;
                write_tpool->queue_work_item_batch([this, s, mask] {
                        this->notify_write(s, mask);
                    });
            }
            write_tpool->end_queue_work_item();
        }
    }

    void kernel1::notify_connect(int fd, int events)
    {
        LOG_DEBUG("enter: "<<fd);

        // get connect state
        lock.lock();
        auto it = connect_map.find(fd);
        std::shared_ptr<kernel1_connect_state> state;
        if (it != connect_map.end())
        {
            state = std::get<1>(*it);
        }
        lock.unlock();
    
        // if it's not a late event, handle it
        if (state)
        {
            LOG_DEBUG("retry: "<<fd);
            // retry connect
            retry_connect(state->connect_socket, state);
        }
        LOG_DEBUG("exit: "<<fd);
    }

    void kernel1::connect_handler()
    {
        LOG_DEBUG("connect handler waiting...");

        while (!should_shutdown())
        {
            int status;
            struct epoll_event events[EPOLL_EVENT_SIZE];
            memset(events, 0, sizeof(events[0]) * EPOLL_EVENT_SIZE);

            do
            {
                status = epoll_wait(epoll_connect_fd, events, EPOLL_EVENT_SIZE, -1);
                if (should_shutdown())
                {
                    LOG_DEBUG("kernel1 connect handler shutting down");
                    return;
                }
                if (status < 0)
                {
                    if (errno == EINTR)
                    {
                        if (should_shutdown())
                        {
                            return;
                        }
                        else
                        {
                            continue;
                        }
                    }
                    FATAL_ERRNO("epoll wait failed", errno);
                }
            }
            while (status < 1);

            LOG_DEBUG("connect handler triggered");
        
            // status > 0
            connect_tpool->begin_queue_work_item();
            for (int i=0; i<status; i++)
            {
                int s = events[i].data.fd;
                int mask = events[i].events;
                connect_tpool->queue_work_item_batch([this, s, mask] {
                        this->notify_connect(s, mask);
                    });
            }
            connect_tpool->end_queue_work_item();
        }
    }

    void kernel1::retry_connect(std::shared_ptr<ovhttpd::connection> connection,
                                std::shared_ptr<kernel1_connect_state> state)
    {
        int status = connection->connect(state->address.addr,
                                         state->address.addr_len);
        if (status)
        {
            if (connection->get_last_error() == EISCONN)
            {
                connect_counter++;
            
                LOG_DEBUG("connect succeeded: " <<
                          connection->get_socket());
		    
                // remove from epoll set
                delete_epoll(epoll_connect_fd, connection->get_socket());

                // remove mapping 
                lock.lock();
                connect_map.erase(connection->get_socket());
                lock.unlock();
		    
                // start it up
                next->add(state->accepted_socket, connection,
                          state->header, state->host, state->port);
            }
            else if (connection->get_last_error() == EINPROGRESS)
            {
                LOG_DEBUG("connect in progress: " << connection->get_socket());
		    
                // listen for writable
                modify_write_epoll(epoll_connect_fd,
                                   connection->get_socket(),
                                   true);
            }
            else
            {
                connect_fail_counter++;

                LOG_WARN_ERRNO("connect failed: " << state->host,
                               connection->get_last_error());
		    
                // remove from epoll set
                delete_epoll(epoll_connect_fd, connection->get_socket());

                // remove mapping 
                lock.lock();
                connect_map.erase(connection->get_socket());
                lock.unlock();
		    
                // shutdown connections
                schedule_close(connection);
                state->accepted_socket->shutdown_write();
                state->accepted_socket->shutdown_read();
                schedule_close(state->accepted_socket);
            }
        }
        else
        {
            connect_counter++;
            
            LOG_DEBUG("connect succeeded: " << connection->get_socket());
		
            // remove from epoll set
            delete_epoll(epoll_connect_fd, connection->get_socket());
		
            // remove mapping 
            lock.lock();
            connect_map.erase(connection->get_socket());
            lock.unlock();
		
            // start it up
            next->add(state->accepted_socket, connection,
                      state->header, state->host, state->port);
        }
    }

    void kernel1::try_connect(std::shared_ptr<ovhttpd::connection> accepted_socket,
                              const name_resolver & helper,
                              const std::shared_ptr<std::vector<char>> header,
                              const std::string & host, const int port)
    {
        auto conn = std::make_shared<ovhttpd::connection>(helper.family,
                                                 helper.socktype,
                                                 helper.protocol);
        assert(conn);

        LOG_DEBUG("create socket: " << conn->get_socket());
	
        // set non blocking on connect socket
        int status = conn->set_nonblocking();
        if (status)
        {
            FATAL_ERRNO("fcntl failed", conn->get_last_error());
        }
	
        // try to connect
        status = conn->connect(helper.addr, helper.addr_len);
        if (status)
        {
            // try again later
            if (conn->get_last_error() == EINPROGRESS)
            {
                LOG_DEBUG("start connect wants connect");
			
                auto state =
                    std::make_shared<kernel1_connect_state>(accepted_socket,
                                                            conn,
                                                            helper,
                                                            header,
                                                            host, port);
                assert(state);
            
                // register connect socket in map
                lock.lock();
                connect_map[conn->get_socket()] = state;
                lock.unlock();
			
                // listen for writable
                add_write_epoll(epoll_connect_fd, conn->get_socket(), true);
            }
            // error
            else
            {
                LOG_WARN_ERRNO("connect failed: " << helper.name << " " <<
                               host << ":" << port,
                               conn->get_last_error());

                accepted_socket->shutdown_write();
                accepted_socket->shutdown_read();
                schedule_close(accepted_socket);
            }
        }
        else // connected
        {
            connect_counter++;

            LOG_DEBUG("start connect succeeded: " <<
                      conn->get_socket());
            // start it up
            next->add(accepted_socket, conn, header, host, port);
        }
    }

    Json::Value kernel1::get_stats()
    {
        Json::Value v;

        lock.lock();
        v["connect_map"] = (Json::UInt64)connect_map.size();
        v["read_map"] = (Json::UInt64)read_map.size();
        v["write_map"] = (Json::UInt64)write_map.size();
        lock.unlock();

        return v;
    }

    void kernel1::dump_stats()
    {
        LOG_INFO("kernel1 stats: " << std::endl <<
                 "accept count: " << accept_counter << std::endl <<
                 "accept fails: " << accept_fail_counter << std::endl <<
                 "connect count: " << connect_counter << std::endl <<
                 "connect fails: " << connect_fail_counter << std::endl <<
                 "dns fails: " << name_fail_counter << std::endl);
        accept_counter = 0;
        accept_fail_counter = 0;
        connect_counter = 0;
        connect_fail_counter = 0;
        name_fail_counter = 0;
    }

    void kernel1::retry_write_header(std::shared_ptr<kernel1_write_state> state)
    {
    
        ssize_t sent;

        auto status =
            state->accepted_socket->write(state->buf + state->written,
                                          state->total_len - state->written, sent);
        switch (status)
        {
        case ovhttpd::connection::CONNECTION_ERROR:
            LOG_WARN_ERRNO("error writing header",
                           state->accepted_socket->get_last_error());

            // remove the state
            lock.lock();
            write_map.erase(state->accepted_socket->get_socket());
            lock.unlock();

            // remove from epoll set
            delete_epoll(epoll_write_fd, state->accepted_socket->get_socket());
		
            // close and shutdown socket
            state->accepted_socket->shutdown_write();
            state->accepted_socket->shutdown_read();
            schedule_close(state->accepted_socket);
            return;
            break;
        case ovhttpd::connection::CONNECTION_OK:
            // full send
            if (sent + state->written >= state->total_len)
            {

                // remove the state
                lock.lock();
                write_map.erase(state->accepted_socket->get_socket());
                lock.unlock();
            
                // remove from epoll set
                delete_epoll(epoll_write_fd, state->accepted_socket->get_socket());
            
                // start connection process
                if (state->connect)
                {
                    try_connect(state->accepted_socket, state->address,
                                state->header, state->host, state->port);
                }
                else
                {
                    state->accepted_socket->shutdown_write();
                    // TODO: listen for shut rd
                }
            }
            // partial send
            else
            {
                state->written += sent;
            
                // listen for writeable
                modify_write_epoll(epoll_write_fd,
                                   state->accepted_socket->get_socket(), true);
            }
            break;
        default:
            FATAL("unexpected write state: " << status);
            break;;
        }

    }

    void kernel1::try_write_header_200_response(std::shared_ptr<ovhttpd::connection> accepted_socket,
                                                const name_resolver& address,
                                                const std::shared_ptr<std::vector<char>> header,
                                                const bool http11,
                                                const std::string & host,
                                                const int port)
    {
        int len = std::strlen(header_200_response);

        ssize_t sent;

        auto status =
            accepted_socket->write(http11 ?
                                   header_200_response : header_200_10_response,
                                   len, sent);
        switch (status)
        {
        case ovhttpd::connection::CONNECTION_STATUS::CONNECTION_CLOSED:
        {
            accepted_socket->shutdown_write();
            accepted_socket->shutdown_read();
            schedule_close(accepted_socket);
        }
        break;
        case ovhttpd::connection::CONNECTION_STATUS::CONNECTION_ERROR:
        {
            LOG_WARN_ERRNO("error writing header",
                           accepted_socket->get_last_error());
            accepted_socket->shutdown_write();
            accepted_socket->shutdown_read();
            schedule_close(accepted_socket);
        }
        break;
        case ovhttpd::connection::CONNECTION_STATUS::CONNECTION_WANTS:
        {
            // store write state
            auto state =
                std::make_shared<kernel1_write_state>(accepted_socket,
                                                      header_200_response,
                                                      0, address, header,
                                                      host, port);
            assert(state);
        
            lock.lock();
            write_map[accepted_socket->get_socket()] = state;
            lock.unlock();
        
            // listen for writable
            add_write_epoll(epoll_write_fd, accepted_socket->get_socket(), true);
        }
        break;
        case ovhttpd::connection::CONNECTION_STATUS::CONNECTION_OK:
        {
            // full send
            if (sent == len)
            {
                // start connection process
                try_connect(accepted_socket, address, header, host, port);
            }
            // partial send
            else
            {
                // store write state
                auto state =
                    std::make_shared<kernel1_write_state>(accepted_socket,
                                                          header_200_response,
                                                          sent, address,
                                                          header, host, port);
            
                lock.lock();
                write_map[accepted_socket->get_socket()] = state;
                lock.unlock();
            
                // listen for writeable
                add_write_epoll(epoll_write_fd,
                                accepted_socket->get_socket(),
                                true);
            }
        }
        break;
        default:
            FATAL("unexpected write status: " << status);
            break;
        }
    }

    void kernel1::try_write_header_400_response(std::shared_ptr<ovhttpd::connection> accepted_socket,
                                                const char* header)
    {
        int len = std::strlen(header);

        ssize_t sent;

        auto status = accepted_socket->write(header, len, sent);
        switch (status)
        {
        case ovhttpd::connection::CONNECTION_CLOSED:
        {
            accepted_socket->shutdown_write();
            accepted_socket->shutdown_read();
            schedule_close(accepted_socket);
        }
        break;

        case ovhttpd::connection::CONNECTION_ERROR:
        {
            LOG_WARN_ERRNO("error writing header", accepted_socket->get_socket());
            accepted_socket->shutdown_write();
            accepted_socket->shutdown_read();
            schedule_close(accepted_socket);
        }
        break;

        case ovhttpd::connection::CONNECTION_WANTS:
        {
            // store write state
            auto state =
                std::make_shared<kernel1_write_state>(accepted_socket, header, 0);
            assert(state);
    
            lock.lock();
            write_map[accepted_socket->get_socket()] = state;
            lock.unlock();
        
            // listen for writable
            add_write_epoll(epoll_write_fd, accepted_socket->get_socket(), true);
        }
        break;

        case ovhttpd::connection::CONNECTION_OK:
        {
            if (sent == len)
            {
                // shutdown write and wait for close response
                accepted_socket->shutdown_write();
            
                // TODO: listen for rd close
            }
            // partial send
            else
            {
                // store write state
                auto state =
                    std::make_shared<kernel1_write_state>(accepted_socket,
                                                          header, sent);
                assert(state);
            
                lock.lock();
                write_map[accepted_socket->get_socket()] = state;
                lock.unlock();
            
                // listen for writeable
                add_write_epoll(epoll_write_fd,
                                accepted_socket->get_socket(), true);
            }
        }
        break;
        default:
        {
            FATAL("unexpted write status: " << status);
        }
        break;
        }
    }

    void kernel1::retry_read_header(std::shared_ptr<kernel1_read_state> state)
    {
        assert(state);

        ssize_t read;

        char buf[kernel1_read_state::MAX_HEADER_SIZE];

        auto status = state->accepted_socket->read(buf,
                                                   kernel1_read_state::MAX_HEADER_SIZE - state->buffer.size(),
                                                   read);
        switch (status)
        {
        case ovhttpd::connection::CONNECTION_ERROR:
        {
            LOG_WARN_ERRNO("error reading header",
                           state->accepted_socket->get_last_error());

            // remove from epoll set
            delete_epoll(epoll_read_fd, state->accepted_socket->get_socket());
		
            // remove state
            lock.lock();
            read_map.erase(state->accepted_socket->get_socket());
            lock.unlock();

            // shutdown and close socket
            state->accepted_socket->shutdown_write();
            state->accepted_socket->shutdown_read();
            schedule_close(state->accepted_socket);
        }
        break;
        case ovhttpd::connection::CONNECTION_CLOSED:

        {
            // remove from epoll set
            delete_epoll(epoll_read_fd, state->accepted_socket->get_socket());
		
            // remove state
            lock.lock();
            read_map.erase(state->accepted_socket->get_socket());
            lock.unlock();

            // shutdown and close socket
            state->accepted_socket->shutdown_write();
            state->accepted_socket->shutdown_read();
            schedule_close(state->accepted_socket);

        }
        break;

        case ovhttpd::connection::CONNECTION_WANTS:
        {
            LOG_ERROR("connection wants after a successful read poll");
        }
        break;

        case ovhttpd::connection::CONNECTION_OK:
        {
            state->buffer.insert(state->buffer.end(), buf, buf + read);
            // add trailing space
            state->buffer.push_back(0);
        
            // data read
            // search for full header
            char* pos = std::strstr(state->buffer.data(), crlfcrlf);
            // remove trailing space
            state->buffer.pop_back();
            // found full header
            if (pos != nullptr)
            {
                // remove from epoll set
                delete_epoll(epoll_read_fd, state->accepted_socket->get_socket());
		
                // remove state
                lock.lock();
                read_map.erase(state->accepted_socket->get_socket());
                lock.unlock();

                int index = pos - state->buffer.data();
                process_header(state->accepted_socket,
                               state->buffer.data(), state->buffer.size(),
                               index);
            }
            // hit max buffer without a header
            else if (state->buffer.size() >= kernel1_read_state::MAX_HEADER_SIZE)
            {
                // remove from epoll set
                delete_epoll(epoll_read_fd, state->accepted_socket->get_socket());
		
                // remove state
                lock.lock();
                read_map.erase(state->accepted_socket->get_socket());
                lock.unlock();

                // shutdown and close socket
                LOG_WARN("received max header size");
                state->accepted_socket->shutdown_write();
                state->accepted_socket->shutdown_read();
                schedule_close(state->accepted_socket);
            }
            // listen for more data
            else
            {
                // listen for readable
                modify_read_epoll(epoll_read_fd,
                                  state->accepted_socket->get_socket(), true, true);
            }
        }
        break;
        default:
        {
            FATAL("unexpected read status: " << status);
        }
        break;
        }
    }

    void kernel1::process_header(std::shared_ptr<ovhttpd::connection> accepted_socket,
                                 const char * buf, const int size, const int index)
    {
        // full header - now parse
        std::regex connect_regex(connect_header_regex);
    
        std::string header(buf, index);
        std::smatch match;
        bool connect_header = false;
        bool http11;
        std::string hostname;
        std::string port_str;
    
        std::regex_search(header, match, connect_regex);                
        // connect header
        if (match.size() == 4)
        {
            // found extra data
            if (index + 4 != size)
            {
                LOG_WARN("found extra data after header");
                accepted_socket->shutdown_write();
                accepted_socket->shutdown_read();
                schedule_close(accepted_socket);
                return;
            }
            hostname = match[1];
            port_str = match[2];
            connect_header = true;
            http11 = match[3] == "1.1";
        }
        // http header
        else 
        {
            std::regex plain_regex(header_regex);
            std::regex_search(header, match, plain_regex);                
            if (match.size() != 4)
            {
                LOG_WARN("received invalid proxy header:\r\n" << header);
                accepted_socket->shutdown_write();
                accepted_socket->shutdown_read();
                schedule_close(accepted_socket);
                return;
            }
            if (!parse_header(header, hostname, port_str))
            {
                LOG_WARN("failed to parse header 2: " << header);
                const char* response =
                    http11 ? header_400_response : header_400_10_response;
                try_write_header_400_response(accepted_socket, response);
                return;
            }
            http11 = match[3] == "1.1";
        }
        if (hostname.size() == 0 || port_str.size() == 0)
        {
            LOG_WARN("received invalid proxy header (no hostname or port):\r\n" << header);
            accepted_socket->shutdown_write();
            accepted_socket->shutdown_read();
            schedule_close(accepted_socket);
            return;
        }

        // good header - try reply
        LOG_DEBUG("proxy request: " << hostname << ":" << port_str);
    
        // if a connect header - reply with a 200 before connecting
        std::shared_ptr<std::vector<char>> vbuf;
        // not a connect header - store whatever was written
        if (!connect_header)
        {
            vbuf = std::make_shared<std::vector<char>>();
            assert(vbuf);
            vbuf->insert(vbuf->end(), buf, buf+size);
        }

        // queue up name resolution on the thread pool so as not to block
        // the accept worker thread
        dns_tpool->queue_work_item([this, vbuf, connect_header, hostname, port_str, accepted_socket, http11] {

                name_resolver address;
    
                int status =
                    name_resolver::resolve(hostname.c_str(),
                                           port_str.c_str(),
                                           ipv4_support, ipv6_support,
                                           address);
            
                if (status)
                {
                    name_fail_counter++;
                    LOG_WARN("failed to resolve address: " << hostname <<
                             " status=" << gai_strerror(status));
                    const char* response =
                        http11 ? header_404_response : header_404_10_response;
                    this->try_write_header_400_response(accepted_socket, response);
                
                    return;
                }
            
                int port = stoi(port_str);
            
                if (same_address(address.addr, port))
                {
                    LOG_WARN("closed connect to self");

                    accepted_socket->shutdown_write();
                    accepted_socket->shutdown_read();
                    schedule_close(accepted_socket);

                    return;
                }
            
                // if a connect header - reply with a 200 before connecting
                if (connect_header)
                {
                    try_write_header_200_response(accepted_socket,
                                                  address,
                                                  vbuf,
                                                  http11,
                                                  hostname, port);
                }
                // not a connect header - start connecting
                else
                {
                    try_connect(accepted_socket, address, vbuf,
                                hostname, port);
                }
            });
    }

    bool kernel1::parse_header(const std::string& header,
                               std::string& hostname,
                               std::string& port) const
    {
        std::string host = "host:";
        std::string line;

        std::stringstream ss(header);

        while (std::getline(ss, line))
        {
            std::stringstream sline(line);
    
            std::string item;

            sline >> item;

            // case insenstive compare
            if ((item.size() == host.size()) &&
                std::equal(item.begin(), item.end(), host.begin(),
                           [](char & c1, char & c2){
                               return (c1 == c2 || std::toupper(c1) == std::toupper(c2));
                           }))
            {
                sline >> item;
                size_t index = item.find(":");

                // default host port
                if (index == std::string::npos)
                {
                    hostname = item;
                    port = port_80_string;
                }
                // custom host port
                else
                {
                    hostname = item.substr(0, index);
                    port = item.substr(index+1);
                }

                return true;
            }
        }
        return false;
    }

    void kernel1::try_read_header(std::shared_ptr<ovhttpd::connection> connection)
    {
        ssize_t read;

        char buf[kernel1_read_state::MAX_HEADER_SIZE+1];

        auto status = connection->read(buf,
                                       kernel1_read_state::MAX_HEADER_SIZE,
                                       read);
        switch (status)
        {
        case ovhttpd::connection::CONNECTION_CLOSED:
        {
            connection->shutdown_write();
            connection->shutdown_read();
            schedule_close(connection);
        }
        break;
        case ovhttpd::connection::CONNECTION_OK:
        {
            buf[read] = 0;
            char* pos = std::strstr(buf, crlfcrlf);
            // found full header
            if (pos != nullptr)
            {
                int index = pos - buf;
                process_header(connection, buf, read, index);
            }
            // hit max buffer without a header
            else if (read >= kernel1_read_state::MAX_HEADER_SIZE)
            {
                LOG_WARN("received max header size");
                connection->shutdown_write();
                connection->shutdown_read();
                schedule_close(connection);
            }
            // listen for more data
            else
            {
                auto state =
                    std::make_shared<kernel1_read_state>(connection, buf, read);
                assert(state);

                // store read state
                lock.lock();
                read_map[connection->get_socket()] = state;
                lock.unlock();

                // listen for readable
                add_read_epoll(epoll_read_fd, connection->get_socket(), true, true);
            }
        }
        break;
        case ovhttpd::connection::CONNECTION_WANTS:
        {
            // store read state
            auto state =
                std::make_shared<kernel1_read_state>(connection, buf, read);
            assert(state);
        
            lock.lock();
            read_map[connection->get_socket()] = state;
            lock.unlock();
        
            // listen for readable
            add_read_epoll(epoll_read_fd, connection->get_socket(), true, true);
        }
        break;
        case ovhttpd::connection::CONNECTION_ERROR:
        {
            LOG_WARN_ERRNO("error reading header", connection->get_last_error());
            connection->shutdown_write();
            connection->shutdown_read();
            schedule_close(connection);
        }
        break;
        default:
        {
            FATAL("unexpected connection status: " << status);
        }
        break;
        }
    }

    void kernel1::handle_accept(const int s)
    {
        LOG_DEBUG("accept success: " << s);

        auto accepted_socket = std::make_shared<ovhttpd::connection>(s);

        if (accepted_socket->set_nonblocking())
        {
            FATAL_ERRNO("fcntl failed", accepted_socket->get_last_error());
        }

        try_read_header(accepted_socket);
    }

    kernel1::kernel1() : 
        epoll_accept_fd(create_epoll_fd()), epoll_connect_fd(create_epoll_fd()),
        epoll_read_fd(create_epoll_fd()), epoll_write_fd(create_epoll_fd()),
        next(nullptr), listen_port(-1), listen_socket(AF_INET, SOCK_STREAM, 0)
    {
    }

    kernel1::~kernel1()
    {
    }

    void kernel1::set_listen_port(int port)
    {
        listen_port = port;
    }

    void kernel1::schedule_close(const std::shared_ptr<ovhttpd::connection> connection)
    {
        auto close_time = std::chrono::steady_clock::now();

        shutdown_lock.lock();
        shutdown_list.push_back(std::make_tuple(close_time, connection));
        shutdown_lock.unlock();
    }

    void kernel1::run_close_job()
    {
        // scan the list until times don't expire
        shutdown_lock.lock();
    
        auto first_to_remove(shutdown_list.end());
        auto last_to_remove(shutdown_list.begin());
    
        auto then = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        for (auto it = shutdown_list.begin(); it != shutdown_list.end(); it++)
        {
            last_to_remove++;
            if (std::get<0>(*it) < then)
            {
                if (first_to_remove == shutdown_list.end())
                {
                    first_to_remove = it;
                }
            }
            else
            {
                break;
            }
        }
    
        std::list<std::shared_ptr<ovhttpd::connection>> to_close;

        // remove expired times
        if (first_to_remove != shutdown_list.end())
        {
            for (auto it = first_to_remove; it != last_to_remove; it++)
            {
                to_close.push_back(std::get<1>(*it));
            }
            shutdown_list.erase(first_to_remove, last_to_remove);
        }
    
        shutdown_lock.unlock();

        // schedule shutdown
        schedule_job([this] {
                this->run_close_job();
            }, SHUTDOWN_JOB_SECONDS * 1000);
    }

    void kernel1::initialize()
    {
        next = get_component<kernel3>(kernel3::NAME);
        assert(next);

        // get local addresses
        struct ifaddrs *ifap;

        int status = getifaddrs(&ifap);
        if (status)
        {
            FATAL_ERRNO("getifaddrs failed", errno);
        }
        if (!ifap)
        {
            FATAL("no ethernet interfaces available");
        }
        struct ifaddrs *cur_if = ifap;
        while (cur_if)
        {
            struct sockaddr addr = *(cur_if->ifa_addr);
            if (addr.sa_family == AF_INET)
            {
                ipv4_support = true;
                LOG_DEBUG("iface: " << cur_if->ifa_name);
                local_addresses.push_back(addr);
            }
            else if (addr.sa_family == AF_INET6)
            {
                ipv6_support = true;
                LOG_DEBUG("iface: " << cur_if->ifa_name);
                local_addresses.push_back(addr);
            }
            cur_if = cur_if->ifa_next;
        }
        freeifaddrs(ifap);

        dns_tpool = add_thread_pool(100);
        read_tpool = add_thread_pool(100);
        write_tpool = add_thread_pool(100);
        connect_tpool = add_thread_pool(100);

        // set listen socket to non-blocking
        status = listen_socket.set_nonblocking();
        if (status)
        {
            FATAL_ERRNO("fcntl failed", listen_socket.get_last_error());
        }

        // reuse addr 
        status = listen_socket.reuse_addr(true);
        if (status)
        {
            FATAL_ERRNO("setsockopt failed", listen_socket.get_last_error());
        }

        // bind
        status = listen_socket.bind(listen_port);
        if (status)
        {
            FATAL_ERRNO("bind on port " << listen_port << " failed",
                        listen_socket.get_last_error());
        }

        // listen
        status = listen_socket.listen(10000);
        if (status)
        {
            FATAL_ERRNO("listen failed", listen_socket.get_last_error());
        }

        LOG_DEBUG("listening on port " << listen_port);

        // create accept thread
        add_thread([this]() {
                accept_handler();
            });
        // create connect, read and write threads
        for (int i=0; i<THREAD_COUNT; i++)
        {
            // connect thread
            add_thread([this]() {
                    connect_handler();
                });

            // read thread
            add_thread([this]() {
                    read_handler();
                });

            // write thread
            add_thread([this]() {
                    write_handler();
                });
        }

        // listen for readable
        add_read_epoll(epoll_accept_fd, listen_socket.get_socket(), true, true);

        LOG_DEBUG("kernel1 initialized on port " << listen_port);
    }

    void kernel1::start()
    {
        kernel::start();

        // schedule shutdown
        schedule_job([this] {
                this->run_close_job();
            }, SHUTDOWN_JOB_SECONDS * 1000);

        LOG_DEBUG("kernel1 started");
    }

    void kernel1::release()
    {
        delete_epoll(epoll_accept_fd, listen_socket.get_socket());
        run_close_job();
        connect_map.clear();
        read_map.clear();
        write_map.clear();
    }
}
