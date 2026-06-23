#pragma once

#include <stdlib.h>
#include <sys/types.h>
#include <sstream>
#include <string>
#include <chrono>
#include <tuple>
#include "file_utils.h"

namespace minerva
{

class log
{
public:

    /*
     * Log levels are ordered by severity. set_log_level(X) emits any
     * message whose level is >= X. Use NONE (the highest sentinel) to
     * silence all output.
     */
    enum LOG_LEVEL {
        TRACE = 0,
        DEBUG = 1,
        INFO  = 2,
        WARN  = 3,
        ERROR = 4,
        FATAL = 5,
        NONE  = 6
    };

    constexpr static const char* TRACE_STRING = "TRACE";
    constexpr static const char* DEBUG_STRING = "DEBUG";
    constexpr static const char* INFO_STRING  = "INFO";
    constexpr static const char* WARN_STRING  = "WARN";
    constexpr static const char* ERROR_STRING = "ERROR";
    constexpr static const char* FATAL_STRING = "FATAL";
    constexpr static const char* time_format_string = "%Y/%m/%d %H:%M:%S";

    static std::string strerror_string(int err);

    static std::tuple<std::time_t, int> get_systime();

    static pid_t gettid();

    inline static LOG_LEVEL get_log_level()
    {
        return log_level;
    }

    static void set_log_level(LOG_LEVEL level);

    static std::string format_current_time();

    static void log_message(const std::string& msg,
                            const char* level_string,
                            const char* pretty_name,
                            const char* file_name,
                            const int line_no);

    static void log_errno_message(const std::string& msg,
                                  const char* level_string,
                                  const char* pretty_name,
                                  const char* file_name,
                                  const int line_no,
                                  int err);

    /* Flushes all log sinks. Safe to call from a fatal-error handler. */
    static void flush();

private:
    static LOG_LEVEL log_level;
};

}

#define LOG(level, level_string, filename, line, args)                  \
    do                                                                  \
    {                                                                   \
        if (level >= minerva::log::get_log_level())                     \
        {                                                               \
            std::stringstream _mlog_ss_;                                \
            _mlog_ss_ << args;                                          \
            minerva::log::log_message(_mlog_ss_.str(),                  \
                                      level_string, __func__,           \
                                      filename, line);                  \
        }                                                               \
    } while (0)

#define LOG_ERRNO(level, level_string, filename, line, args, err)       \
    do                                                                  \
    {                                                                   \
        if (level >= minerva::log::get_log_level())                     \
        {                                                               \
            std::stringstream _mlog_ss_;                                \
            _mlog_ss_ << args;                                          \
            minerva::log::log_errno_message(_mlog_ss_.str(),            \
                                            level_string, __func__,     \
                                            filename, line, err);       \
        }                                                               \
    } while (0)

#define LOG_TRACE(args)                                                 \
    LOG(minerva::log::TRACE, minerva::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_TRACE_ERRNO(args, err)                                      \
    LOG_ERRNO(minerva::log::TRACE, minerva::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args, err)

#define LOG_DEBUG(args)                                                 \
    LOG(minerva::log::DEBUG, minerva::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_DEBUG_ERRNO(args, err)                                      \
    LOG_ERRNO(minerva::log::DEBUG, minerva::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args, err)

#define LOG_INFO(args)                                                  \
    LOG(minerva::log::INFO, minerva::log::INFO_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_INFO_ERRNO(args, err)                                       \
    LOG_ERRNO(minerva::log::INFO, minerva::log::INFO_STRING, __SHORT_FILE__, __LINE__, args, err)

#define LOG_WARN(args)                                                  \
    LOG(minerva::log::WARN, minerva::log::WARN_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_WARN_ERRNO(args, err)                                       \
    LOG_ERRNO(minerva::log::WARN, minerva::log::WARN_STRING, __SHORT_FILE__, __LINE__, args, err)

#define LOG_ERROR(args)                                                 \
    LOG(minerva::log::ERROR, minerva::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_ERROR_ERRNO(args, err)                                      \
    LOG_ERRNO(minerva::log::ERROR, minerva::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args, err)

#define LOG_FATAL(args)                                                 \
    LOG(minerva::log::FATAL, minerva::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args)

#define LOG_FATAL_ERRNO(args, err)                                      \
    LOG_ERRNO(minerva::log::FATAL, minerva::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args, err)

#define FATAL(args)                                                     \
    do                                                                  \
    {                                                                   \
        LOG(minerva::log::FATAL, minerva::log::FATAL_STRING,            \
            __SHORT_FILE__, __LINE__, args);                            \
        minerva::log::flush();                                          \
        abort();                                                        \
    } while (0)

#define FATAL_ERRNO(args, err)                                          \
    do                                                                  \
    {                                                                   \
        LOG_ERRNO(minerva::log::FATAL, minerva::log::FATAL_STRING,      \
                  __SHORT_FILE__, __LINE__, args, err);                 \
        minerva::log::flush();                                          \
        abort();                                                        \
    } while (0)
