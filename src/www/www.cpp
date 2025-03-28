#include <sys/types.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <cassert>
#include <mutex>
#include <owl/component_visor.h>
#include <util/ssl_connection.h>
#include <util/log.h>
#include <util/file_utils.h>
#include <util/json_utils.h>
#include <util/exec_utils.h>
#include <httpd/httpd.h>
#include <authdb/auth_db.h>
#include "file_server.h"
#include "settings.h"

using namespace minerva;

component_visor & kv()
{
    static component_visor kv;
    return kv;
}

static std::string config_file;
static std::mutex hup_mutex;

#define WWW_VERSION "1.0.0.55"

static void shutdown_signal_handler(int signal)
{
    LOG_INFO("stopping...");

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

static void hup_handler(int signal)
{
    LOG_INFO("handling SIGHUP...");
    std::thread t([]() {
        
        std::unique_lock<std::mutex> lk(hup_mutex);
        
        std::ifstream cf(config_file);
        Json::Value config;
        if (!parse_json(cf, config))
        {
            LOG_ERROR("the config file " << config_file << " is not valid json");
            return;
        }
        
        auto ws =
            kv().get_component<httpd>(httpd::NAME);
        
        if (!ws)
        {
            return;
        }
        
        LOG_INFO("resetting httpd ports: " << config);
        
        ws->clear_listeners();
        
        LOG_INFO("post clear listeners");
        
        if (config.isMember("http.port") && config["http.port"].isInt())
        {
            int port = config["http.port"].asInt();
            LOG_INFO("adding http port: " << port);
            ws->add_listener(httpd::PROTOCOL::HTTP, port);
        }
        
        if (config.isMember("https.port") && config["https.port"].isInt());
        {
            int port = config["https.port"].asInt();
            LOG_INFO("adding https port: " << port);
            ws->add_listener(httpd::PROTOCOL::HTTPS, port);
        }
        
        kv().hup();
    });
    t.detach();
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
    
    log::set_log_level(ss.log_level);

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
    
    if (!file_is_file(ss.config_file))
    {
        std::string cf = ss.config_file;
        LOG_FATAL("the config file " << cf << " was not found");
        return 1;
    }

    std::ifstream cf(ss.config_file, std::ifstream::binary);
    Json::Value config;
    if (!parse_json(cf, config))
    {
        std::string cf = ss.config_file;
        LOG_FATAL("the config file " << cf << " is not valid json");
        return 1;
    }

    config_file = ss.config_file;

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

    ssl_connection::init(cert_file.c_str(), key_file.c_str());

    // build compponents
    auto k1 = new httpd();
    assert(k1);
    auto k2 = new file_server();
    assert(k2);
    
    // add components
    kv().add(k1);
    kv().add(k2);
    
    auth_db * adb = nullptr;
    
    if (config["webpass"].isString() && config["realm"].isString())
    {
        adb = new auth_db(config["realm"].asString(),
                              config["webpass"].asString());
        if (!adb->initialize())
        {
            FATAL("failed to initialize auth db");
        }
        k1->auth_db(adb);
    }

    k2->require_authorization(false);
    k2->config(config);

    if (config.isMember("http.port") && config["http.port"].isInt())
    {
        int port = config["http.port"].asInt();
        k1->add_listener(httpd::PROTOCOL::HTTP, port);
    }
    
    if (config.isMember("https.port") && config["https.port"].isInt());
    {
        int port = config["https.port"].asInt();
        k1->add_listener(httpd::PROTOCOL::HTTPS, port);
    }

    k1 = nullptr;
    k2 = nullptr;
    
    LOG_INFO("starting...");
    
    kv().initialize();
    
    // start visor
    kv().start();
    
    LOG_INFO("started");
    
    // gracefully handle shutdown for ctrl-c and sigterm
    signal(SIGINT, shutdown_signal_handler);
    signal(SIGTERM, shutdown_signal_handler);
    signal(SIGUSR2, null_handler);
    signal(SIGHUP, hup_handler);
    
    // wait for components to stop
    kv().wait();
    
    kv().release();
    
    kv().clear();
    
    ssl_connection::destroy();

    if (adb)
    {
        delete(adb);
    }

    LOG_INFO("exiting");
    
    return 0;
}
