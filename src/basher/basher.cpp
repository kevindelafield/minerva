#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "http_client.h"
#include "request_gen.h"
#include "stats.h"

using namespace minerva;

namespace
{
    struct run_options
    {
        std::string host = "127.0.0.1";
        int port = 8080;
        int threads = 4;
        uint64_t count = 1000;
        uint64_t seed = 0;
        double fault_rate = 0.0;
        size_t max_size = 65536;
        double keepalive_rate = 0.5;
        int timeout_ms = 30000;
    };

    double uniform01(std::mt19937_64 & rng)
    {
        return static_cast<double>(rng() % 1000000ULL) / 1000000.0;
    }

    void worker(int id,
                const run_options & opt,
                basher_stats & stats,
                std::atomic<uint64_t> & remaining)
    {
        basher_config cfg;
        cfg.host = opt.host;
        cfg.max_size = opt.max_size;
        cfg.fault_rate = opt.fault_rate;

        std::mt19937_64 rng(opt.seed +
                            static_cast<uint64_t>(id) * 0x9e3779b97f4a7c15ULL + 1);
        request_gen gen(cfg);
        std::unique_ptr<http_client> conn;

        while (true)
        {
            // Claim one unit of work.
            uint64_t cur = remaining.load(std::memory_order_relaxed);
            if (cur == 0)
            {
                break;
            }
            if (!remaining.compare_exchange_weak(cur, cur - 1,
                                                 std::memory_order_relaxed))
            {
                continue;
            }

            bool want_keep = uniform01(rng) < opt.keepalive_rate;
            request_spec spec = gen.next(rng, want_keep);

            if (spec.force_new_conn || !want_keep)
            {
                conn.reset();
            }

            if (!conn || !conn->is_open())
            {
                conn = std::make_unique<http_client>(opt.host, opt.port,
                                                     opt.timeout_ms);
                if (!conn->open())
                {
                    stats.errors.fetch_add(1);
                    conn.reset();
                    continue;
                }
                stats.conn_new.fetch_add(1);
            }
            else
            {
                stats.conn_reuse.fetch_add(1);
            }

            stats.sent.fetch_add(1);
            if (spec.is_fault)
            {
                stats.fault_sent.fetch_add(1);
            }
            stats.bytes_sent.fetch_add(spec.raw_request.size());

            auto t0 = std::chrono::steady_clock::now();

            if (!conn->send_all(spec.raw_request.data(), spec.raw_request.size()))
            {
                if (spec.is_fault)
                {
                    stats.fault_handled.fetch_add(1);
                }
                else
                {
                    stats.errors.fetch_add(1);
                }
                conn.reset();
                continue;
            }

            http_client::response resp;
            // For faults that truncate the body, half-close so the server sees
            // EOF and can abort promptly instead of waiting for the timeout.
            if (spec.half_close)
            {
                conn->shutdown_write();
            }
            bool got = conn->read_response(resp);

            auto t1 = std::chrono::steady_clock::now();
            stats.record_latency(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));

            if (!got)
            {
                // Connection closed/errored before a full response. For faults
                // this is an acceptable way for the server to reject input.
                if (spec.is_fault)
                {
                    stats.fault_handled.fetch_add(1);
                }
                else
                {
                    uint64_t e = stats.errors.fetch_add(1);
                    if (e < 20)
                    {
                        fprintf(stderr, "[transport-error] %s reqbytes=%zu\n",
                                spec.description.c_str(), spec.raw_request.size());
                    }
                }
                conn.reset();
                continue;
            }

            stats.bytes_recv.fetch_add(resp.body.size());
            stats.record_status(resp.status_code);

            if (spec.is_fault)
            {
                // The server responded instead of crashing -> handled.
                stats.fault_handled.fetch_add(1);
                conn.reset();
                continue;
            }

            if (verify_response(spec, resp))
            {
                stats.ok.fetch_add(1);
            }
            else
            {
                uint64_t m = stats.mismatch.fetch_add(1);
                if (m < 10)
                {
                    fprintf(stderr,
                            "[mismatch] %s status=%d bodylen=%zu expected_status=%d\n",
                            spec.description.c_str(), resp.status_code,
                            resp.body.size(), spec.expected_status);
                }
            }

            if (spec.close_after || !resp.keep_alive)
            {
                conn.reset();
            }
        }
    }

    // After the run, confirm the server is still alive and serving correctly.
    bool liveness_check(const run_options & opt)
    {
        http_client c(opt.host, opt.port, opt.timeout_ms);
        if (!c.open())
        {
            return false;
        }
        std::string req =
            "GET /echo/stream?size=64&seed=7&mode=cl HTTP/1.1\r\n"
            "Host: " + opt.host + "\r\n"
            "Connection: close\r\n\r\n";
        if (!c.send_all(req.data(), req.size()))
        {
            return false;
        }
        http_client::response r;
        if (!c.read_response(r))
        {
            return false;
        }
        return r.status_code == 200 && r.body.size() == 64;
    }

    void print_usage()
    {
        fprintf(stderr,
                "usage: basher [options]\n"
                "  --host H            target host (default 127.0.0.1)\n"
                "  --port N            target port (default 8080)\n"
                "  --threads N         worker threads (default 4)\n"
                "  --count N           total requests (default 1000)\n"
                "  --seed N            base RNG seed (default 0)\n"
                "  --fault-rate F      probability 0..1 of malformed requests (default 0)\n"
                "  --max-size N        max body size in bytes (default 65536)\n"
                "  --keepalive-rate F  probability 0..1 of connection reuse (default 0.5)\n"
                "  --timeout N         per-request socket timeout ms (default 30000)\n");
    }
}

