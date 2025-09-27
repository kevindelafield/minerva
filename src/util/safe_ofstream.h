#pragma once

#include <unistd.h>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include "guid.h"

namespace minerva
{
    // RAII helper class for exception-safe temp file management
    class temp_file_guard
    {
        std::string path;
        bool released = false;
    public:
        temp_file_guard(const std::string& p) : path(p) {}
        ~temp_file_guard() { if (!released) unlink(path.c_str()); }
        void release() { released = true; }
    };

    class safe_ofstream : public std::ofstream
    {
    public:

    safe_ofstream(const std::string & filename);
        
        ~safe_ofstream();
        
        bool commit();
        
        // Status query methods
        bool is_committed() const { return m_committed; }
        const std::string& get_temp_path() const { return m_fakepath; }
        const std::string& get_target_path() const { return m_realpath; }
        
        // Copy operations deleted for safety
        safe_ofstream(const safe_ofstream&) = delete;
        safe_ofstream& operator=(const safe_ofstream&) = delete;
        
        // Move operations for transferring ownership
        safe_ofstream(safe_ofstream&& other) noexcept;
        safe_ofstream& operator=(safe_ofstream&& other) noexcept;
        
    private:
        std::string m_fakepath;
        std::string m_realpath;
        bool m_committed = false;  // Fixed typo
        bool m_open = false;
        bool m_temp_file_exists = false;  // Track actual temp file existence
    };

}
