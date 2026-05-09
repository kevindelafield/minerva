#include "json_utils.h"
#include "log.h"
#include <memory>

namespace minerva
{
    bool parse_json(std::istream & str, Json::Value & value)
    {
        value = Json::Value();

        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, str, &value, &errors))
        {
            value = Json::Value();
            LOG_DEBUG("Failed to parse JSON from stream: " << errors);
            return false;
        }
        return true;
    }

    bool parse_json(const std::string & json_str, Json::Value & value,
                    std::string * error_msg)
    {
        value = Json::Value();

        if (json_str.empty())
        {
            if (error_msg) *error_msg = "Empty JSON string";
            return false;
        }

        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

        std::string errors;
        if (!reader->parse(
                json_str.c_str(),
                json_str.c_str() + json_str.size(),
                &value,
                &errors))
        {
            value = Json::Value();
            if (error_msg) *error_msg = errors;
            LOG_DEBUG("Failed to parse JSON string: " << errors);
            return false;
        }
        return true;
    }

    std::string to_json_string(const Json::Value & value, bool pretty)
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = pretty ? "  " : "";
        return Json::writeString(builder, value);
    }
}
