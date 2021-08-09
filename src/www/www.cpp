#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <cassert>
#include <owl/log.h>
#include <owl/component_visor.h>
#include <owl/httpd.h>
#include <owl/ssl_connection.h>
#include <owl/file_utils.h>
#include <owl/json_utils.h>
#include <owl/exec_utils.h>
#include "kernel_stats.h"
#include "kernel_default.h"
#include "settings.h"

using namespace www;

owl::component_visor & kv()
{
    static owl::component_visor kv;
    return kv;
}

static volatile bool stopped = false;
static volatile bool resetting = false;

#define WWW_VERSION "1.0.0.55"

static void shutdown_signal_handler(int signal)
{
    LOG_INFO("stopping...");

    if (!stopped)
    {
        stopped = true;

        // set a timer to kill the process if it hasn't stopped after 7 seconds
        struct sigevent te;
        timer_t timer;
        struct itimerspec its;

        te.sigev_notify = SIGEV_SIGNAL;
        te.sigev_signo = SIGKILL;

        timer_create(CLOCK_MONOTONIC, &te, &timer);

        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 5;
        its.it_value.tv_nsec = 0;

        timer_settime(timer, 0, &its, nullptr);

        // stop the visor
        std::thread t([]() {
                kv().stop();
            });
        t.detach();
    }
}

static void hup_handler(int signal)
{
    LOG_INFO("handling SIGHUP...");
    if (!stopped)
    {
        std::thread t([]() {
                kv().hup();
            });
        t.detach();
    }
}

static void null_handler(int signal)
{
}

int main(int argc, char** argv)
{
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/usr/bin/X11:/usr/local/bin:/usr/local/sbin", 1);

    // ignore sigpipe since we don't want the process to exit
    signal(SIGPIPE, SIG_IGN);
    
    settings ss;
    if (!ss.parse_command_line(argc, argv))
    {
        settings::print_usage();
        return 1;
    }
    
    owl::log::set_log_level(ss.log_level);

    LOG_INFO("www application version " << WWW_VERSION);
    
    if (ss.print_version)
    {
        return 0;
    }

    if (ss.config_file.empty())
    {
        LOG_FATAL("the config file location was not included on the command line");
        return 1;
    }
    
    if (!owl::file_is_file(ss.config_file))
    {
        std::string cf = ss.config_file;
        LOG_FATAL("the config file " << cf << " was not found");
        return 1;
    }

    std::ifstream cf(ss.config_file, std::ifstream::binary);
    Json::Value config;
    if (!owl::parse_json(cf, config))
    {
        std::string cf = ss.config_file;
        LOG_FATAL("the config file " << cf << " is not valid json");
        return 1;
    }

    std::string cert_file;
    std::string key_file;

    if (!config.isMember("cert_file") || !config["cert_file"].isString())
    {
        LOG_INFO("defaulting cert file to /etc/stunnel/cert.pem");
        cert_file = "/etc/stunnel/cert.pem";
    }
    else
    {
        cert_file = config["cert_file"].asString();
    }

    if (!config.isMember("key_file") || !config["key_file"].isString())
    {
        LOG_INFO("defaulting key file to /etc/stunnel/key.pem");
        key_file = "/etc/stunnel/key.pem";
    }
    else
    {
        key_file = config["key_file"].asString();
    }

    owl::ssl_connection::init(cert_file.c_str(), key_file.c_str());

    kv().set_config(config);

    // build kernels
    auto k1 = std::make_shared<owl::httpd>();
    assert(k1);
    auto k2 = std::make_shared<kernel_stats>();
    assert(k2);
    auto k3 = std::make_shared<kernel_default>();
    assert(k3);
    
    // add kernels
    kv().add(k1);
    kv().add(k2);
    kv().add(k3);
    
    k1 = nullptr;
    k2 = nullptr;
    k3 = nullptr;
    
    LOG_INFO("starting...");
    
    kv().initialize();
    
    // start kernels
    kv().start();
    
    LOG_INFO("started");
    
    // gracefully handle shutdown for ctrl-c and sigterm
    signal(SIGINT, shutdown_signal_handler);
    signal(SIGTERM, shutdown_signal_handler);
    signal(SIGUSR2, null_handler);
    signal(SIGHUP, hup_handler);
    
    // wait for kernels to stop
    kv().wait();
    
    kv().release();
    
    kv().clear();
    
    owl::ssl_connection::destroy();

    LOG_INFO("exiting");
    
    return 0;
}
