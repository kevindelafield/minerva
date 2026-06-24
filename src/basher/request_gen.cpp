#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <httptest/test_payload.h>

#include "request_gen.h"

namespace minerva
{

    request_gen::request_gen(const basher_config & cfg) : m_cfg(cfg)
    {
    }

    size_t request_gen::pick_size(std::mt19937_64 & rng)
    {
        // With ~40% probability pick a boundary-ish size to exercise edge cases.
        static const size_t boundaries[] = {
            0, 1, 2, 3, 255, 256, 257, 1023, 1024, 1025,
            8191, 8192, 8193, 16383, 16384, 16385, 65535, 65536
        };
        if ((rng() % 100) < 40)
        {
            size_t s = boundaries[rng() % (sizeof(boundaries) / sizeof(boundaries[0]))];
            return s > m_cfg.max_size ? m_cfg.max_size : s;
        }
        return static_cast<size_t>(rng() % (m_cfg.max_size + 1));
    }

    static std::string build_request(const std::string & method,
                                     const std::string & path,
                                     const std::string & host,
                                     const std::string & body,
                                     bool has_body,
                                     bool chunked,
                                     bool keep_alive,
                                     std::mt19937_64 & rng)
    {
        std::ostringstream os;
        os << method << " " << path << " HTTP/1.1\r\n";
        os << "Host: " << host << "\r\n";
        os << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";

        if (!has_body)
        {
            os << "\r\n";
            return os.str();
        }

        if (chunked)
        {
            os << "Transfer-Encoding: chunked\r\n\r\n";
            size_t off = 0;
            while (off < body.size())
            {
                size_t remaining = body.size() - off;
                size_t c = 1 + static_cast<size_t>(rng() % remaining);
                if (c > 16384)
                {
                    c = 16384;
                }
                os << std::hex << c << std::dec << "\r\n";
                os.write(body.data() + off, c);
                os << "\r\n";
                off += c;
            }
            os << "0\r\n\r\n";
        }
        else
        {
            os << "Content-Length: " << body.size() << "\r\n\r\n";
            os.write(body.data(), body.size());
        }
        return os.str();
    }

    // Build a request that carries a body framed with an explicit Content-Type
    // header (used for multipart/form-data).
    static std::string build_typed_request(const std::string & method,
                                           const std::string & path,
                                           const std::string & host,
                                           const std::string & body,
                                           const std::string & content_type,
                                           bool chunked,
                                           bool keep_alive,
                                           std::mt19937_64 & rng)
    {
        std::ostringstream os;
        os << method << " " << path << " HTTP/1.1\r\n";
        os << "Host: " << host << "\r\n";
        os << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
        os << "Content-Type: " << content_type << "\r\n";

        if (chunked)
        {
            os << "Transfer-Encoding: chunked\r\n\r\n";
            size_t off = 0;
            while (off < body.size())
            {
                size_t remaining = body.size() - off;
                size_t c = 1 + static_cast<size_t>(rng() % remaining);
                if (c > 16384)
                {
                    c = 16384;
                }
                os << std::hex << c << std::dec << "\r\n";
                os.write(body.data() + off, c);
                os << "\r\n";
                off += c;
            }
            os << "0\r\n\r\n";
        }
        else
        {
            os << "Content-Length: " << body.size() << "\r\n\r\n";
            os.write(body.data(), body.size());
        }
        return os.str();
    }

