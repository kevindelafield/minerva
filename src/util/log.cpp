#include <unistd.h>
#include <sys/syscall.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <mutex>
#include <system_error>
#include "log.h"
#include "time_utils.h"

namespace minerva
{

    log::LOG_LEVEL log::log_level = log::INFO;

    namespace
    {
        class console_sink
        {
        public:
            console_sink() : out("/dev/console")
            {
                // If /dev/console is unavailable (non-root, no tty group,
                // chroot, etc.), out is in fail state and writes become no-ops.
            }

            std::ofstream out;
            std::mutex    mtx;
        };

        // Meyers singleton: constructed on first use, avoiding static-init
        // ordering issues with other translation units that may log from
        // their own static constructors.
        console_sink& console()
        {
            static console_sink instance;
            return instance;
        }

        // Serializes writes to std::cerr across threads.
        std::mutex& cerr_mutex()
        {
            static std::mutex m;
            return m;
        }
    }

    void log::set_log_level(log::LOG_LEVEL level)
    {
        log_level = level;
    }

    pid_t log::gettid()
    {
        return syscall(SYS_gettid);
    }

    std::string log::strerror_string(int err)
    {
        // std::system_category().message() is portable, thread-safe, and
        // sidesteps the XSI-vs-GNU strerror_r return-type fiasco.
        return std::system_category().message(err);
    }

    std::tuple<std::time_t, int> log::get_systime()
    {
        const auto t = std::chrono::system_clock::now().time_since_epoch();
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(t);
        const auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(t - secs);
        return std::make_tuple(static_cast<std::time_t>(secs.count()),
                               static_cast<int>(ms.count()));
    }

    std::string log::format_current_time()
    {
        const auto st = log::get_systime();
        std::tm tm = minerva::localtime(std::get<0>(st));
        std::stringstream ss;
        ss << std::put_time(&tm, log::time_format_string)
           << " (" << std::setw(3) << std::setfill('0')
           << std::get<1>(st) << "ms)";
        return ss.str();
    }

    static std::string format_prefix(const char* level_string,
                                     const char* pretty_name,
                                     const char* file_name,
                                     int line_no)
    {
        std::stringstream ss;
        ss << level_string << ' '
           << log::format_current_time() << ' '
           << log::gettid() << ' ';
        if (file_name)
        {
            ss << file_name << ':' << line_no << ' ';
        }
        ss << pretty_name << ": ";
        return ss.str();
    }

    static void emit(const std::string& line)
    {
        {
            std::lock_guard<std::mutex> lk(cerr_mutex());
            std::cerr << line;
            std::cerr.flush();
        }
        console_sink& cs = console();
        if (cs.out.is_open())
        {
            std::lock_guard<std::mutex> lk(cs.mtx);
            cs.out << line;
            cs.out.flush();
        }
    }

    void log::log_message(const std::string& msg,
                          const char* level_string,
                          const char* pretty_name,
                          const char* file_name,
                          const int line_no)
    {
        emit(format_prefix(level_string, pretty_name, file_name, line_no)
             + msg + '\n');
    }

    void log::log_errno_message(const std::string& msg,
                                const char* level_string,
                                const char* pretty_name,
                                const char* file_name,
                                const int line_no,
                                int err)
    {
        std::stringstream ss;
        ss << format_prefix(level_string, pretty_name, file_name, line_no)
           << msg << " errno=" << err
           << " (" << log::strerror_string(err) << ")\n";
        emit(ss.str());
    }

    void log::flush()
    {
        {
            std::lock_guard<std::mutex> lk(cerr_mutex());
            std::cerr.flush();
        }
        console_sink& cs = console();
        if (cs.out.is_open())
        {
            std::lock_guard<std::mutex> lk(cs.mtx);
            cs.out.flush();
        }
    }

}
