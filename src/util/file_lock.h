#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include "log.h"

namespace minerva
{
    class file_lock
    {
    public:

        file_lock(const std::string & name, int flags, mode_t mode) : m_name(name)
        {
            m_sp = open(name.c_str(), flags, mode);
            if (m_sp < 0)
            {
                FATAL_ERRNO("failed to open lock file: " << name, errno);
            }
        }

        // Make non-copyable to prevent double-close issues
        file_lock(const file_lock&) = delete;
        file_lock& operator=(const file_lock&) = delete;

        // Allow moving
        file_lock(file_lock&& other) noexcept 
            : m_name(std::move(other.m_name)), m_sp(other.m_sp)
        {
            other.m_sp = -1;  // Prevent double-close
        }

        file_lock& operator=(file_lock&& other) noexcept
        {
            if (this != &other) {
                // Close current file descriptor if valid
                if (m_sp >= 0 && close(m_sp)) {
                    FATAL_ERRNO("failed to close lock file: " << m_name, errno);
                }
                m_name = std::move(other.m_name);
                m_sp = other.m_sp;
                other.m_sp = -1;  // Prevent double-close
            }
            return *this;
        }

        virtual ~file_lock() 
        {
            if (m_sp >= 0 && close(m_sp))
            {
                FATAL_ERRNO("failed to close lock file: " << m_name, errno);
            }
        }

        virtual void lock()
        {
            if (m_sp < 0) {
                FATAL("Attempting to lock with invalid file descriptor: " << m_name);
            }
            if (lockf(m_sp, F_LOCK, 0))
            {
                FATAL_ERRNO("failed to lock file: " << m_name, errno);
            }
        }

        virtual bool try_lock()
        {
            if (m_sp < 0) {
                FATAL("Attempting to try_lock with invalid file descriptor: " << m_name);
            }
            if (lockf(m_sp, F_TLOCK, 0))
            {
                if (errno == EACCES || errno == EAGAIN)
                {
                    return false;
                }
                FATAL_ERRNO("failed to try lock file: " << m_name, errno);
            }

            return true;
        }

        virtual void unlock()
        {
            if (m_sp < 0) {
                FATAL("Attempting to unlock with invalid file descriptor: " << m_name);
            }
            if (lockf(m_sp, F_ULOCK, 0))
            {
                FATAL_ERRNO("failed to unlock file: " << m_name, errno);
            }
        }

        // Accessor for debugging or advanced use cases
        int get_file_descriptor() const { return m_sp; }
        const std::string& get_name() const { return m_name; }

    private:

        int m_sp;
        std::string m_name;

    };
}
