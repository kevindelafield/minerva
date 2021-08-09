#pragma once

#include <memory>
#include <deque>
#include <vector>
#include <atomic>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <jsoncpp/json/json.h>
#include "log.h"

namespace owl
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
        int last_err;
        CONNECTION_STATUS accept_status;
        CONNECTION_STATUS read_status;
        CONNECTION_STATUS write_status;

    public:
        connection(int socket);
        connection(int family, int socktype, int protocol);
        virtual ~connection();

        std::chrono::steady_clock::time_point last_read;
        std::chrono::steady_clock::time_point last_write;

        std::deque<char> overflow;

        static Json::Value get_stats();

        static std::atomic<unsigned long long> shutdown_counter;
        static std::atomic<unsigned long long> open_counter;
        static std::atomic<unsigned long long> read_counter;
        static std::atomic<unsigned long long> write_counter;
        static std::atomic<unsigned long long> socket_counter;

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

        int get_local_addr(const struct sockaddr_in & addr, 
                           socklen_t &addr_len,
                           struct sockaddr_in & out_addr,
                           socklen_t & out_addr_len);

        int set_close_on_exec(bool close);

        int reuse_addr(bool reuse);

        int bind(int port);

        int listen(int backlog);
    
        int accept(struct sockaddr_in & addr, socklen_t &addr_len, int flags);

        bool data_available(bool & available);

        virtual CONNECTION_STATUS accept_ssl()
        {
            return CONNECTION_OK;
        }

        int set_blocking();
    
        int set_nonblocking();
    
        virtual int connect(const struct sockaddr * addr,
                            const socklen_t addr_len);

        int poll(bool & read, bool & write, bool & error, int timeoutMs);

        static int poll(std::vector<shared_poll_fd> & fds, int timeoutMs, int & err);

        virtual CONNECTION_STATUS shutdown()
        {
            return CONNECTION_OK;
        }

        inline int shutdown_write()
        {
            LOG_DEBUG("shutdown write: " << socket);
            write_status = CONNECTION_CLOSED;
            int status = ::shutdown(socket, SHUT_WR);
            if (status && errno != ENOTCONN)
            {
                LOG_DEBUG_ERRNO("shutdown write failed: " << socket, errno);
            }
            return status;
        }

        inline int shutdown_read()
        {
            LOG_DEBUG("shutdown read: " << socket);
            read_status = CONNECTION_CLOSED;
            int status = ::shutdown(socket, SHUT_RD);
            if (status && errno != ENOTCONN)
            {
                LOG_DEBUG_ERRNO("shutdown read failed: " << socket, errno);
            }
            return status;
        }

        inline int get_socket() const
        {
            return socket;
        }

        inline int get_last_error() const
        {
            return last_err;
        }

        inline CONNECTION_STATUS get_read_status() const
        {
            return read_status;
        }

        inline CONNECTION_STATUS get_write_status() const
        {
            return write_status;
        }
    };
}