#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <functional>
#include <chrono>
#include <ovhttpd/httpd.h>
#include <ovhttpd/log.h>
#include <ovhttpd/component_visor.h>
#include "kernel.h"
#include "kernel1.h"
#include "kernel2.h"
#include "kernel3.h"

namespace ovhttpd
{
}

namespace epoll
{

    const int kernel::epoll_wait_timeout_ms = 2000;
    
    kernel::kernel()
    {
    }
    
    kernel::~kernel()
    {
        std::for_each(epoll_fds.begin(), epoll_fds.end(),
                      [] (auto it)
                      {
                          if (close(it))
                          {
                              FATAL_ERRNO("close epoll fd failed", errno);
                          }
                      });
        epoll_fds.clear();
    }
    
    int kernel::create_epoll_fd()
    {
        int fd = epoll_create(1);
        
        if (fd == -1)
        {
            FATAL_ERRNO("epoll fd create failed", errno);
        }
        
        epoll_fds.insert(fd);
        
        return fd;
    }
    
    void kernel::add_read_epoll(int epoll_fd, int fd, bool include_in,
                                bool include_hup)
    {
        struct epoll_event epv;
        std::memset(&epv, 0, sizeof(epv));
        epv.data.fd = fd;
        epv.events = EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLONESHOT;
        if (include_in)
        {
            epv.events |= EPOLLIN;
        }
        if (include_hup)
        {
            epv.events |= EPOLLHUP;
        }
        
        int status = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &epv);
        if (status)
        {
            FATAL_ERRNO("epoll ctl add failed", errno);
        }
    }
    
    void kernel::modify_read_epoll(int epoll_fd, int fd, bool include_in,
                                   bool include_hup)
    {
        struct epoll_event epv;
        std::memset(&epv, 0, sizeof(epv));
        epv.data.fd = fd;
        epv.events = EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLONESHOT;
        if (include_in)
        {
            epv.events |= EPOLLIN;
        }
        if (include_hup)
        {
            epv.events |= EPOLLHUP;
        }
        
        int status = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &epv);
        if (status)
        {
            FATAL_ERRNO("epoll ctl mod failed", errno);
        }
    }
    
    void kernel::add_write_epoll(int epoll_fd, int fd, bool include_out)
    {
        struct epoll_event epv;
        std::memset(&epv, 0, sizeof(epv));
        epv.data.fd = fd;
        epv.events = EPOLLHUP | EPOLLERR | EPOLLET | EPOLLONESHOT;
        if (include_out)
        {
            epv.events |= EPOLLOUT;
        }
        
        int status = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &epv);
        if (status)
        {
            FATAL_ERRNO("epoll ctl add failed", errno);
        }
    }
    
    void kernel::modify_write_epoll(int epoll_fd, int fd, bool include_out)
    {
        struct epoll_event epv;
        std::memset(&epv, 0, sizeof(epv));
        epv.data.fd = fd;
        epv.events = EPOLLHUP | EPOLLERR | EPOLLET | EPOLLONESHOT;
        if (include_out)
        {
            epv.events |= EPOLLOUT;
        }
        
        int status = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &epv);
        if (status)
        {
            FATAL_ERRNO("epoll ctl mod failed", errno);
        }
    }
    
    void kernel::delete_epoll(int epoll_fd, int fd)
    {
        struct epoll_event epv;
        std::memset(&epv, 0, sizeof(epv));
        epv.data.fd = fd;
        
        int status =
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &epv);
        if (status)
        {
            FATAL_ERRNO("epoll ctl del failed", errno);
        }
    }
}
