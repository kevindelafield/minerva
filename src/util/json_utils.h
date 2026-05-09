#pragma once

#include <istream>
#include <string>
#include <jsoncpp/json/json.h>

namespace minerva
{
    /**
     * Parse JSON from an input stream.
     *
     * On failure, @p value is reset to a default-constructed Json::Value and
     * the function returns false. The stream overload does not surface the
     * underlying parser error message; use the string overload if a detailed
     * error is required.
     *
     * @param str   Input stream containing JSON data
     * @param value Output Json::Value object (cleared on failure)
     * @return true if parsing succeeded, false otherwise
     */
    bool parse_json(std::istream & str, Json::Value & value);

    /**
     * Parse JSON from a string.
     *
     * On failure, @p value is reset to a default-constructed Json::Value, the
     * parser error is written to @p error_msg (if non-null), and the function
     * returns false.
     *
     * @param json_str  String containing JSON data
     * @param value     Output Json::Value object (cleared on failure)
     * @param error_msg Optional output parameter for error message
     * @return true if parsing succeeded, false otherwise
     */
    bool parse_json(const std::string & json_str, Json::Value & value,
                    std::string * error_msg = nullptr);

    /**
     * Convert a Json::Value to a string.
     *
     * @param value  Json::Value to convert
     * @param pretty If true, format with 2-space indentation; otherwise emit
     *               a compact single-line representation. In either mode the
     *               result has no trailing newline.
     * @return JSON string representation
     */
    std::string to_json_string(const Json::Value & value, bool pretty = false);
}
