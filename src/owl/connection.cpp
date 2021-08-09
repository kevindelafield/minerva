#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <jsoncpp/json/config.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstring>
#include <algorithm>
#include "connection.h"
#include "log.h"
#include "locks.h"

namespace owl
{

    std::atomic<unsigned long long> connection::shutdown_counter;
    std::atomic<unsigned long long> connection::open_counter;
    std::atomic<unsigned long long> connection::read_counter;
    std::atomic<unsigned long long> connection::write_counter;
    std::atomic<unsigned long long> connection::socket_counter;

    connection::connection(int family, int socktype, int protocol) :
        last_err(0),
        read_status(CONNECTION_WANTS_READ),
        write_status(CONNECTION_OK),
        accept_status(CONNECTION_OK),
        last_read(std::chrono::steady_clock::now()),
        last_write(std::chrono::steady_clock::now())
    {
        std::unique_lock<std::mutex> lk(fd_lock);

        int s = ::socket(family, socktype, protocol);
        if (s == -1)
        {
            FATAL_ERRNO("socket create failed", errno);
        }
        socket = s;
        open_counter++;
        socket_counter++;
        set_close_on_exec(true);
    }

    connection::connection(int socket) :
        socket(socket), last_err(0),
        read_status(CONNECTION_WANTS_READ),
        write_status(CONNECTION_OK),
        accept_status(CONNECTION_OK)
    {
        open_counter++;
        socket_counter++;
        set_close_on_exec(true);
    }

    connection::~connection()
    {
        int status = close(socket);
        if (status)
        {
            FATAL_ERRNO("error closing socket: " << socket, errno);
        }
        shutdown_counter++;
        socket_counter--;
    }

    Json::Value connection::get_stats()
    {
        Json::Value v;

        v["read"] = (Json::UInt64)read_counter.load();
        v["written"] = (Json::UInt64)write_counter.load();
        v["sockets"] = (Json::UInt64)socket_counter.load();
        v["open"] = (Json::UInt64)open_counter.load();
        v["close"] = (Json::UInt64)shutdown_counter.load();

        return v;
    }

    bool connection::data_available(bool & available)
    {
        // ioctl(fd,FIONREAD,&bytes_available)

        char buf;

        last_err = 0;
        ssize_t r = recv(socket, &buf, 1, MSG_PEEK);
        if (r < 0)
        {
            LOG_ERROR_ERRNO("failed to peek on socket", errno);
            available = false;
            return false;
        }

        available = r > 0;

        return true;
        
    }

    int connection::get_local_addr(const struct sockaddr_in & client_addr, 
                                   socklen_t &client_addr_len,
                                   struct sockaddr_in & addr,
                                   socklen_t & addr_len)
    {
        int status = ::getsockname(socket, (struct sockaddr *)&addr, &addr_len);
        if (status)
        {
            LOG_ERROR_ERRNO("failed to get socket name", errno);
            last_err = errno;
            return status;
        }

        return 0;
    }

    int connection::set_close_on_exec(bool close)
    {
        last_err = 0;
        int flags = fcntl(socket, F_GETFD, 0);
        if (flags == -1) {
            last_err = errno;
            LOG_ERROR_ERRNO("failed to get socket flags", errno);
            return errno;
        }
 
        if (close)
        {
            flags |= FD_CLOEXEC;
        }
        else
        {
            flags &= ~FD_CLOEXEC;
        }

        if (fcntl(socket, F_SETFD, flags) == -1) {
            last_err = errno;
            LOG_ERROR_ERRNO("failed to set socket flags", errno);
            return errno;
        }    

        return 0;
    }

