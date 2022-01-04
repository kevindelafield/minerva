#pragma once

#include <istream>
#include <jsoncpp/json/json.h>

namespace util
{

    bool parse_json(std::istream & str, Json::Value & value);

}
