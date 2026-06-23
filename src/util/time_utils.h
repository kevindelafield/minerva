#pragma once

#include <chrono>
#include <ctime>
#include <string>

namespace minerva
{
    /**
     * Thread-safe wrappers around the C time formatters.
     *
     * On invalid input (e.g. a time_t that cannot be represented), ctime()
     * returns an empty string and the *time wrappers return a zeroed std::tm.
     */
    std::string ctime(const std::time_t& time);
    std::tm     localtime(const std::time_t& time);
    std::tm     gmtime(const std::time_t& time);

    /**
     * Steady-clock stopwatch.
     *
     * NOT thread-safe: callers must serialize access externally.
     *
     * Lifecycle:
     *   - Default-constructed timer is already running ("now").
     *   - start()  : (re)start. Discards any prior elapsed time.
     *   - stop()   : freeze elapsed at "now". No-op if already stopped.
     *   - reset()  : clear and stop.
     */
    class timer
    {
    public:
        using time_point = std::chrono::steady_clock::time_point;

        timer();

        timer(const timer&)            = delete;
        timer& operator=(const timer&) = delete;
        timer(timer&&)                 = delete;
        timer& operator=(timer&&)      = delete;

        void start();
        void stop();
        void reset() noexcept;

        double    get_elapsed_time()         const;
        long long get_elapsed_milliseconds() const;

        bool is_running() const { return m_is_running; }

    protected:
        static time_point get_time_now();

    private:
        time_point start_time{};
        time_point stop_time{};
        bool       m_is_running{false};
    };
}
