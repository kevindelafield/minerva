#include <string>
#include <cassert>
#include <iostream>
#include "settings.h"

namespace www
{

    bool settings::parse_command_line(int argc, char** argv)
    {
        for (int i=1; i<argc; i++)
        {
            std::string option(argv[i]);
            if (option == "-c")
            {
                i++;
                if (i==argc)
                {
                    return false;
                }
                config_file = argv[i];
            }
            else if (option == "-l")
            {
                i++;
                if (i==argc)
                {
                    return false;
                }
                std::string log_str(argv[i]);
                log_level = (owl::log::LOG_LEVEL)std::stoi(log_str);
            }
            else if (option == "-v")
            {
                print_version = true;
            }
            else if (option == "-pw")
            {
                i++;
                if (i==argc)
                {
                    return false;
                }
                pw_override = argv[i];
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
        std::cout << "usage: www -c config_file [-l log_level]" << std::endl;
    }
}
