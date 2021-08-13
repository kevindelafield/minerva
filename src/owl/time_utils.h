#pragma once

#include <chrono>
#include <ctime>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <time.h>

namespace owl
{
    std::string ctime(const std::time_t & time);

    std::tm localtime(const std::time_t & time);

    std::tm gmtime(const std::time_t & time);

    class timer
    {
    public:
        /// Convenience alias
        using time_point = std::chrono::steady_clock::time_point;

        /// Constructor
        timer(const bool startNow = false);

        /// Destructor
        virtual ~timer() = default;

        /// Start the timer
        virtual void start();

        /// Stop the timer
        virtual void stop();

        /// Clears the timer
        virtual void reset() noexcept;

        /// Get the elapsed time in seconds
        [[nodiscard]] virtual double get_elapsed_time() const;

        /// Get the elapsed time in milliseconds
        [[nodiscard]] virtual long long get_elapsed_milliseconds() const;

        /// Determine if the timer is currently running
        virtual bool is_running();

        /*
         * Disable copying / move semantics
         */
        timer(const timer& other) = default;
               
        timer& operator= (const timer& other)
            {
                if (this != &other)
                {
                    start_time = other.start_time;
                    stop_time = other.stop_time;
                    m_is_running = other.m_is_running;
                }
                return *this;
            }

        timer(timer&&) = delete;
        timer& operator= (timer&&) = delete;


    protected:
        static time_point get_time_now();

    private:
        time_point start_time;
        time_point stop_time;
        bool m_is_running;
    };

}
