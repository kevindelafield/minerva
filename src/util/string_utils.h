#pragma once

#include <string>
#include <algorithm>

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
        try
        {
            value = std::stod(s);
            return true;
        }
        catch (std::exception & e)
        {
            return false;
        }
    }

    inline bool try_parse_float(const std::string & s, float & value)
    {
        try
        {
            value = std::stof(s);
            return true;
        }
        catch (std::exception & e)
        {
            return false;
        }
    }

    inline bool try_parse_int(const std::string & s, int & value)
    {
        try
        {
            value = std::stoi(s);
            return true;
        }
        catch (std::exception & e)
        {
            return false;
        }
    }

    inline bool try_parse_long(const std::string & s, long long & value)
    {
        try
        {
            value = std::stoll(s);
            return true;
        }
        catch (std::exception & e)
        {
            return false;
        }
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

    inline bool starts_with(const std::string & s1, const std::string s2)
    {
        return s1.rfind(s2, 0) == 0;
    }

    inline bool ends_with(const std::string & s1, const std::string s2)
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
        return str;
    }
    
    inline std::string& rtrim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
    {
        auto pos = str.find_last_not_of(chars);
        if (pos != std::string::npos && pos + 1 < str.size())
        {
            str.erase(pos + 1);
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
}
