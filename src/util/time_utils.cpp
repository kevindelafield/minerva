#include <cstring>
#include "time_utils.h"

namespace minerva
{
    std::string ctime(const std::time_t& time)
    {
        // ctime_r writes a 26-byte string ending in "\n\0".
        char buf[26];
        if (!ctime_r(&time, buf))
        {
            return {};
        }
        std::string s(buf);
        if (!s.empty() && s.back() == '\n')
        {
            s.pop_back();
        }
        return s;
    }

    std::tm localtime(const std::time_t& time)
    {
        std::tm tm{};
        if (!localtime_r(&time, &tm))
        {
            // tm is already zero-initialized.
        }
        return tm;
    }

    std::tm gmtime(const std::time_t& time)
    {
        std::tm tm{};
        if (!gmtime_r(&time, &tm))
        {
            // tm is already zero-initialized.
        }
        return tm;
    }

    timer::timer()
        : start_time(get_time_now()), m_is_running(true)
    {
    }

    void timer::start()
    {
        m_is_running = true;
        start_time   = get_time_now();
    }

    void timer::stop()
    {
        if (!m_is_running) return;       // idempotent
        stop_time    = get_time_now();
        m_is_running = false;
    }

    void timer::reset() noexcept
    {
        m_is_running = false;
        start_time   = time_point();
        stop_time    = time_point();
    }

    double timer::get_elapsed_time() const
    {
        const auto end = m_is_running ? get_time_now() : stop_time;
        return std::chrono::duration<double>(end - start_time).count();
    }

    long long timer::get_elapsed_milliseconds() const
    {
        const auto end = m_is_running ? get_time_now() : stop_time;
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start_time).count();
    }

    timer::time_point timer::get_time_now()
    {
        return std::chrono::steady_clock::now();
    }
}
