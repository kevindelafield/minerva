#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <ovhttpd/log.h>

namespace epoll
{

    class spin_lock
    {
    private:
        std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
        volatile bool locked = false;
    
    public:
        spin_lock() = default;

        ~spin_lock() = default;

        void lock()
        {
            long long c = 0;
            while (m_lock.test_and_set(std::memory_order_acquire))
            {
                c++;
            }
            assert(!locked);
            locked = true;
            if (c >= 100000)
            {
                LOG_INFO("long spin lock: " << c);
            }
        }
    
        void unlock()
        {
            assert(locked);
            locked = false;
            m_lock.clear(std::memory_order_release);
        }
    };
}
