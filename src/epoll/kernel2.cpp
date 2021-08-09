#include <map>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iterator>
#include <thread>
#include <sys/epoll.h>
#include <unistd.h>
#include <ovhttpd/log.h>
#include <ovhttpd/connection.h>
#include "kernel2.h"

namespace epoll
{

    connection_state::connection_state(std::shared_ptr<ovhttpd::connection> src,
                                       std::shared_ptr<ovhttpd::connection> sink,
                                       const std::string& host, const int port) :
        src(src),
        sink(sink),
        write_shutdown_count(0), closed(false),
        host(host), port(port)
    {
        assert(this->src);
        assert(this->sink);
    }

    connection_state::~connection_state()
    {
    }

    static const int EPOLL_EVENT_SIZE = 100;

    static const int BUF_SIZE = (128*1024);

    void kernel2::write_handler()
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
                    LOG_DEBUG("kernel2 write handler shutting down");
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
                    else
                    {
                        FATAL_ERRNO("epoll wait failed", errno);
                    }
                }
            }
            while (status < 1);
            // status > 0
            tpool->begin_queue_work_item();
            for (int i=0; i<status; i++)
            {
                int s = events[i].data.fd;
                int mask = events[i].events;
                tpool->queue_work_item_batch([this, s, mask] {
                        this->notify_write(s, mask);
                    });
            }
            tpool->end_queue_work_item();
        }
    }

    void kernel2::shutdown_job()
    {
        LOG_DEBUG("kernel1 shutdown job running...");

        // scan the list until times don't expire
        shutdown_lock.lock();
    
        auto first_to_remove(shutdown_list.end());
        auto last_to_remove(shutdown_list.begin());
    
        std::list<shutdown_entry_t> todo;

        auto then = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        for (auto it = shutdown_list.begin(); it != shutdown_list.end(); it++)
        {
            last_to_remove++;
            if (std::get<0>(*it) < then)
            {
                todo.push_back(*it);
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
    
        // remove expired times
        if (first_to_remove != shutdown_list.end())
        {
            shutdown_list.erase(first_to_remove, last_to_remove);
        }
    
        shutdown_lock.unlock();

        schedule_job([this] {
                this->shutdown_job();
            }, SHUTDOWN_JOB_SECONDS * 1000);

        LOG_DEBUG("kernel2 shutdown job exitting");
    }

    void kernel2::read_handler()
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
                    LOG_DEBUG("kernel2 read handler shutting down");
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
                    else
                    {
                        FATAL_ERRNO("epoll wait failed", errno);
                    }
                }
            }
            while (status < 1);
            // status > 0
            tpool->begin_queue_work_item();
            for (int i=0; i<status; i++)
            {
                int s = events[i].data.fd;
                int mask = events[i].events;
                tpool->queue_work_item_batch([this, s, mask] {
                        this->notify_read(s, mask);
                    });
            }
            tpool->end_queue_work_item();
        }
    }

    kernel2::kernel2() :
        epoll_read_fd(create_epoll_fd()), epoll_write_fd(create_epoll_fd())
    {
    }

    kernel2::~kernel2()
    {
    }

    void kernel2::initialize()
    {
        tpool = add_thread_pool(THREAD_COUNT);

        for (int i=0; i<THREAD_COUNT; i++)
        {
            add_thread([this]() {
                    this->read_handler();
                });
            add_thread([this]() {
                    this->write_handler();
                });
        }
        LOG_DEBUG("kernel2 initalized");
    }

    void kernel2::start()
    {
        kernel::start();

        schedule_job([this] {
                this->shutdown_job();
            }, SHUTDOWN_JOB_SECONDS * 1000);

        LOG_DEBUG("kernel2 started");
    }

    void kernel2::add(std::shared_ptr<ovhttpd::connection> accept_socket,
                      std::shared_ptr<ovhttpd::connection> connect_socket,
                      const std::shared_ptr<std::vector<char>> header,
                      const std::string& host,
                      const int port)
    {
        auto state =
            std::make_shared<connection_state>(accept_socket,
                                               connect_socket,
                                               host, port);
        assert(state);

        accept_counter++;

        LOG_DEBUG("adding " << host << ":" << port << " " <<
                  accept_socket << " " << connect_socket);

        if (header && header->size() > 0)
        {
            state->sink->overflow.insert(state->sink->overflow.end(),
                                         header->begin(), header->end());
            LOG_DEBUG("sending header: " << header->size() << " for socket " <<
                      state->sink->get_socket());
        }

        // register pair
        state_lock.lock();
        connection_map[accept_socket->get_socket()] = state;
        connection_map[connect_socket->get_socket()] = state;
        // lock step
        state->state_lock.lock();
        state_lock.unlock();

        // register for reads and hups
        if (header && header->size() > 0)
        {
            // listen for EPOLLOUT if there is a header
            add_read_epoll(epoll_read_fd, accept_socket->get_socket(), false);
            add_write_epoll(epoll_write_fd, connect_socket->get_socket(), true);
        }
        else
        {
            // listen for EPOLLIN if there is no header
            add_read_epoll(epoll_read_fd, accept_socket->get_socket(), true);
            add_write_epoll(epoll_write_fd, connect_socket->get_socket(), false);
        }
        add_write_epoll(epoll_write_fd, accept_socket->get_socket(), false);
        add_read_epoll(epoll_read_fd, connect_socket->get_socket(), true);

        state->state_lock.unlock();

    }

    void kernel2::handle_rd_close(std::shared_ptr<connection_state> state)
    {
        assert(state);
    
        LOG_DEBUG("handling rdhup");

        state->state_lock.lock();

        bool closed = false;

        if (!state->closed)
        {
            state->write_shutdown_count++;
            if (state->write_shutdown_count == 2)
            {
                state->closed = true;
                closed = true;
            }
        }

        state->state_lock.unlock();

        if (closed)
        {
            LOG_DEBUG("shutting down state for rd close: " <<
                      state->src->get_socket() <<
                      " " << state->sink->get_socket());
            state_lock.lock();

            // remove state
            connection_map.erase(state->src->get_socket());
            connection_map.erase(state->sink->get_socket());
    
            // remove epoll subscriptions
            delete_epoll(epoll_read_fd, state->src->get_socket());
            delete_epoll(epoll_read_fd, state->sink->get_socket());
            delete_epoll(epoll_write_fd, state->src->get_socket());
            delete_epoll(epoll_write_fd, state->sink->get_socket());

            state_lock.unlock();

            // try to close other half of connections
            state->src->shutdown_write();
            state->sink->shutdown_write();
            state->src->shutdown_read();
            state->sink->shutdown_read();
            
            auto close_time = std::chrono::steady_clock::now();

            // add to shutdown list
            shutdown_lock.lock();

            shutdown_list.push_back(make_tuple(close_time, state->src));
            shutdown_list.push_back(make_tuple(close_time, state->sink));

            close_request_counter += 2;

            shutdown_lock.unlock();
        }

    }


    void kernel2::handle_hup(std::shared_ptr<connection_state> state)
    {
        assert(state);
    
        LOG_DEBUG("handling hup");

        state->state_lock.lock();

        bool closed = false;

        if (!state->closed)
        {
            state->closed = true;
            closed = true;
        }

        state->state_lock.unlock();

        if (closed)
        {
            LOG_DEBUG("shutting down state for hup: " <<
                      state->host << " " <<
                      state->src->get_socket() << " " <<
                      state->sink->get_socket());
            state_lock.lock();

            // remove state
            connection_map.erase(state->src->get_socket());
            connection_map.erase(state->sink->get_socket());
    
            // remove epoll subscriptions
            delete_epoll(epoll_read_fd, state->src->get_socket());
            delete_epoll(epoll_read_fd, state->sink->get_socket());
            delete_epoll(epoll_write_fd, state->src->get_socket());
            delete_epoll(epoll_write_fd, state->sink->get_socket());

            state_lock.unlock();

            // try to close other full state of connections
            state->src->shutdown_read();
            state->src->shutdown_write();
            state->sink->shutdown_read();
            state->sink->shutdown_write();

            auto close_time = std::chrono::steady_clock::now();

            // add to shutdown list
            shutdown_lock.lock();

            shutdown_list.push_back(make_tuple(close_time, state->src));
            shutdown_list.push_back(make_tuple(close_time, state->sink));

            close_request_counter += 2;

            shutdown_lock.unlock();
        }

    }


    void kernel2::notify_read(int fd, int events)
    {
        LOG_DEBUG("notify read: " << fd);

        if (events & EPOLLRDHUP)
        {
            LOG_DEBUG("epoll RDHUP: " << fd);
        }
    
        std::shared_ptr<connection_state> state = nullptr;

        state_lock.lock();
        auto ci = connection_map.find(fd);
        if (ci != connection_map.end())
        {
            state = ci->second;;
        }
        state_lock.unlock();

        // check for late events
        if (state == nullptr)
        {
            LOG_DEBUG("late event.  ignoring");
            return;
        }

        if ((events & EPOLLOUT) == EPOLLOUT)
        {
            FATAL("GOT EPOLLOUT");
        }

        if ((events & EPOLLERR) == EPOLLERR)
        {
            int       error = 0;
            socklen_t errlen = sizeof(error);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
            LOG_DEBUG_ERRNO("epoll read ERR: " << fd << " " << state->host, error);
            handle_hup(state);
            return;
        }
    
        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;

        if (fd == state->src->get_socket())
        {
            src = state->src;
            sink = state->sink;
        }
        else
        {
            sink = state->src;
            src = state->sink;
        }

        char buf[BUF_SIZE];
        ssize_t read, written;

        // read next frame
        ovhttpd::connection::CONNECTION_STATUS read_status =
            src->read(buf, sizeof(buf), read);
        while (read_status == ovhttpd::connection::CONNECTION_OK)
        {
            // write the frame if there is something was read
            ovhttpd::connection::CONNECTION_STATUS write_status =
                sink->write(buf, read, written);
            switch (write_status)
            {
            case ovhttpd::connection::CONNECTION_OK: // continue reading
            case ovhttpd::connection::CONNECTION_WANTS: // register for a write callback
            {
                assert(sink->overflow.size() == 0);
            
                // allocate overflow
                sink->overflow.insert(sink->overflow.end(),
                                      buf+written,
                                      buf+read);
            
                assert(sink->overflow.size() == read - written);
            
                // listen for writability
                state->state_lock.lock();
                if (!state->closed)
                {
                    modify_write_epoll(epoll_write_fd,
                                       sink->get_socket(), true);
                }
                state->state_lock.unlock();
                return;
            }
            break;
            case ovhttpd::connection::CONNECTION_CLOSED: // hup - close full pair
                LOG_DEBUG("write closed.  shutting down pair: " <<
                          sink->get_socket());
                handle_hup(state);
                return;
                break;
            case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
                LOG_WARN("write error.  shutting down pair: " <<
                         sink->get_socket());
                handle_hup(state);
                return;
                break;
            default:
                FATAL("invalid connection status: " << write_status);
            }
        
            // read the next frame
            read_status = src->read(buf, sizeof(buf), read);
        }

        // handle last read status if we got here
        switch (read_status)
        {
        case ovhttpd::connection::CONNECTION_WANTS:  // register for a read callback
            state->state_lock.lock();
            if (!state->closed)
            {
                modify_read_epoll(epoll_read_fd, src->get_socket(), true);
            }
            state->state_lock.unlock();
            break;
        case ovhttpd::connection::CONNECTION_CLOSED:  // rd hup - close sink write connection
            LOG_DEBUG("read closed on " << src->get_socket() <<
                      " shutting down write: " << sink->get_socket());
            sink->shutdown_write();
            handle_rd_close(state);
            break;
        case ovhttpd::connection::CONNECTION_OK:
            FATAL("read CONNECTION_OK not expected: " << src->get_socket());
            break;
        case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
            LOG_DEBUG("read error.  shutting down pair: " << sink->get_socket());
            handle_hup(state);
            break;
        default:
            FATAL("invalid connection status: " << read_status);
        }
    }

    void kernel2::notify_write(int fd, int events)
    {
        LOG_DEBUG("notify write: " << fd);

        std::shared_ptr<connection_state> state = nullptr;

        state_lock.lock();
        auto ci = connection_map.find(fd);
        if (ci != connection_map.end())
        {
            state = ci->second;;
        }
        state_lock.unlock();

        // check for late events
        if (state == nullptr)
        {
            LOG_DEBUG("late event.  ignoring");
            return;
        }

        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;

        if (fd == state->src->get_socket())
        {
            src = state->sink;
            sink = state->src;
        }
        else
        {
            sink = state->sink;
            src = state->src;
        }

        if ((events & EPOLLHUP) == EPOLLHUP)
        {
            LOG_DEBUG("epoll HUP: " << fd);
            handle_hup(state);
            return;
        }
    
        if ((events & EPOLLIN) == EPOLLIN)
        {
            FATAL("GOT EPOLLIN");
        }

        if ((events & EPOLLERR) == EPOLLERR)
        {
            int       error = 0;
            socklen_t errlen = sizeof(error);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
            LOG_DEBUG_ERRNO("epoll write ERR: " << fd << " " << state->host, error);
            handle_hup(state);
            return;
        }
    
        if (sink->overflow.size() == 0)
        {
            LOG_ERROR("unexpected no overflow: " << sink->overflow.size());
        }
        assert(sink->overflow.size() > 0);

        ssize_t written;

        std::vector<char> buf(std::make_move_iterator(sink->overflow.begin()),
                              std::make_move_iterator(sink->overflow.end()));

        ovhttpd::connection::CONNECTION_STATUS write_status =
            sink->write(buf.data(), buf.size(), written);
        if (write_status == ovhttpd::connection::CONNECTION_OK)
        {
            // reduce overflow
            LOG_DEBUG("REDUCING OVERFLOW BY " << written << " bytes");
        
            sink->overflow.erase(sink->overflow.begin(),
                                 sink->overflow.begin() + written);

            sink->overflow.shrink_to_fit();

            if (sink->overflow.size() > 0)
            {
                // need to flush again - wait for write event
                state->state_lock.lock();
                if (!state->closed)
                {
                    modify_write_epoll(epoll_write_fd, sink->get_socket(), true);
                }
                state->state_lock.unlock();
                return;
            }

            assert(0 == sink->overflow.size());

            char buf[BUF_SIZE];
            ssize_t read;

            ovhttpd::connection::CONNECTION_STATUS read_status =
                src->read(buf, sizeof(buf), read);	
            while (read_status == ovhttpd::connection::CONNECTION_OK)
            {
                ovhttpd::connection::CONNECTION_STATUS write_status =
                    sink->write(buf, read, written);
                switch (write_status)
                {
                case ovhttpd::connection::CONNECTION_OK: // continue reading
                    if (written < read)
                    {
                        // allocate overflow
                        sink->overflow.insert(sink->overflow.end(),
                                              buf+written,
                                              buf+read);
                        assert(sink->overflow.size() == read - written);
                    
                        // listen for write
                        state->state_lock.lock();
                        if (!state->closed)
                        {
                            modify_write_epoll(epoll_write_fd,
                                               sink->get_socket(), true);
                        }
                        state->state_lock.unlock();
                        return;
                    }
                    break;
                case ovhttpd::connection::CONNECTION_WANTS: // register for a write callback
                {
                    // listen for write
                    state->state_lock.lock();
                    if (!state->closed)
                    {
                        modify_write_epoll(epoll_write_fd,
                                           sink->get_socket(), true);
                    }
                    state->state_lock.unlock();
                    return;
                }
                break;
                case ovhttpd::connection::CONNECTION_CLOSED: // hup - close full pair
                    LOG_DEBUG("write closed.  shutting down pair: " <<
                              sink->get_socket());
                    handle_hup(state);
                    return;
                    break;
                case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
                    LOG_WARN("write error.  shutting down pair: " <<
                             sink->get_socket());
                    handle_hup(state);
                    return;
                    break;
                default:
                    FATAL("invalid connection status: " << write_status);
                }
	    
                // read the next frame
                read_status = src->read(buf, sizeof(buf), read);
            }

            // handle last read status if we got here
            switch (read_status)
            {
            case ovhttpd::connection::CONNECTION_WANTS:  // register for a read callback
                state->state_lock.lock();
                if (!state->closed)
                {
                    modify_read_epoll(epoll_read_fd, src->get_socket(), true);
                    modify_write_epoll(epoll_write_fd, sink->get_socket(), false);
                }
                state->state_lock.unlock();
                return;
                break;
            case ovhttpd::connection::CONNECTION_CLOSED:  // rd hup - close write connection
                LOG_DEBUG("read closed on " << src->get_socket() <<
                          " shutting down write: " <<
                          sink->get_socket());
                sink->shutdown_write();
                handle_rd_close(state);
                return;
                break;
            case ovhttpd::connection::CONNECTION_OK:
                FATAL("read CONNECTION_OK not expected: " << src->get_socket());
                break;
            case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
                LOG_WARN("read error.  shutting down write: " <<
                         sink->get_socket());
                handle_hup(state);
                return;
                break;
            default:
                FATAL("invalid connection status: " << read_status);
                break;
            }
        }
        else
        {
            // check flush status
            switch (write_status)
            {
            case ovhttpd::connection::CONNECTION_WANTS:
            {
                // need to flush again - wait for write event
                state->state_lock.lock();
                if (!state->closed)
                {
                    modify_write_epoll(epoll_write_fd, sink->get_socket(), true);
                }
                state->state_lock.unlock();
                return;
            }
            break;
            // hup - close socket pair
            case ovhttpd::connection::CONNECTION_CLOSED:
                LOG_DEBUG("write closed.  shutting down write: " <<
                          sink->get_socket());
                handle_hup(state);
                return;
                break;
                // error - close socket pair
            case ovhttpd::connection::CONNECTION_ERROR:
                LOG_WARN("write error.  shutting down write: " <<
                         sink->get_socket());
                handle_hup(state);
                return;
                break;
            case ovhttpd::connection::CONNECTION_OK:
                FATAL("write CONNECTION_OK not expected: " << sink->get_socket());
                break;
            default:
                FATAL("invalid connection status: " << write_status);
            }
        }
    }

    Json::Value kernel2::get_stats()
    {
        Json::Value v;

        state_lock.lock();
        v["connection_map"] = (Json::UInt64)connection_map.size();
        state_lock.unlock();

        shutdown_lock.lock();
        v["shutdown_list"] = (Json::UInt64)shutdown_list.size();
        shutdown_lock.unlock();

        return v;
    }

}
