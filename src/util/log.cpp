#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <fstream>
#include "log.h"
#include "time_utils.h"

namespace minerva
{

#define STRERROR_BUF_SIZE 64

    log::LOG_LEVEL log::log_level = log::INFO;

    class console
    {
    public:

        console() : out("/dev/console")
        {
        }

        ~console() = default;

        std::ofstream out;
    };

    static console console_log;

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
        char buf[STRERROR_BUF_SIZE];
        return std::string(strerror_r(err, buf, STRERROR_BUF_SIZE));
    }

    std::tuple<std::time_t, std::chrono::seconds::rep> log::get_systime()
    {
        std::chrono::system_clock::time_point t = std::chrono::system_clock::now();
        const std::chrono::duration<double> tse = t.time_since_epoch();
        std::chrono::seconds::rep milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(tse).count() % 1000;
        std::time_t now = std::chrono::system_clock::to_time_t(t);
        return std::make_tuple(now, milliseconds);
    }

    std::string log::format_current_time()
    {
        auto st = log::get_systime();
        std::tm tm = minerva::localtime(std::get<0>(st));
        auto put = std::put_time(&tm, log::time_format_string);
        std::stringstream ss;
        ss << put << " (" << std::get<1>(st) << "ms)";
        return ss.str();
    }

    void log::log_message(std::string& msg,
                          const char* level_string,
                          const char* pretty_name,
                          const char* file_name,
                          const int line_no)
    {
        std::stringstream ss;                                               
        ss << level_string << " " <<                                
            log::format_current_time() << " " <<                    
            log::gettid() << " ";
        if (file_name)
        {
            ss << file_name << ":" << line_no << " ";
        }
        ss << pretty_name << ": " <<  msg << std::endl;
        std::string m = ss.str();
        std::cerr << m;                                 
        console_log.out << m;                                 
        console_log.out.flush();
    }

    void log::log_errno_message(std::string& msg,
                                const char* level_string,
                                const char* pretty_name,
                                const char* file_name,
                                const int line_no,
                                int err)
    {
        std::stringstream ss;                                               
        ss << level_string << " " <<                                
            log::format_current_time() << " " <<                    
            log::gettid() << " ";
        if (file_name)
        {
            ss << file_name << ":" << line_no << " ";
        }
        ss << pretty_name << ": " << msg <<
            " errno=" << err << " (" << log::strerror_string(err) << ")" <<
            std::endl;                                      
        std::string m = ss.str();
        std::cerr << m;                                 
        console_log.out << m;                                 
        console_log.out.flush();
    }

}
