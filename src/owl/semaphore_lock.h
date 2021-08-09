#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <string>
#include "log.h"

namespace owl
{
    class named_semaphore
    {
    public:

    named_semaphore(const std::string & name, int flags, mode_t mode, unsigned int value) : m_name(name)
        {
            m_sp = sem_open(name.c_str(), flags, mode, value);
            if (m_sp == SEM_FAILED)
            {
                FATAL_ERRNO("failed to open semaphore: " << name, errno);
            }
        }

        virtual ~named_semaphore() 
        {
            if (sem_close(m_sp))
            {
                FATAL_ERRNO("failed to close semaphore: " << m_name, errno);
            }
        }

        void lock()
        {
            if (sem_wait(m_sp))
            {
                FATAL_ERRNO("failed to wait on semaphore: " << m_name, errno);
            }
        }

        bool try_lock()
        {
            if (sem_trywait(m_sp))
            {
                if (errno == EAGAIN)
                {
                    return false;
                }
                FATAL_ERRNO("failed to try wait on semaphore: " << m_name, errno);
            }
            return true;
        }

        void unlock()
        {
            if (sem_post(m_sp))
            {
                FATAL_ERRNO("failed to post to semaphore: " << m_name, errno);
            }
        }

    private:

        sem_t * m_sp;
        std::string m_name;

    };
}
