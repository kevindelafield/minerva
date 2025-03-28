#include "json_utils.h"
#include "log.h"

namespace minerva
{

    bool parse_json(std::istream & str, Json::Value & value)
    {
        try
        {
            str >> value;
            return true;
        }
        catch (std::exception & e)
        {
            LOG_WARN("Failed to parse JSON: " << e.what());
            return false;
        }

    }
}
