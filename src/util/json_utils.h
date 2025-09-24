#pragma once

#include <istream>
#include <jsoncpp/json/json.h>

namespace minerva
{
    bool parse_json(std::istream & str, Json::Value & value);
}
