#pragma once

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>
#include <algorithm>

namespace minerva
{

    // Aggregate counters for a basher run.  All counters are atomic so worker
    // threads can update them concurrently.
    class basher_stats
    {
    public:
        std::atomic<uint64_t> sent{0};
        std::atomic<uint64_t> ok{0};            // request succeeded and verified
        std::atomic<uint64_t> mismatch{0};      // response body/status mismatch
        std::atomic<uint64_t> errors{0};        // transport/connection errors
        std::atomic<uint64_t> status_2xx{0};
        std::atomic<uint64_t> status_3xx{0};
        std::atomic<uint64_t> status_4xx{0};
        std::atomic<uint64_t> status_5xx{0};
        std::atomic<uint64_t> conn_new{0};
        std::atomic<uint64_t> conn_reuse{0};
        std::atomic<uint64_t> fault_sent{0};
        std::atomic<uint64_t> fault_handled{0}; // server rejected/closed, stayed up
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_recv{0};

        // Latency accumulation in microseconds.
        std::atomic<uint64_t> latency_sum_us{0};
        std::atomic<uint64_t> latency_count{0};
        std::atomic<uint64_t> latency_max_us{0};

        void record_latency(uint64_t us)
        {
            latency_sum_us.fetch_add(us, std::memory_order_relaxed);
            latency_count.fetch_add(1, std::memory_order_relaxed);
            uint64_t prev = latency_max_us.load(std::memory_order_relaxed);
            while (us > prev &&
                   !latency_max_us.compare_exchange_weak(prev, us,
                                                         std::memory_order_relaxed))
            {
            }
        }

        void record_status(int code)
        {
            if (code >= 200 && code < 300) status_2xx.fetch_add(1);
            else if (code >= 300 && code < 400) status_3xx.fetch_add(1);
            else if (code >= 400 && code < 500) status_4xx.fetch_add(1);
            else if (code >= 500) status_5xx.fetch_add(1);
        }

        void print(std::ostream & os) const
        {
            uint64_t lcount = latency_count.load();
            double avg_us = lcount ? static_cast<double>(latency_sum_us.load()) / lcount
                                   : 0.0;
            os << "\n==== basher results ====\n";
            os << "requests sent      : " << sent.load() << "\n";
            os << "verified ok        : " << ok.load() << "\n";
            os << "mismatches         : " << mismatch.load() << "\n";
            os << "transport errors   : " << errors.load() << "\n";
            os << "status 2xx         : " << status_2xx.load() << "\n";
            os << "status 3xx         : " << status_3xx.load() << "\n";
            os << "status 4xx         : " << status_4xx.load() << "\n";
            os << "status 5xx         : " << status_5xx.load() << "\n";
            os << "new connections    : " << conn_new.load() << "\n";
            os << "reused connections : " << conn_reuse.load() << "\n";
            os << "fault requests     : " << fault_sent.load() << "\n";
            os << "fault handled ok   : " << fault_handled.load() << "\n";
            os << "bytes sent         : " << bytes_sent.load() << "\n";
            os << "bytes received     : " << bytes_recv.load() << "\n";
            os << "latency avg (us)   : " << avg_us << "\n";
            os << "latency max (us)   : " << latency_max_us.load() << "\n";
            os << "========================\n";
        }

        // The run is considered a failure if any request mismatched, or a
        // non-fault request produced a transport error / unexpected 5xx.
        bool failed() const
        {
            return mismatch.load() != 0;
        }
    };
}
