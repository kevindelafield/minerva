#pragma once

#include <string>
#include <vector>

namespace util
{
    bool file_exists(const std::string & path);

    bool file_is_directory(const std::string & dir);

    bool file_is_file(const std::string & file);

    bool file_is_empty(const std::string & file);

    bool touch(const std::string & file);

    bool rmdir(const std::string & dir, bool recursive = true);

    bool enum_files(const std::string & dir, 
                    std::vector<std::string> & files,
                    const std::string & end_pattern = "");

    constexpr const char FILE_SEPERATOR = '/';
    
    constexpr const char* str_end(const char *str) {
        return *str ? str_end(str + 1) : str;
    }
    
    constexpr bool str_slant(const char *str) {
        return *str == FILE_SEPERATOR ? 
            true : (*str ? str_slant(str + 1) : false);
    }
    
    constexpr const char* r_slant(const char* str) {
        return *str == FILE_SEPERATOR ? (str + 1) : r_slant(str - 1);
    }
    constexpr const char* short_file_name(const char* str) {
        return str_slant(str) ? r_slant(str_end(str)) : str;
    }
    
}

#define __SHORT_FILE__ util::short_file_name(__FILE__)
