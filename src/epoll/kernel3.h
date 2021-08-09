#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <list>
#include <mutex>
#include <ovhttpd/thread_pool.h>
#include <ovhttpd/connection.h>
#include "spin_lock.h"
#include "hybrid_lock.h"
#include "kernel.h"

namespace epoll
{

    class connection_state_3
    {
    public:
        using lock_type = hybrid_lock;

        connection_state_3(std::shared_ptr<ovhttpd::connection> src,
                           std::shared_ptr<ovhttpd::connection> sink,
                           const std::string& host,
                           const int port);

        virtual ~connection_state_3();

        volatile bool src_block_read;
        volatile bool src_block_write;

        volatile bool sink_block_read;
        volatile bool sink_block_write;

        volatile bool src_rd_hup;
        volatile bool sink_rd_hup;

        volatile int write_shutdown_count;
        volatile bool closed;

        const std::string host;
        const int port;

        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;
        std::deque<char> src_overflow;
        std::deque<char> sink_overflow;
    
        lock_type state_lock;
    };

    class kernel3 : public kernel
    {

    private:

        const size_t MAX_OVERFLOW_SIZE = 1024 * 1024;
        const size_t BUFFER_SIZE = 128 * 1024;
        const int EPOLL_EVENT_SIZE = 100;
        const int THREAD_COUNT = 50;
        const int SHUTDOWN_JOB_SECONDS = 15;

        typedef std::tuple<std::chrono::steady_clock::time_point, std::shared_ptr<ovhttpd::connection>>
            shutdown_entry_t;

        std::atomic<int> accept_counter;
        std::atomic<int> close_request_counter;
        std::atomic<int> overflow_counter;

        std::shared_ptr<ovhttpd::thread_pool> tpool;

        std::unordered_map<int, std::shared_ptr<connection_state_3>> connection_map;
    
        std::list<shutdown_entry_t> shutdown_list;
        std::mutex shutdown_lock;

        void shutdown_job();

        std::mutex state_lock;
        int epoll_read_fd;
        int epoll_write_fd;
        void handle_read(int fd, std::shared_ptr<connection_state_3> state,
                         int events);
        void handle_write(int fd, std::shared_ptr<connection_state_3> state);
        void handle_rd_close(std::shared_ptr<connection_state_3> state,
                             std::unique_lock<connection_state_3::lock_type> & lk);
        void handle_hup(std::shared_ptr<connection_state_3> state,
                        std::unique_lock<connection_state_3::lock_type> & lk);
        void close_pair(std::shared_ptr<connection_state_3> state);
        void read_handler();
        void write_handler();
    
    protected:
        void notify_read(int fd, int events);
        void notify_write(int fd, int events);

    public:
        kernel3();
        virtual ~kernel3();

        void add(std::shared_ptr<ovhttpd::connection> accept_socket,
                 std::shared_ptr<ovhttpd::connection> connect_socket,
                 const std::shared_ptr<std::vector<char>> header,
                 const std::string& host,
                 const int port);

        void initialize() override;

        void start() override;

        constexpr static const char * NAME = "kernel3";

        const char * name() override
        {
            return NAME;
        }

        Json::Value get_stats() override;
    };
}
