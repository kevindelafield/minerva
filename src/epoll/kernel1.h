#pragma once

#include <unordered_map>
#include <atomic>
#include <memory>
#include <list>
#include <vector>
#include <chrono>
#include <cassert>
#include <mutex>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <ovhttpd/thread_pool.h>
#include <ovhttpd/time_utils.h>
#include <ovhttpd/connection.h>
#include "name_resolver.h"
#include "spin_lock.h"
#include "hybrid_lock.h"
#include "kernel.h"

namespace epoll
{

    class kernel1_read_state
    {
    public:
        
        constexpr static int MAX_HEADER_SIZE = 1024 * 10;

    kernel1_read_state(std::shared_ptr<ovhttpd::connection> accepted_socket,
                       const char * buf, int length) :
        accepted_socket(accepted_socket), _timer()
        {
            buffer.insert(buffer.end(), buf, buf + length);
        }

        ~kernel1_read_state()
        {
        }

        long long elapsed_ms() const
        {
            return _timer.get_elapsed_milliseconds();
        }

        const std::shared_ptr<ovhttpd::connection> accepted_socket;
        const ovhttpd::timer _timer;
        std::vector<char> buffer;
    };

    class kernel1_write_state
    {
    public:

    kernel1_write_state(std::shared_ptr<ovhttpd::connection> accepted_socket,
                        const char* buf, int written,
                        const name_resolver& address,
                        const std::shared_ptr<std::vector<char>> header,
                        const std::string &host, const int port) :
        accepted_socket(accepted_socket),
            buf(buf), written(written), total_len(strlen(buf)),
            _timer(), address(address), connect(true),
            header(header), host(host), port(port)
        {
            assert(written > -1);
            assert(buf);
        }
    
    kernel1_write_state(std::shared_ptr<ovhttpd::connection> accepted_socket,
                        const char* buf, int written) :
        accepted_socket(accepted_socket),
            buf(buf), written(written), total_len(strlen(buf)),
            _timer(), connect(false), port(-1)
        {
            assert(written > -1);
            assert(buf);
        }

        ~kernel1_write_state() = default;

        long long elapsed_ms() const
        {
            return _timer.get_elapsed_milliseconds();
        }

        const std::shared_ptr<ovhttpd::connection> accepted_socket;
        const ovhttpd::timer _timer;
        const char* buf;
        const int total_len;
        int written;
        const bool connect;
        const name_resolver address;
        const std::shared_ptr<std::vector<char>> header;
        const std::string host;
        const int port;
    };

    class kernel1_connect_state
    {
    public:
    kernel1_connect_state(std::shared_ptr<ovhttpd::connection> accepted_socket,
                          std::shared_ptr<ovhttpd::connection> connect_socket,
                          const name_resolver& address,
                          std::shared_ptr<std::vector<char>> header,
                          const std::string &host, const int port) :
        accepted_socket(accepted_socket), connect_socket(connect_socket),
            address(address), host(host), port(port),
            header(header), _timer()
        {
        }

        ~kernel1_connect_state()
        {
        }

        long long elapsed_ms() const
        {
            return _timer.get_elapsed_milliseconds();
        }

        const name_resolver address;
        const std::shared_ptr<ovhttpd::connection> accepted_socket;
        const std::shared_ptr<ovhttpd::connection> connect_socket;
        const std::shared_ptr<std::vector<char>> header;
        const std::string host;
        const int port;
        const ovhttpd::timer _timer;
    };

    class kernel3;

    class kernel1 : public kernel
    {
    private:

        constexpr static const char* crlfcrlf = "\r\n\r\n";
        constexpr static const char* connect_header_regex =
            "^CONNECT\\s+([^:]+):(\\d+)\\s+HTTP/(1.0|1.1)";
        constexpr static const char* header_regex =
            "^(.*)\\s+(.+)\\s+HTTP/(1.0|1.1)";
        constexpr static const char* header_200_response =
            "HTTP/1.1 200 OK\r\n\r\n";
        constexpr static const char* header_400_response =
            "HTTP/1.1 400 Bad Request\r\n\r\n";
        constexpr static const char* header_404_response =
            "HTTP/1.1 403 Not Found\r\n\r\n";
        constexpr static const char* header_200_10_response =
            "HTTP/1.0 200 OK\r\n\r\n";
        constexpr static const char* header_400_10_response =
            "HTTP/1.0 400 Bad Request\r\n\r\n";
        constexpr static const char* header_404_10_response =
            "HTTP/1.0 403 Not Found\r\n\r\n";
        constexpr static const char* empty_string = "";
        constexpr static const char* port_80_string = "80";

