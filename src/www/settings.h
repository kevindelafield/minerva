#pragma once

#include <string>
#include <util/log.h>

namespace www
{

    class settings
    {
    public:
    settings() : config_file("/www/config/config.json")
        {
        }
        virtual ~settings()
        {
        }
    
        bool parse_command_line(int argc, char** argv);

        static void print_usage();

        std::string pw_override;
        std::string config_file;
        util::log::LOG_LEVEL log_level = util::log::INFO;
        bool print_version = false;
    };
}
