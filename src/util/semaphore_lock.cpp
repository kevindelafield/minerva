#include <fcntl.h>
#include <errno.h>
#include "semaphore_lock.h"
#include "log.h"
#include <system_error>
#include <utility>

namespace minerva
{
    namespace
    {
        // POSIX (and Linux specifically) requires names to start with '/' and
        // contain no further '/'. Catch typos before the syscall fails.
        void validate_name(const std::string& name)
        {
            if (name.empty() || name[0] != '/')
            {
                throw std::invalid_argument(
                    "named_semaphore: name must start with '/': " + name);
            }
            if (name.find('/', 1) != std::string::npos)
            {
                throw std::invalid_argument(
                    "named_semaphore: name must contain only one '/': " + name);
            }
        }

        [[noreturn]] void throw_errno(int err, const char* op,
                                      const std::string& name)
        {
            throw std::system_error(
                err, std::system_category(),
                std::string("named_semaphore::") + op + " '" + name + "'");
        }
    }

    named_semaphore::named_semaphore(sem_t* sp, std::string name)
        : m_sp(sp), m_name(std::move(name))
    {
    }

    named_semaphore named_semaphore::open_existing(const std::string& name)
    {
        validate_name(name);
        sem_t* sp = sem_open(name.c_str(), 0);
        if (sp == SEM_FAILED)
        {
            throw_errno(errno, "open_existing", name);
        }
        return named_semaphore(sp, name);
    }

    named_semaphore named_semaphore::create_or_open(const std::string& name,
                                                    mode_t mode,
                                                    unsigned int value)
    {
        validate_name(name);
        sem_t* sp = sem_open(name.c_str(), O_CREAT, mode, value);
        if (sp == SEM_FAILED)
        {
            throw_errno(errno, "create_or_open", name);
        }
        return named_semaphore(sp, name);
    }

    bool named_semaphore::unlink_name(const std::string& name)
    {
        validate_name(name);
        if (sem_unlink(name.c_str()) == 0)
        {
            return true;
        }
        if (errno == ENOENT)
        {
            return false;
        }
        throw_errno(errno, "unlink_name", name);
    }

    named_semaphore::~named_semaphore()
    {
        if (m_sp != SEM_FAILED && m_sp != nullptr)
        {
            if (sem_close(m_sp) != 0)
            {
                // Destructors must not throw. Log and move on; this leaks at
                // most a process-wide handle, not the semaphore name.
                LOG_ERROR_ERRNO("named_semaphore: sem_close failed for "
                                << m_name, errno);
            }
        }
    }

    named_semaphore::named_semaphore(named_semaphore&& other) noexcept
        : m_sp(other.m_sp), m_name(std::move(other.m_name))
    {
        other.m_sp = SEM_FAILED;
    }

    named_semaphore& named_semaphore::operator=(named_semaphore&& other) noexcept
    {
        if (this != &other)
        {
            if (m_sp != SEM_FAILED && m_sp != nullptr)
            {
                if (sem_close(m_sp) != 0)
                {
                    LOG_ERROR_ERRNO("named_semaphore: sem_close failed for "
                                    << m_name, errno);
                }
            }
            m_sp   = other.m_sp;
            m_name = std::move(other.m_name);
            other.m_sp = SEM_FAILED;
        }
        return *this;
    }

    void named_semaphore::wait()
    {
        while (sem_wait(m_sp) != 0)
        {
            if (errno == EINTR) continue;
            throw_errno(errno, "wait", m_name);
        }
    }

    bool named_semaphore::try_wait()
    {
        while (sem_trywait(m_sp) != 0)
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return false;
            throw_errno(errno, "try_wait", m_name);
        }
        return true;
    }

    void named_semaphore::post()
    {
        if (sem_post(m_sp) != 0)
        {
            throw_errno(errno, "post", m_name);
        }
    }

    void named_semaphore::unlink()
    {
        if (sem_unlink(m_name.c_str()) != 0 && errno != ENOENT)
        {
            throw_errno(errno, "unlink", m_name);
        }
    }
}
