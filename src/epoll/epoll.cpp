#include <cassert>
#include <unistd.h>
#include <signal.h>
#include <ovhttpd/log.h>
#include <ovhttpd/component_visor.h>
#include <ovhttpd/httpd.h>
#include "kernel3.h"
#include "kernel2.h"
#include "kernel1.h"
#include "kernel_stats.h"
#include "kernel_test.h"
#include "settings.h"

using namespace epoll;

static ovhttpd::component_visor kv;

static void shutdown_signal_handler(int signal)
{
    LOG_INFO("stopping...");
    std::thread t([]() {
            kv.stop();
        });
    t.detach();
}

static void null_handler(int signal)
{
}

static void stats_handler(int signal)
{
    std::thread t([]() {
            kv.dump_stats();
        });
    t.detach();
}

int main(int argc, char** argv)
{
    // ignore sigpipe since we don't want the process to exit
    signal(SIGPIPE, SIG_IGN);
    
    settings ss;
    if (!ss.parse_command_line(argc, argv))
    {
        settings::print_usage();
        return 1;
    }
    
    ovhttpd::log::set_log_level(ss.log_level);
    
    // build kernels
    auto k1 = std::make_shared<kernel1>();
    assert(k1);
//    auto k2 = std::make_shared<kernel2>();
//    assert(k2);
    auto k3 = std::make_shared<ovhttpd::httpd>();
    assert(k3);
    k3->username("admin");
    k3->password("Password1");
    auto k4 = std::make_shared<kernel_stats>();
    assert(k4);
    auto k5 = std::make_shared<kernel3>();
    assert(k5);
    auto k6 = std::make_shared<kernel_test>();
    assert(k5);
    
    // configure kernel 1
    k1->set_listen_port(ss.proxy_listen_port);
    
    // add kernels
    kv.add(k1);
//    kv.add(k2);
    kv.add(k3);
    kv.add(k4);
    kv.add(k5);
    kv.add(k6);
    
    k1 = nullptr;
//    k2 = nullptr;
    k3 = nullptr;
    k4 = nullptr;
    k5 = nullptr;
    k6 = nullptr;
    
    LOG_INFO("starting...");
    
    kv.initialize();
    
    // start kernels
    kv.start();
    
    LOG_INFO("started");
    
    // gracefully handle shutdown for ctrl-c and sigterm
    signal(SIGINT, shutdown_signal_handler);
    signal(SIGTERM, shutdown_signal_handler);
    signal(SIGHUP, stats_handler);
    signal(SIGUSR1, null_handler);
    
    // wait for kernels to stop
    kv.wait();
    
    kv.release();
    
    kv.clear();
    
    LOG_INFO("exiting");
    
    return 0;
}
