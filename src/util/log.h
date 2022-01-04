#pragma once

#include <stdlib.h>
#include <sys/types.h>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <tuple>
#include "file_utils.h"

namespace util
{

class log
{
public:

    enum LOG_LEVEL {
        NONE = 0,
        TRACE = 1,
        DEBUG = 2,
        INFO = 3,
        WARN = 4,
        ERROR = 5,
        FATAL = 6
    };

    constexpr static const char* TRACE_STRING = "TRACE";
    constexpr static const char* DEBUG_STRING = "DEBUG";
    constexpr static const char* INFO_STRING = "INFO";
    constexpr static const char* WARN_STRING = "WARN";
    constexpr static const char* ERROR_STRING = "ERROR";
    constexpr static const char* FATAL_STRING = "FATAL";
    constexpr static const char* time_format_string = "%Y/%m/%d %H:%M:%S";

    static std::string strerror_string(int err);

    static std::tuple<std::time_t, std::chrono::seconds::rep> get_systime();

    static pid_t gettid();

    inline static LOG_LEVEL get_log_level()
    {
        return log_level;
    }

    static void set_log_level(LOG_LEVEL level);

    static std::string format_current_time();

    static void log_message(std::string& msg, const char* level_string,
                           const char* pretty_name,
                           const char* file_name,
                           const int line_no);

    static void log_errno_message(std::string& msg,
                                 const char* level_string,
                                 const char* pretty_name,
                                 const char* file_name,
                                 const int line_no,
                                 int err);

private:
    static LOG_LEVEL log_level;

};

}

#define LOG(level, level_string, filename, line, args)                  \
    {                                                                   \
        if (util::log::get_log_level() <= level)                        \
        {                                                               \
            std::stringstream ssx;                                      \
            ssx << args;                                                \
            std::string msg = ssx.str();                                \
            util::log::log_message(msg, level_string, __func__,      \
                                      filename, line);                  \
        }                                                               \
    }

#define LOG_ERRNO(level, level_string, filename, line, args, err)       \
    {                                                                   \
        if (util::log::get_log_level() <= level)                        \
        {                                                               \
            std::stringstream ssx;                                      \
            ssx << args;                                                \
            std::string msg = ssx.str();                                \
            util::log::log_errno_message(msg, level_string, __func__,   \
                                            filename, line, err);       \
        }                                                               \
    }

#define LOG_TRACE(args)                                                 \
    LOG(util::log::TRACE, util::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_TRACE_ERRNO(args, err)                                      \
    LOG_ERRNO(util::log::TRACE, util::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_DEBUG(args)                                                 \
    LOG(util::log::DEBUG, util::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_DEBUG_ERRNO(args, err)                                      \
    LOG_ERRNO(util::log::DEBUG, util::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_INFO(args)                                                  \
    LOG(util::log::INFO, util::log::INFO_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_INFO_ERRNO(args, err)                                       \
    LOG_ERRNO(util::log::INFO, util::log::INFO_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_WARN(args)                                                  \
    LOG(util::log::WARN, util::log::WARN_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_WARN_ERRNO(args, err)                                       \
    LOG_ERRNO(util::log::WARN, util::log::WARN_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_ERROR(args)                                                 \
    LOG(util::log::ERROR, util::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_ERROR_ERRNO(args, err)                                      \
    LOG_ERRNO(util::log::ERROR, util::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_FATAL(args)                                                 \
    LOG(util::log::FATAL, util::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_FATAL_ERRNO(args, err)                                      \
    LOG_ERRNO(util::log::FATAL, util::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args, err);

#define FATAL(args)                                                     \
    {                                                                   \
        LOG(util::log::FATAL, util::log::FATAL_STRING,                  \
            __SHORT_FILE__, __LINE__, args);                                  \
        std::cout.flush();                                              \
        abort();                                                        \
    }

#define FATAL_ERRNO(args, err)                                          \
    {                                                                   \
        LOG_ERRNO(util::log::FATAL, util::log::FATAL_STRING,            \
                  __SHORT_FILE__, __LINE__, args, err);                       \
        std::cout.flush();                                              \
        abort();                                                        \
    }
