#pragma once

#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <limits>
#include <cstdlib>  // For strtod, strtof, strtol, strtoll
#include <cerrno>   // For errno, ERANGE

namespace minerva
{
    // case-independent (ci) compare_less binary function
    struct nocase_compare
    {
        bool operator() (const unsigned char& c1,
                         const unsigned char& c2) const {
            return std::tolower (c1) < std::tolower (c2); 
        }
    };

    struct ci_less
    {
        bool operator() (const std::string & s1, const std::string & s2) const {
            return std::lexicographical_compare 
                (s1.begin (), s1.end (),   // source range
                 s2.begin (), s2.end (),   // dest range
                 nocase_compare ());  // comparison
        }
    };

    inline bool try_parse_double(const std::string & s, double & value)
    {
        if (s.empty())
            return false;
            
        char* end_ptr = nullptr;
        errno = 0;  // Clear errno before conversion
        
        double result = std::strtod(s.c_str(), &end_ptr);
        
        // Check for conversion errors
        if (errno == ERANGE)  // Overflow/underflow
            return false;
            
        if (end_ptr == s.c_str())  // No conversion occurred
            return false;
            
        // Check that we consumed the entire string (skip trailing whitespace)
        while (end_ptr && *end_ptr && std::isspace(*end_ptr))
            ++end_ptr;
            
        if (end_ptr && *end_ptr != '\0')  // Unconsumed non-whitespace characters
            return false;
            
        value = result;
        return true;
    }

    inline bool try_parse_float(const std::string & s, float & value)
    {
        if (s.empty())
            return false;
            
        char* end_ptr = nullptr;
        errno = 0;  // Clear errno before conversion
        
        float result = std::strtof(s.c_str(), &end_ptr);
        
        // Check for conversion errors
        if (errno == ERANGE)  // Overflow/underflow
            return false;
            
        if (end_ptr == s.c_str())  // No conversion occurred
            return false;
            
        // Check that we consumed the entire string (skip trailing whitespace)
        while (end_ptr && *end_ptr && std::isspace(*end_ptr))
            ++end_ptr;
            
        if (end_ptr && *end_ptr != '\0')  // Unconsumed non-whitespace characters
            return false;
            
        value = result;
        return true;
    }

    inline bool try_parse_int(const std::string & s, int & value)
    {
        if (s.empty())
            return false;
            
        char* end_ptr = nullptr;
        errno = 0;  // Clear errno before conversion
        
        long result = std::strtol(s.c_str(), &end_ptr, 10);
        
        // Check for conversion errors
        if (errno == ERANGE)  // Overflow/underflow
            return false;
            
        if (end_ptr == s.c_str())  // No conversion occurred
            return false;
            
        // Check that we consumed the entire string (skip trailing whitespace)
        while (end_ptr && *end_ptr && std::isspace(*end_ptr))
            ++end_ptr;
            
        if (end_ptr && *end_ptr != '\0')  // Unconsumed non-whitespace characters
            return false;
            
        // Check if the result fits in an int
        if (result < std::numeric_limits<int>::min() || 
            result > std::numeric_limits<int>::max())
            return false;
            
        value = static_cast<int>(result);
        return true;
    }

    inline bool try_parse_long(const std::string & s, long long & value)
    {
        if (s.empty())
            return false;
            
        char* end_ptr = nullptr;
        errno = 0;  // Clear errno before conversion
        
        long long result = std::strtoll(s.c_str(), &end_ptr, 10);
        
        // Check for conversion errors
        if (errno == ERANGE)  // Overflow/underflow
            return false;
            
        if (end_ptr == s.c_str())  // No conversion occurred
            return false;
            
        // Check that we consumed the entire string (skip trailing whitespace)
        while (end_ptr && *end_ptr && std::isspace(*end_ptr))
            ++end_ptr;
            
        if (end_ptr && *end_ptr != '\0')  // Unconsumed non-whitespace characters
            return false;
            
        value = result;
        return true;
    }

    // Additional exception-free parsing functions
    inline bool try_parse_uint(const std::string & s, unsigned int & value)
    {
        if (s.empty())
            return false;
            
        // Don't allow negative numbers for unsigned parsing
        if (s[0] == '-')
            return false;
            
        char* end_ptr = nullptr;
        errno = 0;  // Clear errno before conversion
        
        unsigned long result = std::strtoul(s.c_str(), &end_ptr, 10);
        
        // Check for conversion errors
        if (errno == ERANGE)  // Overflow/underflow
            return false;
            
        if (end_ptr == s.c_str())  // No conversion occurred
            return false;
            
        // Check that we consumed the entire string (skip trailing whitespace)
        while (end_ptr && *end_ptr && std::isspace(*end_ptr))
            ++end_ptr;
            
        if (end_ptr && *end_ptr != '\0')  // Unconsumed non-whitespace characters
            return false;
            
        // Check if the result fits in an unsigned int
        if (result > std::numeric_limits<unsigned int>::max())
            return false;
            
        value = static_cast<unsigned int>(result);
        return true;
    }

