#pragma once

#include <sys/stat.h>
#include <semaphore.h>
#include <string>

namespace minerva
{
    /**
     * RAII wrapper around a POSIX named semaphore (sem_open(3)).
     *
     * Use the static factories rather than the public constructor:
     *   - create_or_open(name, mode, value): O_CREAT, may also open existing
     *   - open_existing(name): fails (throws) if the semaphore does not exist
     *
     * The semaphore *kernel object* persists across process restarts; closing
     * the handle (the destructor) detaches this process but does NOT remove
     * the name. Call unlink() (or the static unlink_name()) to remove it.
     *
     * All operations throw std::system_error on failure. EINTR is retried
     * internally; callers do not need to loop.
     *
     * This class wraps a counting semaphore, so the API uses POSIX wait/post
     * naming -- it is intentionally NOT shaped like a C++ Lockable to avoid
     * being mis-wrapped in std::lock_guard on a counting semaphore.
     */
    class named_semaphore
    {
    public:
        // Open existing only (no O_CREAT).
        static named_semaphore open_existing(const std::string& name);

        // Open, creating if necessary.
        static named_semaphore create_or_open(const std::string& name,
                                              mode_t mode,
                                              unsigned int value);

        // Remove a named semaphore from the system. Returns true on success,
        // false if it did not exist. Throws on other errors.
        static bool unlink_name(const std::string& name);

        ~named_semaphore();

        named_semaphore(const named_semaphore&)            = delete;
        named_semaphore& operator=(const named_semaphore&) = delete;
        named_semaphore(named_semaphore&& other) noexcept;
        named_semaphore& operator=(named_semaphore&& other) noexcept;

        // Decrement the semaphore, blocking until > 0. Retries on EINTR.
        void wait();

        // Try to decrement without blocking. Returns true on success, false
        // if the semaphore was 0. Retries on EINTR.
        bool try_wait();

        // Increment the semaphore.
        void post();

        // Remove this semaphore's name from the system. The handle remains
        // usable; the destructor will still close it.
        void unlink();

        const std::string& name() const { return m_name; }

    private:
        named_semaphore(sem_t* sp, std::string name);

        sem_t*      m_sp;
        std::string m_name;
    };
}
