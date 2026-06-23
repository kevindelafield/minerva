#pragma once

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include "file_lock.h"

namespace minerva
{
    /**
     * Cross-process exclusive file lock that is reference-counted across
     * threads of the same process. The first thread to call lock() acquires
     * an exclusive flock(2) on the file; subsequent threads in this process
     * fast-path on the in-process counter without re-entering the kernel.
     * The OS lock is released only when the last in-process holder calls
     * unlock().
     *
     * IMPORTANT:
     *   - The name uses "shared" in the in-process sense (the OS lock is
     *     shared by threads of this process). It is NOT POSIX LOCK_SH.
     *   - Because counter > 0 makes try_lock() succeed for any thread, this
     *     class is unsafe for in-process mutual exclusion. It is only useful
     *     for cross-process exclusion.
     *   - Do not use across fork() without exec(): the child inherits the
     *     in-process counter as if it held the lock, but the OS-level flock
     *     ownership is per-fd / per-process and may not behave as expected.
     *     Construct a fresh shared_file_lock in the child, or call exec().
     */
    class shared_file_lock final
    {
    public:
        shared_file_lock(const std::string& name, int flags, mode_t mode);

        shared_file_lock(const shared_file_lock&)            = delete;
        shared_file_lock& operator=(const shared_file_lock&) = delete;
        shared_file_lock(shared_file_lock&&)                 = delete;
        shared_file_lock& operator=(shared_file_lock&&)      = delete;

        void lock();
        bool try_lock();
        void unlock();

        const std::string& get_name() const { return m_inner.get_name(); }

    private:
        file_lock               m_inner;
        std::mutex              m_mtx;
        std::condition_variable m_cv;
        std::size_t             m_counter   = 0;   // logical holders
        bool                    m_acquiring = false; // OS lock in flight
        bool                    m_held      = false; // OS lock held
    };
}
