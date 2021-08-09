#pragma once

#include <unordered_map>
#include <memory>
#include <vector>
#include <list>
#include <ovhttpd/thread_pool.h>
#include <ovhttpd/connection.h>
#include "spin_lock.h"
#include "kernel.h"

namespace epoll
{

    class connection_state
    {
    public:
        connection_state(std::shared_ptr<ovhttpd::connection> src,
                         std::shared_ptr<ovhttpd::connection> sink,
                         const std::string& host,
                         const int port);

        virtual ~connection_state();

        volatile int write_shutdown_count;
        volatile bool closed;

        const std::string host;
        const int port;

        std::shared_ptr<ovhttpd::connection> src;
        std::shared_ptr<ovhttpd::connection> sink;

        spin_lock state_lock;
    };

    class kernel2 : public kernel
    {

    private:

        typedef std::tuple<std::chrono::steady_clock::time_point, std::shared_ptr<ovhttpd::connection>>
            shutdown_entry_t;

        const int THREAD_COUNT = 50;

        const int SHUTDOWN_JOB_SECONDS = 5;

        std::atomic<int> accept_counter;
        std::atomic<int> close_request_counter;

        std::shared_ptr<ovhttpd::thread_pool> tpool;

        std::unordered_map<int, std::shared_ptr<connection_state>> connection_map;
    
        std::list<shutdown_entry_t> shutdown_list;
        spin_lock shutdown_lock;

        void shutdown_job();

        spin_lock state_lock;
        int epoll_read_fd;
        int epoll_write_fd;
        void handle_rd_close(std::shared_ptr<connection_state> state);
        void handle_hup(std::shared_ptr<connection_state> state);
        void read_handler();
        void write_handler();
    
    protected:
        void notify_read(int fd, int events);
        void notify_write(int fd, int events);

    public:
        kernel2();
        virtual ~kernel2();

        void add(std::shared_ptr<ovhttpd::connection> accept_socket,
                 std::shared_ptr<ovhttpd::connection> connect_socket,
                 const std::shared_ptr<std::vector<char>> header,
                 const std::string& host,
                 const int port);

        void initialize() override;

        void start() override;

        constexpr static const char * NAME = "kernel2";

        const char * name() override
        {
            return NAME;
        }

        Json::Value get_stats() override;
    };
}

