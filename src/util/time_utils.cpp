#include <chrono>
#include <sstream>
#include <ctime>
#include "time_utils.h"

namespace util
{

    std::string ctime(const std::time_t & time)    
    {
        char buf[30];
        std::stringstream ss(ctime_r(&time, buf));
        std::string ts;
        std::getline(ss, ts);
        return ts;
    }

    std::tm localtime(const std::time_t & time)
    {
        std::tm tm;
        localtime_r(&time, &tm);
        return tm;
    }

    std::tm gmtime(const std::time_t & time)
    {
        std::tm tm;
        gmtime_r(&time, &tm);
        return tm;
    }

    timer::timer(const bool startNow /* = false */) : m_is_running(false)
    {
        if (startNow)
        {
            this->start();
        }
    }


    void timer::start()
    {
        m_is_running = true;
        start_time = get_time_now();
    }


    void timer::stop()
    {
        m_is_running = false;
        stop_time = get_time_now();
    }


    void timer::reset() noexcept
    {
        m_is_running = false;
        start_time = time_point();
        stop_time = time_point();
    }


    double timer::get_elapsed_time() const
    {
        std::chrono::duration<double> elapsedSeconds{};

        if (m_is_running)
        {
            elapsedSeconds = get_time_now() - start_time;
        }
        else
        {
            elapsedSeconds = stop_time - start_time;
        }
        return elapsedSeconds.count();
    }

    long long timer::get_elapsed_milliseconds() const
    {
        std::chrono::duration<long long, std::milli> elapsedSeconds{};

        if (m_is_running)
        {
            elapsedSeconds = 
                std::chrono::duration_cast<std::chrono::milliseconds>(get_time_now() - start_time);
        }
        else
        {
            elapsedSeconds = 
                std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time);
        }
        return elapsedSeconds.count();
    }

    timer::time_point timer::get_time_now()
    {
        return std::chrono::steady_clock::now();
    }


    bool timer::is_running()
    {
        return this->m_is_running;
    }

}
