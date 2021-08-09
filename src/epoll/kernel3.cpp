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
#include "kernel3.h"

namespace epoll
{

    connection_state_3::connection_state_3(std::shared_ptr<ovhttpd::connection> src,
                                           std::shared_ptr<ovhttpd::connection> sink,
                                           const std::string& host, const int port) :
        src(src),
        sink(sink),
        write_shutdown_count(0), closed(false),
        host(host), port(port),
        src_block_read(true), src_block_write(false),
        sink_block_read(true), sink_block_write(false),
        src_rd_hup(false), sink_rd_hup(false)
    {
        assert(this->src);
        assert(this->sink);
    }

    connection_state_3::~connection_state_3()
    {
    }

    void kernel3::write_handler()
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
                    LOG_DEBUG("kernel3 write handler shutting down");
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

    void kernel3::shutdown_job()
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

        LOG_DEBUG("kernel3 shutdown job exitting");
    }

    void kernel3::read_handler()
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
                    LOG_DEBUG("kernel3 read handler shutting down");
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

    kernel3::kernel3() :
        epoll_read_fd(create_epoll_fd()), epoll_write_fd(create_epoll_fd())
    {
    }

    kernel3::~kernel3()
    {
    }

    void kernel3::initialize()
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
        LOG_DEBUG("kernel3 initalized");
    }

    void kernel3::start()
    {
        kernel::start();

        schedule_job([this] {
                this->shutdown_job();
            }, SHUTDOWN_JOB_SECONDS * 1000);

        LOG_DEBUG("kernel3 started");
    }

    void kernel3::add(std::shared_ptr<ovhttpd::connection> accept_socket,
                      std::shared_ptr<ovhttpd::connection> connect_socket,
                      const std::shared_ptr<std::vector<char>> header,
                      const std::string& host,
                      const int port)
    {
        auto state =
            std::make_shared<connection_state_3>(accept_socket,
                                                 connect_socket,
                                                 host, port);
        assert(state);

        accept_counter++;

        LOG_DEBUG("adding " << host << ":" << port << " " <<
                  accept_socket << " " << connect_socket);

        if (header && header->size() > 0)
        {
            state->sink_overflow.insert(state->sink_overflow.end(),
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
            add_write_epoll(epoll_write_fd, connect_socket->get_socket(), true);
        }
        else
        {
            // listen for EPOLLIN if there is no header
            add_write_epoll(epoll_write_fd, connect_socket->get_socket(), false);
        }
        add_read_epoll(epoll_read_fd, connect_socket->get_socket(), true);
        add_read_epoll(epoll_read_fd, accept_socket->get_socket(), true);
        add_write_epoll(epoll_write_fd, accept_socket->get_socket(), false);

        state->state_lock.unlock();
    }

    void kernel3::handle_rd_close(std::shared_ptr<connection_state_3> state,
                                  std::unique_lock<connection_state_3::lock_type> & lk)
    {
        assert(state);
    
        LOG_DEBUG("handling rdhup");

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

        if (closed)
        {
            LOG_DEBUG("shutting down state for rd close: " <<
                      state->src->get_socket() <<
                      " " << state->sink->get_socket());
        
            lk.unlock();
            close_pair(state);
            lk.lock();
        }

    }


    void kernel3::close_pair(std::shared_ptr<connection_state_3> state)
    {
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

    void kernel3::handle_hup(std::shared_ptr<connection_state_3> state,
                             std::unique_lock<connection_state_3::lock_type> & lk)
    {
        assert(state);
    
        LOG_DEBUG("handling hup");

        bool closed = false;
        if (!state->closed)
        {
            state->closed = true;
            closed = true;
        }

        if (closed)
        {
            LOG_DEBUG("shutting down state for hup: " <<
                      state->host << " " <<
                      state->src->get_socket() << " " <<
                      state->sink->get_socket());

            lk.unlock();
            close_pair(state);
            lk.lock();
        }

    }

    void kernel3::notify_read(int fd, int events)
    {
        LOG_DEBUG("notify read: " << fd);

        if (events & EPOLLRDHUP)
        {
            LOG_DEBUG("epoll RDHUP: " << fd);
        }
    
        std::shared_ptr<connection_state_3> state = nullptr;

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

            std::unique_lock<connection_state_3::lock_type> lk(state->state_lock);

            handle_hup(state, lk);
            return;
        }

        handle_read(fd, state, events);
    }    

    void kernel3::handle_read(int fd, std::shared_ptr<connection_state_3> state,
                              int events)
    {

        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;
        std::deque<char> * overflow;
        volatile bool *src_block_write;
        volatile bool *sink_block_read;
        volatile bool *src_rd_hup;

        std::unique_lock<connection_state_3::lock_type> lk(state->state_lock);

        if (state->closed)
        {
            return;
        }
        
        if (fd == state->src->get_socket())
        {
            src = state->src;
            sink = state->sink;
            overflow = &(state->sink_overflow);
            src_block_write = &(state->src_block_write);
            sink_block_read = &(state->sink_block_read);
            src_rd_hup = &(state->src_rd_hup);
        }
        else
        {
            sink = state->src;
            src = state->sink;
            overflow = &(state->src_overflow);
            src_block_write = &(state->sink_block_write);
            sink_block_read = &(state->src_block_read);
            src_rd_hup = &(state->sink_rd_hup);
        }

        ssize_t read, written;

        *src_block_write = false;

        ovhttpd::connection::CONNECTION_STATUS read_status;

        if (events & EPOLLIN)
        {
            // read next frame
            lk.unlock();
        
            char buf[BUFFER_SIZE];

            read_status = src->read(buf, sizeof(buf), read);
        
            lk.lock();
        
            if (state->closed)
            {
                return;
            }
        
            while (read_status == ovhttpd::connection::CONNECTION_OK &&
                   read > 0)
            {
                if (*sink_block_read)
                {
                    assert(overflow->empty());
                
                    lk.unlock();
                    auto write_status = sink->write(buf, read, written);
                    lk.lock();
                
                    if (state->closed)
                    {
                        return;
                    }
                
                    switch (write_status)
                    {
                    case ovhttpd::connection::CONNECTION_OK:
                    case ovhttpd::connection::CONNECTION_WANTS:
                    {
                        if (written < read)
                        {
                            overflow_counter++;
                            overflow->insert(overflow->end(),
                                             buf+written,
                                             buf+read);
                        
                            *sink_block_read = false;
                        
                            modify_write_epoll(epoll_write_fd,
                                               sink->get_socket(), true);
                        
                            // NOTE: removing this condition as it kills performance
//                    if (overflow->size() >= MAX_OVERFLOW_SIZE)
                            {
                                *src_block_write = true;
                            
                                modify_read_epoll(epoll_read_fd,
                                                  src->get_socket(), false);
                                return;
                            }
                        }
                    }
                    break;
                    case ovhttpd::connection::CONNECTION_CLOSED:
                    {
                        handle_hup(state, lk);
                        return;
                    }
                    break;
                    case ovhttpd::connection::CONNECTION_ERROR:
                    {
                        handle_hup(state, lk);
                        return;
                    }
                    break;
                    }
                }
                else
                {
                    overflow_counter++;
                    // allocate overflow
                    overflow->insert(overflow->end(),
                                     buf,
                                     buf+read);
                
                    if (overflow->size() >= MAX_OVERFLOW_SIZE)
                    {
                        *src_block_write = true;
                    
                        modify_read_epoll(epoll_read_fd, src->get_socket(), false);
                    
                        return;
                    }
                }

                lk.unlock();
                read_status = src->read(buf, sizeof(buf), read);
                lk.lock();
            
                if (state->closed)
                {
                    return;
                }
            }
        }

        if (events & EPOLLRDHUP)
        {
            LOG_DEBUG("read closed on " << src->get_socket() <<
                      " shutting down write: " << sink->get_socket());
            *src_rd_hup = true;
            if (!overflow->empty() && *sink_block_read)
            {
                *sink_block_read = false;
                modify_write_epoll(epoll_write_fd, sink->get_socket(), true);
            }
            else
            {
                sink->shutdown_write();
            }

            handle_rd_close(state, lk);
            return;
        }

        if (events & EPOLLIN)
        {
            // handle last read status if we got here
            switch (read_status)
            {
            case ovhttpd::connection::CONNECTION_OK:
            {
                assert(read == 0);
                modify_read_epoll(epoll_read_fd, src->get_socket(), true);
                break;
            }
            case ovhttpd::connection::CONNECTION_WANTS:  // register for a read callback
            {
                modify_read_epoll(epoll_read_fd, src->get_socket(), true);
                break;
            }
            case ovhttpd::connection::CONNECTION_CLOSED:  // rd hup - close sink write connection
                FATAL("read CONNECTION_CLOSED not expected: " << src->get_socket());
                break;
            case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
                LOG_DEBUG("read error.  shutting down pair: " << sink->get_socket());
            
                handle_hup(state, lk);
            
                break;
            default:
                FATAL("invalid connection status: " << read_status);
            }
        }
    }

    void kernel3::notify_write(int fd, int events)
    {
        LOG_DEBUG("notify write: " << fd);

        std::shared_ptr<connection_state_3> state = nullptr;

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

        if ((events & EPOLLHUP) == EPOLLHUP)
        {
            LOG_DEBUG("epoll HUP: " << fd);

            std::unique_lock<connection_state_3::lock_type> lk(state->state_lock);

            handle_hup(state, lk);
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

            std::unique_lock<connection_state_3::lock_type> lk(state->state_lock);

            handle_hup(state, lk);
            return;
        }
    
        handle_write(fd, state);
    }

    void kernel3::handle_write(int fd, std::shared_ptr<connection_state_3> state)
    {
        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;
        std::deque<char> * overflow;
        volatile bool *src_block_write;
        volatile bool *sink_block_read;
        volatile bool *src_rd_hup;

        std::unique_lock<connection_state_3::lock_type> lk(state->state_lock);

        if (state->closed)
        {
            return;
        }

        // capture buffer
        if (fd == state->src->get_socket())
        {
            src = state->sink;
            sink = state->src;
            overflow = &(state->src_overflow);
            src_block_write = &(state->sink_block_write);
            sink_block_read = &(state->src_block_read);
            src_rd_hup = &(state->sink_rd_hup);
        }
        else
        {
            sink = state->sink;
            src = state->src;
            overflow = &(state->sink_overflow);
            src_block_write = &(state->src_block_write);
            sink_block_read = &(state->sink_block_read);
            src_rd_hup = &(state->src_rd_hup);
        }

        *sink_block_read = false;

        if (overflow->size() == 0)
        {
            if (*src_rd_hup)
            {
                sink->shutdown_write();
            }
            modify_write_epoll(epoll_write_fd, sink->get_socket(), false);
            return;
        }



        char buf[BUFFER_SIZE];
        auto it = overflow->cbegin();
        size_t buf_len = 0;

        for (int i=0; i<BUFFER_SIZE && it != overflow->cend(); i++, it++)
        {
            buf[i] = *it;
            buf_len++;
        }

        assert(buf_len);

        // write data
        ssize_t written;

        lk.unlock();

        ovhttpd::connection::CONNECTION_STATUS write_status =
            sink->write(buf, buf_len, written);

        lk.lock();

        if (state->closed)
        {
            return;
        }
        
        while (write_status == ovhttpd::connection::CONNECTION_OK)
        {
            // reduce overflow
            overflow->erase(overflow->cbegin(),
                            overflow->cbegin() + written);
//        overflow->shrink_to_fit();

            // notify reader if they are blocked
            if (*src_block_write && overflow->size() < MAX_OVERFLOW_SIZE)
            {
                *src_block_write = false;
                modify_read_epoll(epoll_read_fd, src->get_socket(), true);
            }

            // can't do a full write, wait for write event
            if (written < buf_len)
            {
                modify_write_epoll(epoll_write_fd, sink->get_socket(), true);
                return;
            }

            // get the next buffer
            it = overflow->cbegin();
            buf_len = 0;

            for (int i=0; i<BUFFER_SIZE && it != overflow->cend(); i++, it++)
            {
                buf[i] = *it;
                buf_len++;
            }

            // wait for hup event if the overflow buffer is empty
            if (buf_len == 0)
            {
                if (*src_rd_hup)
                {
                    sink->shutdown_write();
                }
                else
                {
                    *sink_block_read = true;
                    modify_write_epoll(epoll_write_fd, sink->get_socket(), false);
                }
                return;
            }

            lk.unlock();

            write_status = sink->write(buf, buf_len, written);

            lk.lock();

            if (state->closed)
            {
                return;
            }
        
        }
        switch (write_status)
        {
        case ovhttpd::connection::CONNECTION_OK: // continue reading
            FATAL("unexpected state");
            break;
        case ovhttpd::connection::CONNECTION_WANTS: // register for a write callback
        {
            modify_write_epoll(epoll_write_fd, sink->get_socket(), true);
            return;
        }
        break;
        case ovhttpd::connection::CONNECTION_CLOSED: // hup - close full pair
            LOG_DEBUG("write closed.  shutting down pair: " <<
                      sink->get_socket());

            handle_hup(state, lk);
            return;
            break;
        case ovhttpd::connection::CONNECTION_ERROR: // error - close full pair
            LOG_WARN("write error.  shutting down pair: " <<
                     sink->get_socket());

            handle_hup(state, lk);

            return;
            break;
        default:
            FATAL("invalid connection status: " << write_status);
        }
    }

    Json::Value kernel3::get_stats()
    {
        Json::Value v;

        state_lock.lock();
        v["connection_map"] = (Json::UInt64)connection_map.size();
        state_lock.unlock();

        shutdown_lock.lock();
        v["shutdown_list"] = (Json::UInt64)shutdown_list.size();
        shutdown_lock.unlock();

        v["overflow_count"] = overflow_counter.load();

        return v;
    }
}
