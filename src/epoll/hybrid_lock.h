#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include "spin_lock.h"

namespace epoll
{

    class hybrid_lock
    {
    private:
        static std::atomic<unsigned long long> counter;
        int lock_number;

    
    public:
        hybrid_lock()
        {
            lock_number = counter.fetch_add(1) % LOCK_COUNT;
        }

        ~hybrid_lock() = default;

        void lock();

        void unlock();

        constexpr static int LOCK_COUNT = 10000;
    };
}
