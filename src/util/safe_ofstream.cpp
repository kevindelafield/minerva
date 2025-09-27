#include <unistd.h>
#include <stdio.h>
#include <stdexcept>
#include <sstream>
#include "log.h"
#include "safe_ofstream.h"

namespace minerva
{
    safe_ofstream::safe_ofstream(const std::string & filename)
        : std::ofstream(), m_realpath(filename), m_open(false), m_committed(false), m_temp_file_exists(false)
    {
        if (filename.empty())
        {
            throw std::invalid_argument("Filename cannot be empty");
        }
        
        // Validate path safety to prevent directory traversal attacks
        if (!is_path_safe(filename))
        {
            throw std::invalid_argument("Unsafe file path: " + filename);
        }
        
        // Create temp filename with strong GUID for enhanced security
        std::stringstream ss;
        auto strong_guid = minerva::new_strong_guid();
        if (!strong_guid) {
            // Fallback to standard GUID if strong version fails
            ss << filename << "." << minerva::new_guid();
        } else {
            ss << filename << "." << *strong_guid;
        }
        m_fakepath = ss.str();
        
        // Use RAII for exception safety
        temp_file_guard guard(m_fakepath);
        
        // Open file
        std::ofstream::open(m_fakepath.c_str(), std::ios_base::out);
        if (!is_open())
        {
            throw std::runtime_error("Failed to create temporary file: " + m_fakepath);
        }
        
        // Success - release guard and set flags
        guard.release();
        m_open = true;
        m_temp_file_exists = true;  // Temp file now exists on disk
    }

    safe_ofstream::~safe_ofstream()
    {
        // Clean up temp file if it exists and hasn't been committed
        if (m_temp_file_exists && !m_committed)
        {
            // Ensure stream is closed before unlinking
            if (is_open())
            {
                close();
            }
            
            // Remove temp file
            if (unlink(m_fakepath.c_str()) && errno != ENOENT)
            {
                LOG_ERROR_ERRNO("failed to unlink temp file: " << m_fakepath, errno);
            }
        }
    }

    safe_ofstream::safe_ofstream(safe_ofstream&& other) noexcept
        : std::ofstream(std::move(other)),  // Move base class
          m_fakepath(std::move(other.m_fakepath)),
          m_realpath(std::move(other.m_realpath)),
          m_committed(other.m_committed),
          m_open(other.m_open),
          m_temp_file_exists(other.m_temp_file_exists)
    {
        // Invalidate source object to prevent double cleanup
        other.m_committed = true;          // Prevent cleanup in source destructor
        other.m_open = false;
        other.m_temp_file_exists = false;
        other.m_fakepath.clear();
        other.m_realpath.clear();
    }

    safe_ofstream& safe_ofstream::operator=(safe_ofstream&& other) noexcept
    {
        if (this != &other)
        {
            // Clean up current object's resources first
            if (m_temp_file_exists && !m_committed)
            {
                if (is_open())
                {
                    close();
                }
                unlink(m_fakepath.c_str());  // Clean up our temp file
            }
            
            // Move base class
            std::ofstream::operator=(std::move(other));
            
            // Transfer ownership of all members
            m_fakepath = std::move(other.m_fakepath);
            m_realpath = std::move(other.m_realpath);
            m_committed = other.m_committed;
            m_open = other.m_open;
            m_temp_file_exists = other.m_temp_file_exists;
            
            // Invalidate source object
            other.m_committed = true;          // Prevent cleanup in source destructor
            other.m_open = false;
            other.m_temp_file_exists = false;
            other.m_fakepath.clear();
            other.m_realpath.clear();
        }
        return *this;
    }

    bool safe_ofstream::commit()
    {
        // Check if already committed
        if (m_committed)
        {
            return true;  // Already committed successfully
        }
        
        // Check if temp file exists
        if (!m_temp_file_exists)
        {
            LOG_ERROR("No temporary file to commit");
            return false;
        }
        
        // Check stream state (better error detection)
        if (fail() || bad())
        {
            LOG_ERROR("Stream is in error state, cannot commit");
            return false;
        }
        
        // Close stream if still open
        if (is_open())
        {
            flush();
            close();
        }
        
        // Atomically rename temp file to final name
        if (rename(m_fakepath.c_str(), m_realpath.c_str()))
        {
            LOG_ERROR_ERRNO("failed to rename temp file: " << m_fakepath 
                           << " to " << m_realpath 
                           << " (temp file will be cleaned up on destruction)", errno);
            return false;
        }
        
        // Critical: sync() after rename to ensure directory metadata is flushed to disk
        // This prevents file corruption if system crashes/reboots right after rename
        sync();
        
        // Update state - temp file is now the real file
        m_committed = true;
        m_temp_file_exists = false;  // Temp file no longer exists

        return true;
    }
}
