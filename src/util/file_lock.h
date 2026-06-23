#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>     // flock(2)
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string>
#include <system_error>
#include <utility>
#include "log.h"

namespace minerva
{

    // Cross-process file lock built on flock(2).
    //
    // Why flock(2) instead of lockf/F_SETLK?
    //   * flock locks are owned per-fd, not per-process, so two file_lock
    //     objects in the same process pointing at the same path correctly
    //     block each other at the kernel level.
    //   * flock locks are not released by closing an unrelated fd to the
    //     same file (the lockf/POSIX advisory lock footgun).
    //   * flock locks aren't tied to file offsets, so reading/writing the
    //     locked file does not change the locked region.
    //
    // file_lock itself is rarely used directly; prefer
    // exclusive_file_lock (one logical lock, but reentrant across threads
    // in the same process) or shared_file_lock (refcounted across threads
    // in the same process, exclusive across processes).
    class file_lock
    {
    public:
        file_lock(const std::string & name, int flags, mode_t mode)
            : m_name(name), m_sp(-1)
        {
            m_sp = ::open(name.c_str(), flags, mode);
            if (m_sp < 0)
            {
                throw std::system_error(errno, std::system_category(),
                                        "failed to open lock file: " + name);
            }
            // Make sure the lock fd is not inherited by exec'd children;
            // they would otherwise hold our lock open after we'd dropped it.
            int fdflags = ::fcntl(m_sp, F_GETFD, 0);
            if (fdflags >= 0)
            {
                ::fcntl(m_sp, F_SETFD, fdflags | FD_CLOEXEC);
            }
        }

        // Non-copyable: each instance owns one fd.
        file_lock(const file_lock&) = delete;
        file_lock& operator=(const file_lock&) = delete;

        file_lock(file_lock&& other) noexcept
            : m_name(std::move(other.m_name)), m_sp(other.m_sp)
        {
            other.m_sp = -1;
        }

        file_lock& operator=(file_lock&& other) noexcept
        {
            if (this != &other)
            {
                close_fd();
                m_name = std::move(other.m_name);
                m_sp   = other.m_sp;
                other.m_sp = -1;
            }
            return *this;
        }

        virtual ~file_lock()
        {
            close_fd();
        }

        virtual void lock()
        {
            check_fd("lock");
            // flock with LOCK_EX blocks until the lock is acquired.  Retry
            // on EINTR per POSIX.
            int rc;
            do {
                rc = ::flock(m_sp, LOCK_EX);
            } while (rc != 0 && errno == EINTR);
            if (rc != 0)
            {
                throw std::system_error(errno, std::system_category(),
                                        "failed to lock file: " + m_name);
            }
        }

        virtual bool try_lock()
        {
            check_fd("try_lock");
            int rc;
            do {
                rc = ::flock(m_sp, LOCK_EX | LOCK_NB);
            } while (rc != 0 && errno == EINTR);
            if (rc == 0)
            {
                return true;
            }
            if (errno == EWOULDBLOCK)
            {
                return false;
            }
            throw std::system_error(errno, std::system_category(),
                                    "failed to try-lock file: " + m_name);
        }

        virtual void unlock()
        {
            check_fd("unlock");
            int rc;
            do {
                rc = ::flock(m_sp, LOCK_UN);
            } while (rc != 0 && errno == EINTR);
            if (rc != 0)
            {
                throw std::system_error(errno, std::system_category(),
                                        "failed to unlock file: " + m_name);
            }
        }

        // Diagnostic only -- do not close, dup, or otherwise touch this fd.
        int debug_fd() const { return m_sp; }
        const std::string& get_name() const { return m_name; }

    private:
        void close_fd() noexcept
        {
            if (m_sp >= 0)
            {
                if (::close(m_sp) != 0)
                {
                    // Don't throw from a destructor / move-assign.  On
                    // Linux the fd is released regardless of the return
                    // value; just record the error for diagnosis.
                    LOG_ERROR_ERRNO("failed to close lock file: " << m_name,
                                    errno);
                }
                m_sp = -1;
            }
        }

        void check_fd(const char * op) const
        {
            if (m_sp < 0)
            {
                throw std::system_error(EBADF, std::system_category(),
                                        std::string("file_lock::") + op +
                                        " on closed/moved-from instance: " +
                                        m_name);
            }
        }

        // Declaration order matches initializer order to avoid -Wreorder.
        std::string m_name;
        int         m_sp;
    };

    // RAII guard: locks on construction, unlocks on destruction.
    // Works with any of file_lock / exclusive_file_lock / shared_file_lock
    // (anything with lock()/unlock() member functions).
    template <typename Lockable>
    class file_lock_guard
    {
    public:
        explicit file_lock_guard(Lockable & lk) : m_lk(lk)
        {
            m_lk.lock();
        }

        ~file_lock_guard()
        {
            try
            {
                m_lk.unlock();
            }
            catch (const std::exception & e)
            {
                LOG_ERROR("file_lock_guard unlock failed: " << e.what());
            }
        }

        file_lock_guard(const file_lock_guard&) = delete;
        file_lock_guard& operator=(const file_lock_guard&) = delete;

    private:
        Lockable & m_lk;
    };
}
