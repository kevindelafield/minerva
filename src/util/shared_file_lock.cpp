#include "shared_file_lock.h"

namespace minerva
{
    shared_file_lock::shared_file_lock(const std::string& name,
                                       int flags, mode_t mode)
        : m_inner(name, flags, mode)
    {
    }

    void shared_file_lock::lock()
    {
        std::unique_lock<std::mutex> lk(m_mtx);

        // Fast path: another thread already holds the OS lock.
        if (m_held)
        {
            ++m_counter;
            return;
        }

        // If another thread is currently acquiring the OS lock, wait for
        // it to finish (without holding it ourselves).
        m_cv.wait(lk, [&] { return !m_acquiring; });
        if (m_held)
        {
            ++m_counter;
            return;
        }

        // We are the leader. Mark "acquiring", drop the mutex so other
        // threads can fast-path once we publish, take the OS lock, then
        // re-take the mutex and publish.
        m_acquiring = true;
        lk.unlock();

        try
        {
            m_inner.lock();
        }
        catch (...)
        {
            lk.lock();
            m_acquiring = false;
            m_cv.notify_all();
            throw;
        }

        lk.lock();
        m_acquiring = false;
        m_held      = true;
        ++m_counter;
        m_cv.notify_all();
    }

    bool shared_file_lock::try_lock()
    {
        std::unique_lock<std::mutex> lk(m_mtx);

        if (m_held)
        {
            ++m_counter;
            return true;
        }
        if (m_acquiring)
        {
            // Another thread is mid-acquire; treat as "not available now"
            // rather than blocking on the cv -- try_lock must not block.
            return false;
        }

        m_acquiring = true;
        lk.unlock();

        bool got = false;
        try
        {
            got = m_inner.try_lock();
        }
        catch (...)
        {
            lk.lock();
            m_acquiring = false;
            m_cv.notify_all();
            throw;
        }

        lk.lock();
        m_acquiring = false;
        if (got)
        {
            m_held = true;
            ++m_counter;
        }
        m_cv.notify_all();
        return got;
    }

    void shared_file_lock::unlock()
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        if (m_counter == 0)
        {
            throw std::logic_error(
                "shared_file_lock::unlock called when not locked: " +
                get_name());
        }
        --m_counter;
        if (m_counter != 0)
        {
            return;
        }

        // Last holder: release the OS lock. If that throws, restore the
        // counter so the in-process state stays consistent.
        try
        {
            lk.unlock();
            m_inner.unlock();
            lk.lock();
            m_held = false;
        }
        catch (...)
        {
            lk.lock();
            ++m_counter;
            throw;
        }
    }
}
