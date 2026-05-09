#pragma once

#include <mutex>
#include <stdexcept>
#include <string>
#include "file_lock.h"

namespace minerva
{
    // Refcounted file lock that is shared between threads of the same
    // process, exclusive across processes.
    //
    // Despite the name, this is NOT POSIX LOCK_SH (multiple-readers)
    // semantics: it acquires an exclusive OS-level file lock on the
    // first lock() call and releases it on the last unlock() call from
    // the same process.  Within the process, additional threads
    // calling lock() simply bump a counter and proceed without
    // blocking on the OS.
    //
    // Intended usage: one shared_file_lock instance per file per
    // process, used by many threads.
    class shared_file_lock : public file_lock
    {
    public:
        shared_file_lock(const std::string & name, int flags, mode_t mode)
            : file_lock(name, flags, mode)
        {
        }

        void lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);
            if (m_counter == 0)
            {
                // Block on the OS lock while holding the mutex.  Since
                // every other thread arriving here would also need the
                // OS lock, serializing them on the mutex costs nothing.
                file_lock::lock();
            }
            ++m_counter;
        }

        bool try_lock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);
            if (m_counter > 0)
            {
                ++m_counter;
                return true;
            }
            if (!file_lock::try_lock())
            {
                return false;
            }
            ++m_counter;
            return true;
        }

        void unlock() override
        {
            std::unique_lock<std::mutex> lk(m_lock);
            if (m_counter == 0)
            {
                throw std::logic_error(
                    "shared_file_lock::unlock called when not locked: " +
                    get_name());
            }
            --m_counter;
            if (m_counter == 0)
            {
                file_lock::unlock();
            }
        }

    private:
        std::mutex m_lock;
        int        m_counter = 0;  // protected by m_lock
    };
}
