#pragma once

#include <functional>
#include <utility>

namespace minerva
{
    /**
     * RAII scope guard: runs a callable exactly once at scope exit unless
     * release() is called first. Movable, not copyable.
     *
     * Exceptions thrown by the callable during destruction are swallowed
     * (the destructor is noexcept).
     */
    class unique_command
    {
    public:
        explicit unique_command(std::function<void()> cmd)
            : m_cmd(std::move(cmd)), m_armed(true)
        {
        }

        unique_command(const unique_command&)            = delete;
        unique_command& operator=(const unique_command&) = delete;

        unique_command(unique_command&& other) noexcept
            : m_cmd(std::move(other.m_cmd)), m_armed(other.m_armed)
        {
            other.m_armed = false;
        }

        unique_command& operator=(unique_command&& other) noexcept
        {
            if (this != &other)
            {
                fire();
                m_cmd         = std::move(other.m_cmd);
                m_armed       = other.m_armed;
                other.m_armed = false;
            }
            return *this;
        }

        ~unique_command()
        {
            fire();
        }

        // Disarm: cleanup will not be invoked.
        void release() noexcept
        {
            m_armed = false;
        }

        bool is_armed() const noexcept { return m_armed; }

    private:
        void fire() noexcept
        {
            if (m_armed && m_cmd)
            {
                try
                {
                    m_cmd();
                }
                catch (...)
                {
                    // Swallow: destructors must not throw.
                }
            }
            m_armed = false;
        }

        std::function<void()> m_cmd;
        bool                  m_armed{false};
    };
}
