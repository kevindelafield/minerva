#pragma once

#include <queue>
#include <chrono>
#include <mutex>
#include <condition_variable>

namespace minerva
{
    class throttle
    {
    public:

        throttle(int duration_secs, int count) : 
           m_duration_secs(duration_secs), 
           m_count(count)
           {
           }

        ~throttle() = default;

        bool should_fire()
        {
            purge();
            return m_queue.size() < m_count;
        }

        void fire()
        {
            purge();
            m_queue.push(std::chrono::steady_clock::now());
        }

        void clear()
        {
            std::queue<std::chrono::time_point<std::chrono::steady_clock>> 
                empty;

            std::swap(m_queue, empty);
        }

        int backoff()
        {
            return m_duration_secs;
        }

        void backoff(int secs)
        {
            m_duration_secs = secs;
            purge();
        }

    private:

        void purge()
        {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::seconds(m_duration_secs);
            while (!m_queue.empty() && now - m_queue.front() > duration)
            {
                m_queue.pop();
            }
        }

        int m_count;
        int m_duration_secs;
        std::queue<std::chrono::time_point<std::chrono::steady_clock>> m_queue;
    };
}
