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
        socket(socket),
        last_read(std::chrono::steady_clock::now()),
        last_write(std::chrono::steady_clock::now())
    {
    }

    connection::connection(connection && conn) noexcept :
        socket(conn.socket),
        last_read(conn.last_read),
        last_write(conn.last_write),
        overflow(std::move(conn.overflow))
    {
        conn.socket = -1;  // Invalidate source socket (transfer ownership)
    }

    connection & connection::operator=(connection && conn) noexcept
    {
        if (this != &conn)
        {
            // Close existing socket first to prevent leak
            if (socket >= 0)
            {
                if (close(socket) != 0)
                {
                    LOG_ERROR_ERRNO("error closing socket: " << socket, errno);
                }
            }

            // Transfer ownership
            socket = conn.socket;
            last_read = conn.last_read;
            last_write = conn.last_write;
            overflow = std::move(conn.overflow);
            conn.socket = -1;
        }
        return *this;
    }

    connection::~connection()
    {
        if (socket >= 0)
        {
            if (close(socket) != 0)
            {
                // Don't FATAL: on Linux the fd is released regardless, and
                // EINTR/EIO from a previously buffered write should not
                // terminate the process during shutdown.
                LOG_ERROR_ERRNO("error closing socket: " << socket, errno);
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
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available right now (non-blocking socket)
                available = false;
                return true;
            }
            else
            {
                LOG_ERROR_ERRNO("failed to peek on socket", errno);
                available = false;
                return false;
            }
        }
        else if (r == 0)
        {
            // Connection closed by peer
            available = false;
            return false;  // Indicate error condition - connection is closed
        }

        // r > 0: data is available
        available = true;
        return true;
    }

    bool connection::get_local_addr(const struct sockaddr_storage & client_addr,
                                    socklen_t &client_addr_len,
                                    struct sockaddr_storage & addr,
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
            LOG_ERROR_ERRNO("fcntl F_GETFL failed", errno);
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

        if (fcntl(socket, F_SETFL, flags) == -1)
        {
            LOG_ERROR_ERRNO("fcntl F_SETFL failed", errno);
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
        // SO_REUSEADDR lives at SOL_SOCKET, not SOL_IPV6.  This is the same
        // option as reuse_addr() but kept as a separate method for callers
        // operating on IPv6 sockets that want a distinct call site.
        int enable = reuse;
        if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)))
        {
            LOG_ERROR_ERRNO("setsockopt SO_REUSEADDR failed", errno);
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

    bool connection::accept(struct sockaddr_storage & addr, socklen_t &addr_len, int flags, int & sock)
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

        std::vector<struct pollfd> in(fds.size());
        for (size_t i = 0; i < fds.size(); ++i)
        {
            std::memset(&in[i], 0, sizeof(in[i]));
            in[i].fd = fds[i].socket;
            if (fds[i].read)
            {
                in[i].events |= POLLIN;
                in[i].events |= POLLRDHUP;
            }
            if (fds[i].write)
            {
                in[i].events |= POLLOUT;
            }
            if (fds[i].error)
            {
                in[i].events |= POLLERR;
                in[i].events |= POLLHUP;
            }
        }

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

            fd.read = (in[i].revents & (POLLIN | POLLRDHUP)) != 0;
            fd.write = (in[i].revents & POLLOUT) != 0;
            fd.error = (in[i].revents & (POLLERR | POLLHUP)) != 0;
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

        read = (ps.revents & (POLLIN | POLLRDHUP)) != 0;
        write = (ps.revents & POLLOUT) != 0;
        error = (ps.revents & (POLLERR | POLLHUP)) != 0;

        return status;
    }

    connection::CONNECTION_STATUS connection::read(char* buf, size_t length,
                                                   ssize_t & read)
    {
        // Handle zero-length read request
        if (length == 0)
        {
            read = 0;
            return CONNECTION_OK;
        }

        ssize_t r;
        do {
            r = recv(socket, buf, length, 0);
        } while (r < 0 && errno == EINTR);

        if (r < 0)
        {
            read = 0;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return CONNECTION_WANTS_READ;
            }
            else if (errno == ECONNRESET || errno == ENOTCONN ||
                     errno == EPIPE)
            {
                return CONNECTION_CLOSED;
            }
            else
            {
                LOG_DEBUG_ERRNO("recv error on fd " << socket, errno);
                return CONNECTION_ERROR;
            }
        }
        else if (r == 0)
        {
            // Peer closed connection (EOF)
            read = 0;
            return CONNECTION_CLOSED;
        }

        // r > 0: successful read
        read = r;
        last_read = std::chrono::steady_clock::now();
        return CONNECTION_OK;
    }

    connection::CONNECTION_STATUS connection::write(const char* buf, size_t length,
                                                    ssize_t & written)
    {
        // MSG_NOSIGNAL prevents SIGPIPE if the peer has closed the read side.
        ssize_t w;
        do {
            w = send(socket, buf, length, MSG_NOSIGNAL);
        } while (w < 0 && errno == EINTR);

        if (w < 0)
        {
            written = 0;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return CONNECTION_WANTS_WRITE;
            }
            else if (errno == EPIPE || errno == ECONNRESET ||
                     errno == ENOTCONN)
            {
                return CONNECTION_CLOSED;
            }
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
