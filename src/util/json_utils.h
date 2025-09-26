#pragma once

#include <istream>
#include <string>
#include <jsoncpp/json/json.h>

namespace minerva
{
    /**
     * Parse JSON from an input stream.
     * 
     * @param str Input stream containing JSON data
     * @param value Output Json::Value object
     * @return true if parsing succeeded, false otherwise
     */
    bool parse_json(std::istream & str, Json::Value & value);

    /**
     * Parse JSON from a string.
     * 
     * @param json_str String containing JSON data
     * @param value Output Json::Value object
     * @param error_msg Optional output parameter for error message
     * @return true if parsing succeeded, false otherwise
     */
    bool parse_json(const std::string & json_str, Json::Value & value, std::string* error_msg = nullptr);

    /**
     * Convert a Json::Value to a formatted string.
     * 
     * @param value Json::Value to convert
     * @param pretty If true, format with indentation and newlines
     * @return JSON string representation
     */
    std::string to_json_string(const Json::Value & value, bool pretty = false);

    /**
     * Safely get a string value from a JSON object.
     * 
     * @param obj JSON object
     * @param key Key to look up
     * @param default_value Default value if key doesn't exist or isn't a string
     * @return String value or default
     */
    std::string get_string(const Json::Value & obj, const std::string & key, const std::string & default_value = "");

    /**
     * Safely get an integer value from a JSON object.
     * 
     * @param obj JSON object
     * @param key Key to look up
     * @param default_value Default value if key doesn't exist or isn't an integer
     * @return Integer value or default
     */
    int get_int(const Json::Value & obj, const std::string & key, int default_value = 0);

    /**
     * Safely get a double value from a JSON object.
     * 
     * @param obj JSON object
     * @param key Key to look up
     * @param default_value Default value if key doesn't exist or isn't a number
     * @return Double value or default
     */
    double get_double(const Json::Value & obj, const std::string & key, double default_value = 0.0);

    /**
     * Safely get a boolean value from a JSON object.
     * 
     * @param obj JSON object
     * @param key Key to look up
     * @param default_value Default value if key doesn't exist or isn't a boolean
     * @return Boolean value or default
     */
    bool get_bool(const Json::Value & obj, const std::string & key, bool default_value = false);

    /**
     * Check if a JSON object has a key of the expected type.
     * 
     * @param obj JSON object
     * @param key Key to check
     * @param expected_type Expected JSON value type
     * @return true if key exists and has the expected type
     */
    bool has_key_of_type(const Json::Value & obj, const std::string & key, Json::ValueType expected_type);

    /**
     * Merge two JSON objects (shallow merge).
     * Values in obj2 will overwrite values in obj1 for duplicate keys.
     * 
     * @param obj1 Base JSON object
     * @param obj2 JSON object to merge into obj1
     * @return Merged JSON object
     */
    Json::Value merge_objects(const Json::Value & obj1, const Json::Value & obj2);

    /**
     * Create a JSON object from key-value pairs.
     * Template function for convenience.
     * 
     * Usage: auto obj = create_object("name", "John", "age", 30, "active", true);
     */
    template<typename... Args>
    Json::Value create_object(Args&&... args);

    // Implementation of template function
    namespace detail {
        inline void add_to_object(Json::Value&) {}
        
        template<typename T, typename... Args>
        void add_to_object(Json::Value& obj, const std::string& key, T&& value, Args&&... args) {
            obj[key] = std::forward<T>(value);
            add_to_object(obj, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    Json::Value create_object(Args&&... args) {
        static_assert(sizeof...(args) % 2 == 0, "create_object requires an even number of arguments (key-value pairs)");
        Json::Value obj(Json::objectValue);
        detail::add_to_object(obj, std::forward<Args>(args)...);
        return obj;
    }

    /**
     * Create a JSON array from values.
     * 
     * Usage: auto arr = create_array(1, "hello", true, 3.14);
     */
    template<typename... Args>
    Json::Value create_array(Args&&... args);

    // Implementation of create_array template
    namespace detail {
        inline void add_to_array(Json::Value&) {}
        
        template<typename T, typename... Args>
        void add_to_array(Json::Value& arr, T&& value, Args&&... args) {
            arr.append(std::forward<T>(value));
            add_to_array(arr, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    Json::Value create_array(Args&&... args) {
        Json::Value arr(Json::arrayValue);
        detail::add_to_array(arr, std::forward<Args>(args)...);
        return arr;
    }

    /**
     * Validate that a JSON object contains required keys.
     * 
     * @param obj JSON object to validate
     * @param required_keys List of required key names
     * @param missing_keys Output vector of missing keys (optional)
     * @return true if all required keys are present
     */
    bool validate_required_keys(const Json::Value & obj, 
                               const std::vector<std::string> & required_keys,
                               std::vector<std::string>* missing_keys = nullptr);

    /**
     * Get nested value from a JSON object using dot notation.
     * 
     * @param obj JSON object
     * @param path Dot-separated path (e.g., "user.profile.name")
     * @return Json::Value at the path, or null value if path doesn't exist
     */
    Json::Value get_nested(const Json::Value & obj, const std::string & path);

    /**
     * Set nested value in a JSON object using dot notation.
     * Creates intermediate objects as needed.
     * 
     * @param obj JSON object to modify
     * @param path Dot-separated path (e.g., "user.profile.name")
     * @param value Value to set
     * @return true if successful
     */
    bool set_nested(Json::Value & obj, const std::string & path, const Json::Value & value);
}
