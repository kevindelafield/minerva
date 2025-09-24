#pragma once

#include <vector>
#include <string>

namespace minerva
{
    std::string to64frombits(const std::vector<unsigned char> & in);

    std::string to64frombits(const std::string & in);

    bool from64tobits(const std::string & in, std::string & out);
}