        typedef std::tuple<std::chrono::steady_clock::time_point, std::shared_ptr<ovhttpd::connection>> shutdown_entry_t;

        std::list<shutdown_entry_t> shutdown_list;
        std::mutex shutdown_lock;

        std::vector<struct sockaddr> local_addresses;

        std::shared_ptr<ovhttpd::thread_pool> dns_tpool;
        std::shared_ptr<ovhttpd::thread_pool> read_tpool;
        std::shared_ptr<ovhttpd::thread_pool> write_tpool;
        std::shared_ptr<ovhttpd::thread_pool> connect_tpool;

        bool ipv6_support = false;
        bool ipv4_support = false;

        bool same_address(struct sockaddr & addr, int port);

        void run_close_job();

        void schedule_close(const std::shared_ptr<ovhttpd::connection> connection);

        const int THREAD_COUNT = 100;
        const int SHUTDOWN_JOB_SECONDS = 5;
        const int EPOLL_EVENT_SIZE = 100;

        int listen_port;

        std::mutex lock;
    
        std::atomic<unsigned long long> accept_counter;
        std::atomic<unsigned long long> accept_fail_counter;
        std::atomic<unsigned long long> connect_counter;
        std::atomic<unsigned long long> connect_fail_counter;
        std::atomic<unsigned long long> name_fail_counter;

        int epoll_accept_fd;
        int epoll_connect_fd;
        int epoll_read_fd;
        int epoll_write_fd;

        bool parse_header(const std::string& header,
                          std::string& hostname, std::string& port) const;

        std::unordered_map<int, std::shared_ptr<kernel1_connect_state>> connect_map;
        std::unordered_map<int, std::shared_ptr<kernel1_read_state>> read_map;
        std::unordered_map<int, std::shared_ptr<kernel1_write_state>> write_map;

        ovhttpd::connection listen_socket;

        void accept_handler();
        void connect_handler();
        void read_handler();
        void write_handler();

        void notify_connect(int fd, int events);
        void notify_read(int fd, int events);
        void notify_write(int fd, int events);

        void process_header(std::shared_ptr<ovhttpd::connection> accepted_socket,
                            const char * buf, const int size, const int index);

        void try_connect(std::shared_ptr<ovhttpd::connection> accepted_socket,
                         const name_resolver& helper,
                         const std::shared_ptr<std::vector<char>> buf,
                         const std::string& host,
                         const int port);
        void retry_connect(std::shared_ptr<ovhttpd::connection> connection,
                           std::shared_ptr<kernel1_connect_state> state);

        void try_read_header(std::shared_ptr<ovhttpd::connection> connection);

        void retry_read_header(std::shared_ptr<kernel1_read_state> state);

        void try_write_header_200_response(std::shared_ptr<ovhttpd::connection> accepted_socket,
                                           const name_resolver& address,
                                           const std::shared_ptr<std::vector<char>> header,
                                           const bool http11,
                                           const std::string& host,
                                           const int port);
        void try_write_header_400_response(std::shared_ptr<ovhttpd::connection> accepted_socket,
                                           const char* header);
        void retry_write_header(std::shared_ptr<kernel1_write_state> state);

        void handle_accept(int s);

        std::shared_ptr<kernel3> next;

    public:
        kernel1();
        virtual ~kernel1();

        constexpr static const char * NAME = "kernel1";

        const char * name() override
        {
            return NAME;
        }

        void set_listen_port(int port);

        void initialize() override;

        void start() override;

        void release() override;

        void dump_stats() override;

        Json::Value get_stats() override;
    };
}
