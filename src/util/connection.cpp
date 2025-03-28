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
#include <util/log.h>
#include "connection.h"

namespace minerva
{

    connection::connection(int family, int socktype, int protocol) :
        last_read(std::chrono::steady_clock::now()),
        last_write(std::chrono::steady_clock::now())
    {
        int s = ::socket(family, socktype, protocol);
        if (s == -1)
        {
            FATAL_ERRNO("socket create failed", errno);
        }
        socket = s;
    }

    connection::connection(int socket) :
        socket(socket)
    {
    }

    connection::connection(const connection & conn) :
        socket(dup(conn.get_socket()))
    {
        if (socket < 0)
        {
            FATAL_ERRNO("failed to duplicate socket", errno);
        }
    }

    connection::connection(connection && conn) :
        socket(dup(conn.get_socket()))
    {
        if (socket < 0)
        {
            FATAL_ERRNO("failed to duplicate socket", errno);
        }

        int status = close(conn.socket);
        if (status)
        {
            FATAL_ERRNO("error closing socket: " << socket, errno);
        }
        conn.socket = -1;
    }

    connection & connection::operator=(const connection & conn)
    {
        if (this != &conn)
        {
            socket = dup(conn.get_socket());
            if (socket < 0)
            {
                FATAL_ERRNO("failed to duplicate socket", errno);
            }
        }
        return *this;
    }

    connection & connection::operator=(connection && conn)
    {
        if (this != &conn)
        {
            socket = dup(conn.get_socket());
            if (socket < 0)
            {
                FATAL_ERRNO("failed to duplicate socket", errno);
            }
            int status = close(conn.socket);
            if (status)
            {
                FATAL_ERRNO("error closing socket: " << socket, errno);
            }
            conn.socket = -1;
        }
        return *this;
    }

    connection::~connection()
    {
        if (socket > -1)
        {
            int status = close(socket);
            if (status)
            {
                FATAL_ERRNO("error closing socket: " << socket, errno);
            }
        }
    }

    bool connection::data_available(bool & available)
    {
        // ioctl(fd,FIONREAD,&bytes_available)

        char buf;

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

    bool connection::get_local_addr(const struct sockaddr_in & client_addr, 
                                    socklen_t &client_addr_len,
                                    struct sockaddr_in & addr,
                                    socklen_t & addr_len)
    {
        int status = ::getsockname(socket, (struct sockaddr *)&addr, &addr_len);
        if (status)
        {
            LOG_ERROR_ERRNO("failed to get socket name", errno);
            return false;
        }

        return true;
    }

    bool connection::set_close_on_exec(bool close)
    {
        int flags = fcntl(socket, F_GETFD, 0);
        if (flags == -1) {
            LOG_ERROR_ERRNO("failed to get socket flags", errno);
            return false;
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
            LOG_ERROR_ERRNO("failed to set socket flags", errno);
            return false;
        }    

        return true;
    }

    bool connection::set_blocking(bool block)
    {
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0)
        {
            LOG_ERROR_ERRNO("fcntl failed", errno);
            return false;
        }

        if (block)
        {
            flags &= ~O_NONBLOCK;
        }
        else
        {
            flags |= O_NONBLOCK;
        }

        flags = fcntl(socket, F_SETFL, flags) != -1;
        if (flags < 0)
        {
            LOG_ERROR_ERRNO("fcntl failed", errno);
            return false;
        }
        return true;
    }

    bool connection::reuse_addr(bool reuse)
    {
        int enable = reuse;
        if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)))
        {
            LOG_ERROR_ERRNO("setsockopt failed", errno);
            return false;
        }
        return true;
    }

    bool connection::reuse_addr6(bool reuse)
    {
        int enable = reuse;
        if (setsockopt(socket, SOL_IPV6, SO_REUSEADDR, &enable, sizeof(enable)))
        {
            LOG_ERROR_ERRNO("setsockopt failed", errno);
            return false;
        }
        return true;
    }

    bool connection::ipv6_only(bool only)
    {
        int enable = only;
        if (setsockopt(socket, SOL_IPV6, IPV6_V6ONLY, &enable, sizeof(enable)))
        {
            LOG_ERROR_ERRNO("setsockopt failed", errno);
            return false;
        }
        return true;
    }

    bool connection::bind(const struct sockaddr * addr, socklen_t len)
    {
        // bind
        if (::bind(socket, addr, len))
        {
            LOG_ERROR_ERRNO("bind failed", errno);
            return false;
        }
        return true;
    }

    bool connection::listen(int backlog)
    {
        if (::listen(socket, backlog))
        {
            LOG_ERROR_ERRNO("listen failed", errno);
            return false;
        }
        return true;
    }

    bool connection::accept(struct sockaddr_in & addr, socklen_t &addr_len, int flags, int & sock)
    {
        int s = ::accept4(socket, (struct sockaddr *)&addr, &addr_len,
            flags);
        if (s < 0)
        {
            if (errno != EAGAIN)
            {
                LOG_ERROR_ERRNO("accept failed", errno);
            }
            return false;
        }
        sock = s;
        return true;
    }

    bool connection::connect(const struct sockaddr * addr,
                             const socklen_t addr_len)
    {
        int status = ::connect(socket, addr, addr_len);
        if (status)
        {
            if (errno != EINPROGRESS && errno != EISCONN)
            {
                LOG_ERROR_ERRNO("connect failed", errno);
            }
            return false;
        }
        return true;
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
            if (errno != EINTR)
            {
                LOG_ERROR_ERRNO("Poll failed", errno);
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
        ssize_t r = recv(socket, buf, length, 0);
        // would block
        if (r < 0)
        {
            read = 0;
            if (errno == EAGAIN)
            {
                return CONNECTION_WANTS_READ;
            }
            else
            {
                LOG_DEBUG_ERRNO("recv error on fd " << socket, errno);
                return CONNECTION_ERROR;
            }
        }
        read = r;

        last_read = std::chrono::steady_clock::now();

        return CONNECTION_OK;
    }

    connection::CONNECTION_STATUS connection::write(const char* buf, size_t length,
                                                    ssize_t & written)
    {
        ssize_t w = send(socket, buf, length, 0);
        if (w < 0)
        {
            written = 0;
            // would block
            if (errno == EAGAIN)
            {
                return CONNECTION_WANTS_WRITE;
            }
            // connection closed
            else if (errno == EPIPE)
            {
                return CONNECTION_CLOSED;
            }
            // socket error
            else
            {
                LOG_DEBUG_ERRNO("send error on fd " << socket, errno);
                return CONNECTION_ERROR;
            }
        }

        written = w;

        last_write = std::chrono::steady_clock::now();

        return CONNECTION_OK;
    }
}
