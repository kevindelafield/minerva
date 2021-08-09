#include <string>
#include <cassert>
#include <iostream>
#include "settings.h"

namespace epoll
{

    bool settings::parse_command_line(int argc, char** argv)
    {
        for (int i=1; i<argc; i++)
        {
            std::string option(argv[i]);
            if (option == "-p")
            {
                i++;
                if (i==argc)
                {
                    return false;
                }
                std::string port_str(argv[i]);
                proxy_listen_port = std::stoi(port_str);
            }
            else if (option == "-l")
            {
                i++;
                if (i==argc)
                {
                    return false;
                }
                std::string log_str(argv[i]);
                log_level = (ovhttpd::log::LOG_LEVEL)std::stoi(log_str);
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    void settings::print_usage()
    {
        std::cout << "usage: epoll [-p proxy_port] [-l log_level]" << std::endl;
    }
}
