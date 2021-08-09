#pragma once

#include <set>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <condition_variable>
#include <jsoncpp/json/json.h>
#include <ovhttpd/thread_pool.h>
#include <ovhttpd/scheduler.h>
#include <ovhttpd/component.h>

namespace epoll
{

    class kernel : public ovhttpd::component
    {
    private:
        std::set<int> epoll_fds;
        
    protected:

        static const int epoll_wait_timeout_ms;

        int create_epoll_fd();
        void add_read_epoll(int epoll_fd, int fd, bool include_in, bool include_hup = false);    
        void modify_read_epoll(int epoll_fd, int fd, bool include_in, bool include_hup = false);
        void add_write_epoll(int epoll_fd, int fd, bool include_out);
        void modify_write_epoll(int epoll_fd, int fd, bool include_out);
        void delete_epoll(int epoll_fd, int fd);
        
    public:
        kernel();

        virtual ~kernel();
    };
}
