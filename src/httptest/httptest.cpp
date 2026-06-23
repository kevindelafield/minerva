#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>

#include <owl/component_visor.h>
#include <util/log.h>
#include <util/ssl_connection.h>
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
            "usage: httptest [options]\n"
            "  --port N         HTTP listen port (default 8080, 0 to disable)\n"
            "  --https-port N   HTTPS listen port (default disabled)\n"
            "  --cert FILE      TLS certificate file (required with --https-port)\n"
            "  --key FILE       TLS private key file (required with --https-port)\n"
            "  --log-level L    log level 0-6 (default 3)\n"
            "\n"
            "Generate a self-signed cert/key with tools/generate_cert.sh, then:\n"
            "  httptest --https-port 8443 --cert cert.pem --key key.pem\n");
}

int main(int argc, char ** argv)
{
    signal(SIGPIPE, SIG_IGN);

    int port = 8080;
    int https_port = 0;
    int log_level = 3;
    std::string cert_file;
    std::string key_file;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--https-port") == 0 && i + 1 < argc)
        {
            https_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--cert") == 0 && i + 1 < argc)
        {
            cert_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc)
        {
            key_file = argv[++i];
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

    if (https_port > 0 && (cert_file.empty() || key_file.empty()))
    {
        LOG_FATAL("--https-port requires both --cert and --key");
        print_usage();
        return 1;
    }

    bool tls_enabled = https_port > 0;
    if (tls_enabled)
    {
        // Load the cert/key produced by tools/generate_cert.sh into the
        // process-wide SSL context used by ssl_connection.
        ssl_connection::init(cert_file.c_str(), key_file.c_str());
        LOG_INFO("httptest TLS enabled with cert " << cert_file
                 << " key " << key_file);
    }

    LOG_INFO("httptest starting (http port " << port
             << ", https port " << https_port << ")");

    auto server = new httpd();
    kv().add(server);

    // Controllers are plain objects owned by main; they outlive the server.
    echo_controller echo;
    raw_controller raw;

    server->register_controller("echo", &echo);
    server->register_default_controller(&raw);

    if (port > 0)
    {
        server->add_listener(httpd::PROTOCOL::HTTP, port);
    }
    if (tls_enabled)
    {
        server->add_listener(httpd::PROTOCOL::HTTPS, https_port);
    }

    kv().initialize();
    kv().start();

    LOG_INFO("httptest started");

    signal(SIGINT, shutdown_signal_handler);
    signal(SIGTERM, shutdown_signal_handler);

    kv().wait();
    kv().release();
    kv().clear();

    if (tls_enabled)
    {
        ssl_connection::destroy();
    }

    LOG_INFO("httptest exiting");
    return 0;
}
