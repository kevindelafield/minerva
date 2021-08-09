#pragma once

#include <ovhttpd/log.h>

namespace epoll
{

    class settings
    {
    public:
    settings() : proxy_listen_port(8081), log_level(ovhttpd::log::INFO)
        {
        }
        virtual ~settings()
        {
        }
    
        bool parse_command_line(int argc, char** argv);

        static void print_usage();

        int proxy_listen_port;
        ovhttpd::log::LOG_LEVEL log_level;
    };
}
