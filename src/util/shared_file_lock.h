#pragma once

#include <atomic>
#include <mutex>
#include <cassert>
#include <string>
#include "file_lock.h"

namespace minerva
{
    class shared_file_lock : public file_lock
    {
    public:

    shared_file_lock(const std::string & name, int flags, mode_t mode) : 
        file_lock(name, flags, mode)
        {
        }

        void lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);

            int last = m_counter.fetch_add(1);
            if (last == 0)
            {
                file_lock::lock();
            }
        }

        bool try_lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);

            if (m_counter.load() > 0 || file_lock::try_lock())
            {
                m_counter++;
                return true;
            }

            return false;
        }

        void unlock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);

            assert(m_counter.load() != 0);

            int last = m_counter.fetch_sub(1);
            if (last == 1)
            {
                file_lock::unlock();
            }
        }

    private:

        std::mutex m_lock;
        std::atomic<int> m_counter {0};
    };
}
