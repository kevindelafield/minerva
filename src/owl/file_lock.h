#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include "log.h"

namespace owl
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

        virtual ~file_lock() 
        {
            if (close(m_sp))
            {
                FATAL_ERRNO("failed to close lock file: " << m_name, errno);
            }
        }

        virtual void lock()
        {
            if (lockf(m_sp, F_LOCK, 0))
            {
                FATAL_ERRNO("failed to lock file: " << m_name, errno);
            }
        }

        virtual bool try_lock()
        {
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
            if (lockf(m_sp, F_ULOCK, 0))
            {
                FATAL_ERRNO("failed to unlock file: " << m_name, errno);
            }
        }

    private:

        int m_sp;
        std::string m_name;

    };
}
