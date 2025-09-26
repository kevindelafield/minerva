#pragma once

#include <mutex>
#include <condition_variable>
#include <cassert>
#include <string>
#include "file_lock.h"

namespace minerva
{
    class exclusive_file_lock : public file_lock
    {
    public:

    exclusive_file_lock(const std::string & name, int flags, mode_t mode) : 
        file_lock(name, flags, mode)
        {
        }

        void lock() override
        {
            {
                std::unique_lock<std::mutex> lk(m_lock);
                
                while (m_locked)
                {
                    m_cond.wait(lk);
                }
                
                m_locked = true;
            }

            file_lock::lock();
        }

        bool try_lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);

            if (!m_locked && file_lock::try_lock())
            {
                m_locked = true;
                return true;
            }

            return false;
        }

        void unlock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);

            assert(m_locked);

            m_locked = false;

            file_lock::unlock();

            m_cond.notify_one();
        }

    private:

        std::mutex m_lock;
        std::condition_variable m_cond;
        bool m_locked = false;  // Protected by m_lock mutex - no need for volatile
    };
}