    inline bool try_parse_bool(const std::string & s, bool & value)
    {
        if (s.empty())
            return false;
            
        std::string lower_s = s;
        
        // Convert to lowercase manually
        for (char & c : lower_s)
        {
            c = std::tolower(c);
        }
        
        // Trim whitespace manually
        auto start = lower_s.find_first_not_of(" \t\n\v\f\r");
        if (start == std::string::npos) {
            return false; // All whitespace
        }
        
        auto end = lower_s.find_last_not_of(" \t\n\v\f\r");
        lower_s = lower_s.substr(start, end - start + 1);
        
        if (lower_s == "true" || lower_s == "1" || lower_s == "yes" || lower_s == "on")
        {
            value = true;
            return true;
        }
        else if (lower_s == "false" || lower_s == "0" || lower_s == "no" || lower_s == "off")
        {
            value = false;
            return true;
        }
        
        return false; // Unrecognized boolean value
    }

    inline void tolower(std::string & s1)
    {
        for (char & c : s1)
        {
            c = std::tolower(c);
        }
    }

    inline void toupper(std::string & s1)
    {
        for (char & c : s1)
        {
            c = std::toupper(c);
        }
    }

    inline bool starts_with(const std::string & s1, const std::string & s2)
    {
        return s1.rfind(s2, 0) == 0;
    }

    inline bool ends_with(const std::string & s1, const std::string & s2)
    {
        return std::equal(s2.rbegin(), s2.rend(), s1.rbegin());
    }

    inline bool ci_equals(const std::string & s1, const std::string & s2)
    {
        return
            s1.size() == s2.size() &&
            std::equal(s1.begin(), s1.end(), s2.begin(),
                   [](const char & c1, const char & c2)
                   {
                       return (c1 == c2 ||std::toupper(c1) == std::toupper(c2));
                   });
    }

    inline std::string& ltrim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
    {
        auto pos = str.find_first_not_of(chars);
        if (pos != std::string::npos)
        {
            str.erase(0, pos);
        }
        else
        {
            str.clear(); // All characters are whitespace - clear the string
        }
        return str;
    }
    
    inline std::string& rtrim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
    {
        auto pos = str.find_last_not_of(chars);
        if (pos != std::string::npos)
        {
            if (pos + 1 < str.size())
            {
                str.erase(pos + 1);
            }
        }
        else
        {
            str.clear(); // All characters are whitespace - clear the string
        }
        return str;
    }
    
    inline std::string& trim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
    {
        return ltrim(rtrim(str, chars), chars);
    }

    inline std::string& trim_new_line_character(std::string& str)
    {
        rtrim(str, "\r\n");
        return str;
    }

    // Efficient string splitting without multiple allocations
    inline void split(const std::string& str, char delimiter, std::vector<std::string>& result)
    {
        result.clear();
        size_t start = 0;
        size_t pos = 0;
        
        while ((pos = str.find(delimiter, start)) != std::string::npos)
        {
            if (pos > start) // Non-empty substring
                result.emplace_back(str.substr(start, pos - start));
            start = pos + 1;
        }
        
        // Add the last part if non-empty
        if (start < str.size())
            result.emplace_back(str.substr(start));
    }

    // Fast hex string to unsigned conversion - optimized for chunked encoding
    inline bool try_parse_hex(const std::string& hex_str, size_t& result)
    {
        if (hex_str.empty())
            return false;
            
        // Chunked encoding hex values are typically small (< 8 chars for reasonable chunk sizes)
        // Protect against overflow - 16 hex chars = 64 bits (max for size_t on 64-bit systems)
        if (hex_str.size() > 16)
            return false;
            
        result = 0;
        for (char c : hex_str)
        {
            // Check for overflow before shifting
            if (result > (std::numeric_limits<size_t>::max() >> 4))
                return false;
                
            result <<= 4;
            if (c >= '0' && c <= '9')
                result += c - '0';
            else if (c >= 'A' && c <= 'F')
                result += c - 'A' + 10;
            else if (c >= 'a' && c <= 'f')
                result += c - 'a' + 10;
            else
                return false; // Invalid hex character
        }
        return true;
    }
}
