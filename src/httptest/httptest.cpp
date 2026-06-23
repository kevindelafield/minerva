#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>

#include <owl/component_visor.h>
#include <util/log.h>
#include <httpd/httpd.h>

#include "echo_controller.h"
#include "raw_controller.h"

using namespace minerva;

static component_visor & kv()
{
    static component_visor kv;
    return kv;
}

static void shutdown_signal_handler(int)
{
    LOG_INFO("httptest stopping...");
    std::thread t([]() { kv().stop(); });
    t.detach();
}

static void print_usage()
{
    fprintf(stderr,
            "usage: httptest [--port N] [--log-level LEVEL]\n"
            "  --port N         HTTP listen port (default 8080)\n"
            "  --log-level L    log level 0-5 (default 3)\n");
}

int main(int argc, char ** argv)
{
    signal(SIGPIPE, SIG_IGN);

    int port = 8080;
    int log_level = 3;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--log-level") == 0 && i + 1 < argc)
        {
            log_level = std::atoi(argv[++i]);
        }
        else
        {
            print_usage();
            return 1;
        }
    }

    log::set_log_level(static_cast<log::LOG_LEVEL>(log_level));

    LOG_INFO("httptest starting on port " << port);

    auto server = new httpd();
    kv().add(server);

    // Controllers are plain objects owned by main; they outlive the server.
    echo_controller echo;
    raw_controller raw;

    server->register_controller("echo", &echo);
    server->register_default_controller(&raw);
    server->add_listener(httpd::PROTOCOL::HTTP, port);

    kv().initialize();
    kv().start();

    LOG_INFO("httptest started");

    signal(SIGINT, shutdown_signal_handler);
    signal(SIGTERM, shutdown_signal_handler);

    kv().wait();
    kv().release();
    kv().clear();

    LOG_INFO("httptest exiting");
    return 0;
}
