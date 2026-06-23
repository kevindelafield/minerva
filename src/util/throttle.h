#pragma once

#include <chrono>
#include <queue>
#include <stdexcept>

namespace minerva
{
    /**
     * Sliding-window rate limiter.
     *
     * Tracks the timestamps of the last few fire() calls and admits a new
     * one only when fewer than @p count of them fall within the trailing
     * window of @p window_seconds. Events whose age is >= window_seconds
     * are forgotten.
     *
     * NOT thread-safe: callers must serialize access externally.
     *
     * Typical use:
     *     if (t.should_fire()) { do_action(); t.fire(); }
     *
     * should_fire() is advisory -- it does not reserve a slot. fire() is
     * what actually records the event.
     */
    class throttle
    {
    public:
        throttle(int window_seconds, int count)
            : m_count(count), m_window_secs(window_seconds)
        {
            if (count <= 0)
            {
                throw std::invalid_argument(
                    "throttle: count must be positive");
            }
            if (window_seconds < 0)
            {
                throw std::invalid_argument(
                    "throttle: window_seconds must be non-negative");
            }
        }

        // True if a subsequent fire() would be within the rate limit.
        bool should_fire()
        {
            purge();
            return m_queue.size() < static_cast<std::size_t>(m_count);
        }

        // Record an event at "now". Does not enforce the limit; callers
        // are expected to gate this with should_fire() if they want to.
        void fire()
        {
            purge();
            m_queue.push(std::chrono::steady_clock::now());
        }

        void clear()
        {
            std::queue<std::chrono::steady_clock::time_point> empty;
            std::swap(m_queue, empty);
        }

        int  window_seconds() const { return m_window_secs; }

        void set_window_seconds(int secs)
        {
            if (secs < 0)
            {
                throw std::invalid_argument(
                    "throttle: window_seconds must be non-negative");
            }
            m_window_secs = secs;
            purge();
        }

    private:
        void purge()
        {
            const auto now = std::chrono::steady_clock::now();
            const auto window = std::chrono::seconds(m_window_secs);
            // Forget events whose age is >= the window.
            while (!m_queue.empty() && (now - m_queue.front()) >= window)
            {
                m_queue.pop();
            }
        }

        int m_count;
        int m_window_secs;
        std::queue<std::chrono::steady_clock::time_point> m_queue;
    };
}