    int connection::set_blocking()
    {
        int er = 0;
        last_err = 0;
    
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0)
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("fcntl failed", er);
            return er;
        }

        flags &= ~O_NONBLOCK;

        flags = fcntl(socket, F_SETFL, flags) != -1;
        if (flags < 0)
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("fcntl failed", er);
        }
        return er;
    }

    int connection::set_nonblocking()
    {
        int er = 0;
        last_err = 0;
    
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0)
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("fcntl failed", er);
            return er;
        }

        flags |= O_NONBLOCK;

        flags = fcntl(socket, F_SETFL, flags) != -1;
        if (flags < 0)
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("fcntl failed", er);
        }
        return er;
    }

    int connection::reuse_addr(bool reuse)
    {
        int enable = reuse;
        int er = 0;
        last_err = 0;
        if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)))
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("setsockopt failed", er);
        }
        return er;
    }

    int connection::bind(int port)
    {
        // bind
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
    
        int er = 0;
        last_err = 0;
        if (::bind(socket, (struct sockaddr *)&addr, sizeof(addr)))
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("bind failed", er);
        }
        return er;
    }

    int connection::listen(int backlog)
    {
        int er = 0;
        last_err = 0;
        if (::listen(socket, backlog))
        {
            er = errno;
            last_err = errno;
            LOG_ERROR_ERRNO("listen failed", er);
        }
        return er;
    }

    int connection::accept(struct sockaddr_in & addr, socklen_t &addr_len, int flags)
    {
        std::unique_lock<std::mutex> lk(fd_lock);

        last_err = 0;
        int s = ::accept4(socket, (struct sockaddr *)&addr, &addr_len,
            flags);
        if (s < 0)
        {
            int er = errno;
            last_err = er;
            if (er != EAGAIN)
            {
                LOG_ERROR_ERRNO("accept failed", er);
            }
        }
        else
        {
            set_close_on_exec(true);
        }
        return s;
    }

    int connection::connect(const struct sockaddr * addr,
                            const socklen_t addr_len)
    {
        int er = 0;
        last_err = 0;
        int status = ::connect(socket, addr, addr_len);
        if (status)
        {
            er = errno;
            last_err = er;
            if (er != EINPROGRESS && er != EISCONN)
            {
                LOG_ERROR_ERRNO("connect failed", er);
            }
        }
        return er;
    }

    int connection::poll(std::vector<shared_poll_fd> & fds, int timeoutMs,
        int & err)
    {
        err = 0;

        std::vector<struct pollfd> in;
        in.resize(fds.size());
        int i = 0;

        std::for_each(fds.begin(), fds.end(),
                      [&in, &i](shared_poll_fd fd) {
                          struct pollfd ps;
                          std::memset(&ps, 0, sizeof(ps));
                          in[i].fd = fd.socket;
                          if (fd.read)
                          {
                              in[i].events |= POLLIN;
                              in[i].events |= POLLRDHUP;
                          }
                          if (fd.write)
                          {
                              in[i].events |= POLLOUT;
                          }
                          if (fd.error)
                          {
                              in[i].events |= POLLERR;
                              in[i].events |= POLLHUP;
                          }
                          i++;
                      });

        int status = ::poll(in.data(), in.size(), timeoutMs);
        if (status < 0)
        {
            int er = errno;
            err = er;
            if (er != EINTR)
            {
                LOG_ERROR_ERRNO("Poll failed", er);
            }
            return status;
        }

        for (size_t i=0; i<in.size(); i++)
        {
            auto & fd = fds[i];

            fd.read = in[i].revents & POLLIN || in[i].revents & POLLRDHUP;
            fd.write = in[i].revents & POLLOUT;
            fd.error = in[i].revents & POLLERR || in[i].revents & POLLHUP;
        }

        return status;
    }

    int connection::poll(bool & read, bool & write, bool & error, int timeoutMs)
    {
        last_err = 0;

        struct pollfd ps;
    
        std::memset(&ps, 0, sizeof(ps));
    
        ps.fd = socket;
        if (read)
        {
            ps.events |= POLLIN;
            ps.events |= POLLRDHUP;
        }
        if (write)
        {
            ps.events |= POLLOUT;
        }
        if (error)
        {
            ps.events |= POLLERR;
            ps.events |= POLLHUP;
        }
    
        read = write = error = false;

        int status = ::poll(&ps, 1, timeoutMs);
        if (status < 0)
        {
            int er = errno;
            last_err = er;
            if (er != EINTR)
            {
                LOG_ERROR_ERRNO("Poll failed", er);
            }
            return status;
        }

        read = ps.revents & POLLIN || ps.revents & POLLRDHUP;
        write = ps.revents & POLLOUT;
        error = ps.revents & POLLERR || ps.revents & POLLHUP;

        return status;
    }

    connection::CONNECTION_STATUS connection::read(char* buf, size_t length,
                                                   ssize_t & read)
    {
        last_err = 0;
        ssize_t r = recv(socket, buf, length, 0);
        // would block
        if (r < 0)
        {
            read = 0;
            if (errno == EAGAIN)
            {
                read_status = CONNECTION_WANTS_READ;
                return read_status;
            }
            else
            {
                last_err = errno;
                LOG_DEBUG_ERRNO("recv error on fd " << socket, last_err);
                read_status = CONNECTION_ERROR;
                return read_status;
            }
        }
        read = r;

        last_read = std::chrono::steady_clock::now();

        // incr counter
        read_counter += r;

        read_status = CONNECTION_OK;

        return read_status;
    }

    connection::CONNECTION_STATUS connection::write(const char* buf, size_t length,
                                                    ssize_t & written)
    {
        last_err = 0;
        ssize_t w = send(socket, buf, length, 0);
        if (w < 0)
        {
            written = 0;
            // would block
            if (errno == EAGAIN)
            {
                write_status = CONNECTION_WANTS_WRITE;
                return write_status;
            }
            // connection closed
            else if (errno == EPIPE)
            {
                last_err = errno;
                write_status = CONNECTION_CLOSED;
                return write_status;
            }
            // socket error
            else
            {
                last_err = errno;
                LOG_DEBUG_ERRNO("send error on fd " << socket, last_err);
                write_status = CONNECTION_ERROR;
                return write_status;
            }
        }

        written = w;

        last_write = std::chrono::steady_clock::now();

        // incr counter
        write_counter += w;

        // connection closed?
        if (w == 0)
        {
            write_status = CONNECTION_CLOSED;
        }
        // partial write
        else if (w < length)
        {
            write_status = CONNECTION_OK;
        }
        // full write
        else
        {
            write_status = CONNECTION_OK;
        }
        return write_status;
    }
}
