#pragma once

#include <unistd.h>
#include <string>
#include <fstream>
#include <sstream>
#include "guid.h"

namespace util
{
    class safe_ofstream : public std::ofstream
    {
    public:

    safe_ofstream(const std::string & filename) : std::ofstream(),
            m_realpath(filename)
        {
            std::stringstream ss;
            ss << filename;
            ss << ".";
            ss << new_guid();

            m_fakepath = ss.str();

            open();
        }
        
        ~safe_ofstream();
        
        bool commit();
        
    private:
        void open(std::ios_base::openmode mode = std::ios_base::out)
        {
            std::ofstream::open(m_fakepath.c_str(), mode);
            if (is_open())
            {
                m_open = true;
            }
        }

        std::string m_fakepath;
        std::string m_realpath;
        bool m_commited = false;
        bool m_open = false;
    };

}
