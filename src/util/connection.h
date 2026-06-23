#pragma once

#include <memory>
#include <deque>
#include <vector>
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <util/log.h>

namespace minerva
{

    class connection
    {

    public:
        enum CONNECTION_STATUS
        {
            CONNECTION_ERROR = 1,
            CONNECTION_OK = 2,
            CONNECTION_WANTS_READ = 3,
            CONNECTION_WANTS_WRITE = 4,
            CONNECTION_CLOSED = 5,
        };

    protected:
        int socket;

    public:
        connection(int socket);
        connection(int family, int socktype, int protocol);
        virtual ~connection();

        // Move-only: a connection owns a single fd; copying via dup() would
        // give two objects independent overflow buffers and timestamps for
        // the same TCP stream, which is never what callers want.
        connection(const connection & conn) = delete;
        connection & operator=(const connection & conn) = delete;

        connection(connection && conn) noexcept;
        connection & operator=(connection && conn) noexcept;

        std::chrono::steady_clock::time_point last_read;
        std::chrono::steady_clock::time_point last_write;

        std::deque<char> overflow;

        virtual CONNECTION_STATUS read(char* buf, size_t length, ssize_t & read);

        virtual CONNECTION_STATUS write(const char* buf, size_t length, ssize_t & written);

        class shared_poll_fd
        {
        public:

        shared_poll_fd(int socket, bool read, bool write, bool error)
        : socket(socket), read(read), write(write), error(error)
            {
            }

            shared_poll_fd(const shared_poll_fd & other) = default;

            shared_poll_fd & operator= (const shared_poll_fd & other) = default;

            int socket;
            bool read;
            bool write;
            bool error;
        };

        bool get_local_addr(const struct sockaddr_storage & addr,
                           socklen_t &addr_len,
                           struct sockaddr_storage & out_addr,
                           socklen_t & out_addr_len);

        bool set_close_on_exec(bool close);

        bool reuse_addr(bool reuse);

        bool reuse_addr6(bool reuse);

        bool ipv6_only(bool only);

        bool bind(const struct sockaddr * addr, socklen_t len);

        bool listen(int backlog);
    
        bool accept(struct sockaddr_storage & addr, socklen_t &addr_len, int flags,
                    int & s);

        bool data_available(bool & available);

        virtual CONNECTION_STATUS accept_ssl()
        {
            return CONNECTION_OK;
        }

        bool set_blocking(bool block);
    
        virtual bool connect(const struct sockaddr * addr,
                             const socklen_t addr_len);

        int poll(bool & read, bool & write, bool & error, int timeoutMs);

        static int poll(std::vector<shared_poll_fd> & fds, int timeoutMs, int & err);

        virtual CONNECTION_STATUS shutdown()
        {
            return CONNECTION_OK;
        }

        inline bool shutdown_write()
        {
            LOG_DEBUG("shutdown write: " << socket);
            int status = ::shutdown(socket, SHUT_WR);
            if (status && errno != ENOTCONN)
            {
                LOG_DEBUG_ERRNO("shutdown write failed: " << socket, errno);
                return false;
            }
            return true;
        }

        inline bool shutdown_read()
        {
            LOG_DEBUG("shutdown read: " << socket);
            int status = ::shutdown(socket, SHUT_RD);
            if (status && errno != ENOTCONN)
            {
                LOG_DEBUG_ERRNO("shutdown read failed: " << socket, errno);
                return false;
            }
            return true;
        }

        inline int get_socket() const
        {
            return socket;
        }
    };
}
