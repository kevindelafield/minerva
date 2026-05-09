#pragma once

#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <string>
#include "file_lock.h"

namespace minerva
{
    // Process-exclusive file lock with thread-level serialization.
    //
    // Multiple threads in the same process can call lock() on the same
    // exclusive_file_lock instance: the first thread acquires the OS
    // file lock, subsequent threads wait on the internal condition
    // variable until the first one calls unlock().  Recursive locking
    // by the same thread is NOT supported and will deadlock.
    //
    // With the underlying flock(2) implementation in file_lock, two
    // exclusive_file_lock instances in the same process pointing at the
    // same path also block each other correctly (flock is per-fd).
    class exclusive_file_lock : public file_lock
    {
    public:
        exclusive_file_lock(const std::string & name, int flags, mode_t mode)
            : file_lock(name, flags, mode)
        {
        }

        void lock() override
        {
            {
                std::unique_lock<std::mutex> lk(m_lock);
                m_cond.wait(lk, [this]{ return !m_locked; });
                m_locked = true;
            }
            // Acquire OS lock outside the mutex so other threads can at
            // least observe state changes if they were waiting.
            try
            {
                file_lock::lock();
            }
            catch (...)
            {
                std::unique_lock<std::mutex> lk(m_lock);
                m_locked = false;
                m_cond.notify_one();
                throw;
            }
        }

        bool try_lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);
            if (m_locked)
            {
                return false;
            }
            // Optimistically claim the slot, then attempt the OS lock.
            // If the OS lock fails, release the slot.
            m_locked = true;
            lk.unlock();

            bool got;
            try
            {
                got = file_lock::try_lock();
            }
            catch (...)
            {
                lk.lock();
                m_locked = false;
                m_cond.notify_one();
                throw;
            }

            if (!got)
            {
                lk.lock();
                m_locked = false;
                m_cond.notify_one();
                return false;
            }
            return true;
        }

        void unlock() override
        {
            // Release OS lock first, then drop the in-process slot.
            file_lock::unlock();
            {
                std::unique_lock<std::mutex> lk(m_lock);
                if (!m_locked)
                {
                    throw std::logic_error(
                        "exclusive_file_lock::unlock called when not locked: " +
                        get_name());
                }
                m_locked = false;
            }
            m_cond.notify_one();
        }

    private:
        std::mutex              m_lock;
        std::condition_variable m_cond;
        bool                    m_locked = false;
    };
}