int main(int argc, char ** argv)
{
    signal(SIGPIPE, SIG_IGN);

    run_options opt;

    for (int i = 1; i < argc; ++i)
    {
        auto need = [&](const char * name) -> const char * {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "missing value for %s\n", name);
                exit(1);
            }
            return argv[++i];
        };

        if (std::strcmp(argv[i], "--host") == 0) opt.host = need("--host");
        else if (std::strcmp(argv[i], "--port") == 0) opt.port = std::atoi(need("--port"));
        else if (std::strcmp(argv[i], "--threads") == 0) opt.threads = std::atoi(need("--threads"));
        else if (std::strcmp(argv[i], "--count") == 0) opt.count = std::strtoull(need("--count"), nullptr, 10);
        else if (std::strcmp(argv[i], "--seed") == 0) opt.seed = std::strtoull(need("--seed"), nullptr, 10);
        else if (std::strcmp(argv[i], "--fault-rate") == 0) opt.fault_rate = std::atof(need("--fault-rate"));
        else if (std::strcmp(argv[i], "--max-size") == 0) opt.max_size = static_cast<size_t>(std::strtoull(need("--max-size"), nullptr, 10));
        else if (std::strcmp(argv[i], "--keepalive-rate") == 0) opt.keepalive_rate = std::atof(need("--keepalive-rate"));
        else if (std::strcmp(argv[i], "--timeout") == 0) opt.timeout_ms = std::atoi(need("--timeout"));
        else
        {
            print_usage();
            return 1;
        }
    }

    if (opt.threads < 1) opt.threads = 1;

    fprintf(stderr,
            "basher: host=%s port=%d threads=%d count=%llu fault-rate=%.3f "
            "max-size=%zu keepalive-rate=%.3f\n",
            opt.host.c_str(), opt.port, opt.threads,
            static_cast<unsigned long long>(opt.count), opt.fault_rate,
            opt.max_size, opt.keepalive_rate);

    basher_stats stats;
    std::atomic<uint64_t> remaining{opt.count};

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> pool;
    pool.reserve(opt.threads);
    for (int t = 0; t < opt.threads; ++t)
    {
        pool.emplace_back(worker, t, std::cref(opt), std::ref(stats),
                          std::ref(remaining));
    }
    for (auto & th : pool)
    {
        th.join();
    }

    auto end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                      .count() / 1000.0;

    stats.print(std::cout);
    if (secs > 0)
    {
        std::cout << "elapsed (s)        : " << secs << "\n";
        std::cout << "throughput (req/s) : " << (stats.sent.load() / secs) << "\n";
    }

    bool alive = liveness_check(opt);
    std::cout << "server liveness    : " << (alive ? "OK" : "FAILED") << "\n";

    bool failed = stats.failed() || !alive;
    std::cout << "result             : " << (failed ? "FAIL" : "PASS") << "\n";

    return failed ? 1 : 0;
}