    // Incremental FNV-1a matching the httptest /echo/form handler.
    static void mp_fnv_update(uint64_t & h, const char * d, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            h ^= static_cast<unsigned char>(d[i]);
            h *= 1099511628211ULL;
        }
    }

    static void mp_fnv_field(uint64_t & h, const std::string & s)
    {
        mp_fnv_update(h, s.data(), s.size());
        char z = 0;
        mp_fnv_update(h, &z, 1);
    }

    request_spec request_gen::gen_normal(std::mt19937_64 & rng, bool keep_alive)
    {
        request_spec spec;
        spec.close_after = !keep_alive;

        // Choose an endpoint.
        int pick = static_cast<int>(rng() % 7);
        bool body_chunked = (rng() & 1) != 0;
        const char * resp_modes[] = {"", "?mode=cl", "?mode=chunked"};
        std::string resp_mode = resp_modes[rng() % 3];

        switch (pick)
        {
        case 0: // POST /echo/echo with body, verify echo
        {
            size_t n = pick_size(rng);
            uint32_t seed = static_cast<uint32_t>(rng());
            std::string body = test_payload::generate(seed, n);
            spec.k = request_spec::ECHO;
            spec.description = "POST /echo/echo";
            spec.raw_request = build_request("POST", "/echo/echo" + resp_mode,
                                             m_cfg.host, body, true, body_chunked,
                                             keep_alive, rng);
            spec.expected_status = 200;
            spec.check_body = true;
            spec.expected_body = body;
            break;
        }
        case 1: // POST /echo/checksum, verify JSON length + checksum
        {
            size_t n = pick_size(rng);
            uint32_t seed = static_cast<uint32_t>(rng());
            std::string body = test_payload::generate(seed, n);
            spec.k = request_spec::CHECKSUM;
            spec.description = "POST /echo/checksum";
            spec.raw_request = build_request("POST", "/echo/checksum",
                                             m_cfg.host, body, true, body_chunked,
                                             keep_alive, rng);
            spec.expected_status = 200;
            spec.check_checksum = true;
            spec.expected_length = n;
            spec.expected_checksum = test_payload::checksum(body.data(), body.size());
            break;
        }
        case 2: // POST /echo/sink, expect 204
        {
            size_t n = pick_size(rng);
            uint32_t seed = static_cast<uint32_t>(rng());
            std::string body = test_payload::generate(seed, n);
            spec.k = request_spec::SINK;
            spec.description = "POST /echo/sink";
            spec.raw_request = build_request("POST", "/echo/sink",
                                             m_cfg.host, body, true, body_chunked,
                                             keep_alive, rng);
            spec.expected_status = 204;
            break;
        }
        case 3: // GET /echo/stream?size=&seed=&mode=, verify generated body
        {
            size_t n = pick_size(rng);
            uint32_t seed = static_cast<uint32_t>(rng());
            bool resp_chunked = (rng() & 1) != 0;
            std::ostringstream path;
            path << "/echo/stream?size=" << n << "&seed=" << seed
                 << "&mode=" << (resp_chunked ? "chunked" : "cl");
            spec.k = request_spec::STREAM;
            spec.description = "GET /echo/stream";
            spec.raw_request = build_request("GET", path.str(), m_cfg.host,
                                             "", false, false, keep_alive, rng);
            spec.expected_status = 200;
            spec.check_body = true;
            spec.expected_body = test_payload::generate(seed, n);
            break;
        }
        case 4: // POST /raw/bytes (default controller, byte-array read), echo
        {
            size_t n = pick_size(rng);
            uint32_t seed = static_cast<uint32_t>(rng());
            std::string body = test_payload::generate(seed, n);
            spec.k = request_spec::RAW;
            spec.description = "POST /raw/bytes";
            spec.raw_request = build_request("POST", "/raw/bytes",
                                             m_cfg.host, body, true, body_chunked,
                                             keep_alive, rng);
            spec.expected_status = 200;
            spec.check_body = true;
            spec.expected_body = body;
            break;
        }
        case 5: // POST /echo/form multipart/form-data, verify summary
        {
            struct part_def
            {
                std::string name;
                std::string filename;
                std::string ctype;
                std::string data;
                bool is_file = false;
                bool has_ct = false;
            };

            // Deterministic but varied boundary.
            char btmp[17];
            std::snprintf(btmp, sizeof(btmp), "%016llx",
                          static_cast<unsigned long long>(rng()));
            std::string boundary = std::string("----basherBoundary") + btmp;

            int parts = 1 + static_cast<int>(rng() % 4); // 1..4 parts
            std::vector<part_def> ps;
            ps.reserve(parts);
            for (int k = 0; k < parts; ++k)
            {
                part_def p;
                p.name = "field" + std::to_string(k);
                p.is_file = (rng() & 1) != 0;
                if (p.is_file)
                {
                    p.filename = "file" + std::to_string(k) + ".bin";
                }
                p.has_ct = p.is_file || ((rng() & 1) != 0);
                if (p.has_ct)
                {
                    p.ctype = "application/octet-stream";
                }
                size_t n = pick_size(rng);
                uint32_t seed = static_cast<uint32_t>(rng());
                p.data = test_payload::generate(seed, n);
                ps.push_back(std::move(p));
            }

            // Assemble the multipart wire body.
            std::ostringstream wire;
            for (const auto & p : ps)
            {
                wire << "--" << boundary << "\r\n";
                wire << "Content-Disposition: form-data; name=\"" << p.name
                     << "\"";
                if (p.is_file)
                {
                    wire << "; filename=\"" << p.filename << "\"";
                }
                wire << "\r\n";
                if (p.has_ct)
                {
                    wire << "Content-Type: " << p.ctype << "\r\n";
                }
                wire << "\r\n";
                wire.write(p.data.data(), p.data.size());
                wire << "\r\n";
            }
            wire << "--" << boundary << "--\r\n";
            std::string mpbody = wire.str();

            const char * modes[] = {"full", "stream", "partial"};
            std::string rmode = modes[rng() % 3];
            size_t consumed = (rmode == "partial")
                ? static_cast<size_t>(1)
                : ps.size();

            // Reproduce the server-side fold over the consumed parts.
            uint64_t h = 1469598103934665603ULL;
            uint64_t total = 0;
            for (size_t k = 0; k < consumed; ++k)
            {
                mp_fnv_field(h, ps[k].name);
                mp_fnv_field(h, ps[k].filename);
                mp_fnv_field(h, ps[k].ctype);
                mp_fnv_update(h, ps[k].data.data(), ps[k].data.size());
                char z = 0;
                mp_fnv_update(h, &z, 1);
                total += ps[k].data.size();
            }

            std::string ct = "multipart/form-data; boundary=" + boundary;
            spec.k = request_spec::MULTIPART;
            spec.description = "POST /echo/form (" + rmode + ")";
            spec.raw_request = build_typed_request(
                "POST", "/echo/form?read=" + rmode, m_cfg.host, mpbody, ct,
                body_chunked, keep_alive, rng);
            spec.expected_status = 200;
            spec.check_multipart = true;
            spec.expected_count = consumed;
            spec.expected_length = total;
            spec.expected_checksum = h;
            break;
        }
        default: // DELETE /echo/echo, empty body, expect 200 empty
        {
            spec.k = request_spec::RAW;
            spec.description = "DELETE /echo/echo";
            spec.raw_request = build_request("DELETE", "/echo/echo",
                                             m_cfg.host, "", false, false,
                                             keep_alive, rng);
            spec.expected_status = 200;
            spec.check_body = true;
            spec.expected_body = "";
            break;
        }
        }
        return spec;
    }

    request_spec request_gen::gen_fault(std::mt19937_64 & rng)
    {
        request_spec spec;
        spec.k = request_spec::FAULT;
        spec.is_fault = true;
        spec.force_new_conn = true;
        spec.close_after = true;
        spec.half_close = true;
        spec.check_status = false;

        std::ostringstream os;
        int pick = static_cast<int>(rng() % 6);
        switch (pick)
        {
        case 0: // invalid (non-hex) chunk size
            spec.description = "fault: bad chunk size";
            os << "POST /echo/echo HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "Connection: close\r\n"
               << "Transfer-Encoding: chunked\r\n\r\n"
               << "zzzz\r\nhello\r\n0\r\n\r\n";
            break;
        case 1: // Content-Length beyond MAX_CONTENT_LENGTH (rejected at parse)
            spec.description = "fault: oversized content-length";
            os << "POST /echo/echo HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "Connection: close\r\n"
               << "Content-Length: 1073741824\r\n\r\n";
            break;
        case 2: // oversized single header line
        {
            spec.description = "fault: oversized header";
            std::string big(9000, 'A');
            os << "GET /echo/echo HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "X-Big: " << big << "\r\n"
               << "Connection: close\r\n\r\n";
            break;
        }
        case 3: // illegal request method
            spec.description = "fault: illegal method";
            os << "FROB /echo/echo HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "Connection: close\r\n\r\n";
            break;
        case 4: // multipart body that never sends a closing boundary
        {
            spec.description = "fault: unterminated multipart";
            std::string b = "----basherFaultBoundary";
            std::string body = "--" + b + "\r\n"
                "Content-Disposition: form-data; name=\"f\"\r\n\r\n"
                "partial-data-without-closing-boundary";
            os << "POST /echo/form HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "Connection: close\r\n"
               << "Content-Type: multipart/form-data; boundary=" << b
               << "\r\n"
               << "Content-Length: " << body.size() << "\r\n\r\n"
               << body;
            break;
        }
        default: // truncated chunked body: declare a chunk larger than sent, then close
            spec.description = "fault: truncated chunk";
            os << "POST /echo/echo HTTP/1.1\r\n"
               << "Host: " << m_cfg.host << "\r\n"
               << "Connection: close\r\n"
               << "Transfer-Encoding: chunked\r\n\r\n"
               << "10\r\nshort";
            break;
        }
        spec.raw_request = os.str();
        return spec;
    }

    request_spec request_gen::next(std::mt19937_64 & rng, bool keep_alive)
    {
        if (m_cfg.fault_rate > 0.0)
        {
            double roll = static_cast<double>(rng() % 1000000) / 1000000.0;
            if (roll < m_cfg.fault_rate)
            {
                return gen_fault(rng);
            }
        }
        return gen_normal(rng, keep_alive);
    }

    static bool extract_uint(const std::string & json, const std::string & key,
                             uint64_t & out)
    {
        std::string needle = "\"" + key + "\":";
        size_t p = json.find(needle);
        if (p == std::string::npos)
        {
            return false;
        }
        p += needle.size();
        out = std::strtoull(json.c_str() + p, nullptr, 10);
        return true;
    }

    bool verify_response(const request_spec & spec,
                         const http_client::response & r)
    {
        if (spec.is_fault)
        {
            return true;
        }

        if (spec.check_status && r.status_code != spec.expected_status)
        {
            return false;
        }

        if (spec.check_body && r.body != spec.expected_body)
        {
            return false;
        }

        if (spec.check_checksum)
        {
            uint64_t len = 0;
            uint64_t sum = 0;
            if (!extract_uint(r.body, "length", len) ||
                !extract_uint(r.body, "checksum", sum))
            {
                return false;
            }
            if (len != spec.expected_length || sum != spec.expected_checksum)
            {
                return false;
            }
        }

        if (spec.check_multipart)
        {
            uint64_t count = 0;
            uint64_t len = 0;
            uint64_t sum = 0;
            if (!extract_uint(r.body, "count", count) ||
                !extract_uint(r.body, "length", len) ||
                !extract_uint(r.body, "checksum", sum))
            {
                return false;
            }
            if (count != spec.expected_count ||
                len != spec.expected_length ||
                sum != spec.expected_checksum)
            {
                return false;
            }
        }

        return true;
    }
}
