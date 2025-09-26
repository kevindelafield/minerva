#include "json_utils.h"
#include "log.h"
#include <sstream>
#include <vector>

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
            LOG_WARN("Failed to parse JSON from stream: " << e.what());
            return false;
        }
    }

    bool parse_json(const std::string & json_str, Json::Value & value, std::string* error_msg)
    {
        if (json_str.empty()) {
            if (error_msg) *error_msg = "Empty JSON string";
            return false;
        }

        Json::CharReaderBuilder builder;
        Json::CharReader* reader = builder.newCharReader();
        
        std::string errors;
        bool success = reader->parse(
            json_str.c_str(),
            json_str.c_str() + json_str.size(),
            &value,
            &errors
        );
        
        delete reader;
        
        if (!success) {
            if (error_msg) *error_msg = errors;
            LOG_WARN("Failed to parse JSON string: " << errors);
            return false;
        }
        
        return true;
    }

    std::string to_json_string(const Json::Value & value, bool pretty)
    {
        if (pretty) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";  // 2-space indentation
            std::ostringstream stream;
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(value, &stream);
            return stream.str();
        } else {
            Json::FastWriter writer;
            std::string result = writer.write(value);
            // FastWriter adds a trailing newline, remove it
            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }
            return result;
        }
    }

    std::string get_string(const Json::Value & obj, const std::string & key, const std::string & default_value)
    {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isString()) {
            return default_value;
        }
        return obj[key].asString();
    }

    int get_int(const Json::Value & obj, const std::string & key, int default_value)
    {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isInt()) {
            return default_value;
        }
        return obj[key].asInt();
    }

    double get_double(const Json::Value & obj, const std::string & key, double default_value)
    {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isNumeric()) {
            return default_value;
        }
        return obj[key].asDouble();
    }

    bool get_bool(const Json::Value & obj, const std::string & key, bool default_value)
    {
        if (!obj.isObject() || !obj.isMember(key) || !obj[key].isBool()) {
            return default_value;
        }
        return obj[key].asBool();
    }

    bool has_key_of_type(const Json::Value & obj, const std::string & key, Json::ValueType expected_type)
    {
        if (!obj.isObject() || !obj.isMember(key)) {
            return false;
        }
        return obj[key].type() == expected_type;
    }

    Json::Value merge_objects(const Json::Value & obj1, const Json::Value & obj2)
    {
        if (!obj1.isObject() || !obj2.isObject()) {
            LOG_WARN("merge_objects: Both parameters must be JSON objects");
            return obj1.isObject() ? obj1 : Json::Value(Json::objectValue);
        }
        
        Json::Value result = obj1;  // Copy obj1
        
        // Iterate through obj2 and add/overwrite keys
        for (const std::string& key : obj2.getMemberNames()) {
            result[key] = obj2[key];
        }
        
        return result;
    }

    bool validate_required_keys(const Json::Value & obj, 
                               const std::vector<std::string> & required_keys,
                               std::vector<std::string>* missing_keys)
    {
        if (!obj.isObject()) {
            if (missing_keys) {
                *missing_keys = required_keys;  // All keys are "missing" if not an object
            }
            return false;
        }
        
        bool all_present = true;
        if (missing_keys) {
            missing_keys->clear();
        }
        
        for (const std::string& key : required_keys) {
            if (!obj.isMember(key)) {
                all_present = false;
                if (missing_keys) {
                    missing_keys->push_back(key);
                }
            }
        }
        
        return all_present;
    }

    Json::Value get_nested(const Json::Value & obj, const std::string & path)
    {
        if (path.empty()) {
            return obj;
        }
        
        Json::Value current = obj;
        std::istringstream path_stream(path);
        std::string key;
        
        while (std::getline(path_stream, key, '.')) {
            if (!current.isObject() || !current.isMember(key)) {
                return Json::Value(); // Return null value
            }
            current = current[key];
        }
        
        return current;
    }

    bool set_nested(Json::Value & obj, const std::string & path, const Json::Value & value)
    {
        if (path.empty()) {
            return false;
        }
        
        // Ensure obj is an object
        if (!obj.isObject()) {
            obj = Json::Value(Json::objectValue);
        }
        
        std::istringstream path_stream(path);
        std::string key;
        Json::Value* current = &obj;
        
        // Navigate to the parent of the final key
        while (std::getline(path_stream, key, '.')) {
            // Check if this is the last key
            if (path_stream.eof()) {
                // This is the final key, set the value
                (*current)[key] = value;
                return true;
            }
            
            // This is an intermediate key, ensure it's an object
            if (!(*current)[key].isObject()) {
                (*current)[key] = Json::Value(Json::objectValue);
            }
            current = &((*current)[key]);
        }
        
        return false; // Should never reach here
    }

}
