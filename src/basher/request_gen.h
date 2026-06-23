#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "http_client.h"

namespace minerva
{

    struct basher_config
    {
        std::string host = "127.0.0.1";
        size_t max_size = 65536;
        double fault_rate = 0.0;
    };

    // A single generated request: the raw bytes to send plus the information
    // needed to verify the response.
    struct request_spec
    {
        enum kind { ECHO, CHECKSUM, SINK, STREAM, RAW, FAULT };

        kind k = ECHO;
        std::string raw_request;   // bytes to send on the wire
        std::string description;   // for diagnostics

        bool is_fault = false;
        bool force_new_conn = false; // faults always use a fresh connection
        bool close_after = false;    // close the connection after this request
        bool half_close = false;     // shutdown write side after sending (faults)

        // Verification (ignored for faults).
        int expected_status = 200;
        bool check_status = true;
        bool check_body = false;
        std::string expected_body;
        bool check_checksum = false;
        uint64_t expected_length = 0;
        uint64_t expected_checksum = 0;
    };

    class request_gen
    {
    public:
        explicit request_gen(const basher_config & cfg);

        // Produce the next request. keep_alive indicates whether the caller
        // intends to reuse the connection (affects the Connection header).
        request_spec next(std::mt19937_64 & rng, bool keep_alive);

    private:
        request_spec gen_normal(std::mt19937_64 & rng, bool keep_alive);
        request_spec gen_fault(std::mt19937_64 & rng);
        size_t pick_size(std::mt19937_64 & rng);

        basher_config m_cfg;
    };

    // Verify a response against a spec. Returns true if it matches. For fault
    // specs this always returns true (faults are evaluated separately).
    bool verify_response(const request_spec & spec,
                         const http_client::response & r);
}
