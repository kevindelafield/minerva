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

namespace owl
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
        if (owl::log::get_log_level() <= level)                     \
        {                                                               \
            std::stringstream ssx;                                      \
            ssx << args;                                                \
            std::string msg = ssx.str();                                \
            owl::log::log_message(msg, level_string, __func__,      \
                                      filename, line);                  \
        }                                                               \
    }

#define LOG_ERRNO(level, level_string, filename, line, args, err)       \
    {                                                                   \
        if (owl::log::get_log_level() <= level)                     \
        {                                                               \
            std::stringstream ssx;                                      \
            ssx << args;                                                \
            std::string msg = ssx.str();                                \
            owl::log::log_errno_message(msg, level_string, __func__, \
                                            filename, line, err);       \
        }                                                               \
    }

#define LOG_TRACE(args)                                                 \
    LOG(owl::log::TRACE, owl::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_TRACE_ERRNO(args, err)                                      \
    LOG_ERRNO(owl::log::TRACE, owl::log::TRACE_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_DEBUG(args)                                                 \
    LOG(owl::log::DEBUG, owl::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_DEBUG_ERRNO(args, err)                                      \
    LOG_ERRNO(owl::log::DEBUG, owl::log::DEBUG_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_INFO(args)                                                  \
    LOG(owl::log::INFO, owl::log::INFO_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_INFO_ERRNO(args, err)                                       \
    LOG_ERRNO(owl::log::INFO, owl::log::INFO_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_WARN(args)                                                  \
    LOG(owl::log::WARN, owl::log::WARN_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_WARN_ERRNO(args, err)                                       \
    LOG_ERRNO(owl::log::WARN, owl::log::WARN_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_ERROR(args)                                                 \
    LOG(owl::log::ERROR, owl::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_ERROR_ERRNO(args, err)                                      \
    LOG_ERRNO(owl::log::ERROR, owl::log::ERROR_STRING, __SHORT_FILE__, __LINE__, args, err);

#define LOG_FATAL(args)                                                 \
    LOG(owl::log::FATAL, owl::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args);

#define LOG_FATAL_ERRNO(args, err)                                      \
    LOG_ERRNO(owl::log::FATAL, owl::log::FATAL_STRING, __SHORT_FILE__, __LINE__, args, err);

#define FATAL(args)                                                     \
    {                                                                   \
        LOG(owl::log::FATAL, owl::log::FATAL_STRING,                              \
            __SHORT_FILE__, __LINE__, args);                                  \
        std::cout.flush();                                              \
        abort();                                                        \
    }

#define FATAL_ERRNO(args, err)                                          \
    {                                                                   \
        LOG_ERRNO(owl::log::FATAL, owl::log::FATAL_STRING,                        \
                  __SHORT_FILE__, __LINE__, args, err);                       \
        std::cout.flush();                                              \
        abort();                                                        \
    }
