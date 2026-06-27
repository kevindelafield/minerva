#include <cstdint>
#include <cstdlib>
#include <string>
#include <istream>
#include <iterator>

#include <util/string_utils.h>
#include <util/log.h>
#include <httpd/http_request.h>
#include <httpd/http_response.h>

#include "echo_controller.h"
#include "test_payload.h"

namespace minerva
{

    static constexpr int BODY_TIMEOUT_MS = 30000;
    static constexpr size_t STREAM_CHUNK = 8 * 1024;

    // Incremental FNV-1a so streamed and buffered reads produce the same hash.
    static inline void fnv_update(uint64_t & h, const char * data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            h ^= static_cast<unsigned char>(data[i]);
            h *= 1099511628211ULL;
        }
    }

    // Fold a string followed by a NUL separator so field boundaries matter.
    static inline void fnv_update_field(uint64_t & h, const std::string & s)
    {
        fnv_update(h, s.data(), s.size());
        char z = 0;
        fnv_update(h, &z, 1);
    }

    echo_controller::echo_controller()
    {
        // No authentication for the test service.
        require_authorization(false);

        REGISTER_HANDLER("echo", echo_controller::handle_echo);
        REGISTER_HANDLER("checksum", echo_controller::handle_checksum);
        REGISTER_HANDLER("sink", echo_controller::handle_sink);
        REGISTER_HANDLER("stream", echo_controller::handle_stream);
        REGISTER_HANDLER("form", echo_controller::handle_form);
        REGISTER_HANDLER("formgen", echo_controller::handle_formgen);
    }

    void echo_controller::handle_echo(http_context & ctx)
    {
        // Read the full body. The server decodes chunked vs content-length
        // transparently, so we just consume whatever is delivered.
        std::istream & is = ctx.request().read_fully(BODY_TIMEOUT_MS);
        std::string body((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>());

        // Decide response framing. Default mirrors the request framing.
        std::string mode = ctx.request().query_parameter("mode");
        bool chunked;
        if (ci_equals(mode, "chunked"))
        {
            chunked = true;
        }
        else if (ci_equals(mode, "cl"))
        {
            chunked = false;
        }
        else
        {
            chunked = ctx.request().chunked();
        }

        ctx.response().status_code_success();
        ctx.response().content_type_octet_stream();

        if (chunked)
        {
            // Emit the body as one or more chunks. The server sends the final
            // zero length chunk after the handler returns.
            size_t off = 0;
            do
            {
                size_t n = std::min(STREAM_CHUNK, body.size() - off);
                ctx.response().response_stream().write(body.data() + off, n);
                ctx.response().flush();
                off += n;
            } while (off < body.size());
        }
        else
        {
            ctx.response().response_stream().write(body.data(), body.size());
        }
    }

    void echo_controller::handle_checksum(http_context & ctx)
    {
        // Read the body in byte-array passes to exercise the streaming read path.
        char buf[64 * 1024];
        uint64_t h = 1469598103934665603ULL;
        uint64_t total = 0;
        size_t n;
        while ((n = ctx.request().read(buf, sizeof(buf), BODY_TIMEOUT_MS)) > 0)
        {
            for (size_t i = 0; i < n; ++i)
            {
                h ^= static_cast<unsigned char>(buf[i]);
                h *= 1099511628211ULL;
            }
            total += n;
        }

        ctx.response().status_code_success();
        ctx.response().content_type_json();
        ctx.response().response_stream()
            << "{\"length\":" << total << ",\"checksum\":" << h << "}";
    }

    void echo_controller::handle_sink(http_context & ctx)
    {
        // The body is consumed by the server after the handler returns (httpd
        // performs a null_body_read), so we simply report no content here.
        ctx.response().status_code_no_content();
    }

    void echo_controller::handle_stream(http_context & ctx)
    {
        // Any request body is consumed by the server after the handler returns.
        size_t size = 1024;
        uint32_t seed = 0;
        std::string size_s = ctx.request().query_parameter("size");
        if (!size_s.empty())
        {
            size = static_cast<size_t>(std::strtoull(size_s.c_str(), nullptr, 10));
        }
        std::string seed_s = ctx.request().query_parameter("seed");
        if (!seed_s.empty())
        {
            seed = static_cast<uint32_t>(std::strtoul(seed_s.c_str(), nullptr, 10));
        }

        std::string mode = ctx.request().query_parameter("mode");
        bool chunked = ci_equals(mode, "chunked");

        ctx.response().status_code_success();
        ctx.response().content_type_octet_stream();

        std::string body = test_payload::generate(seed, size);

        if (chunked)
        {
            size_t off = 0;
            do
            {
                size_t n = std::min(STREAM_CHUNK, body.size() - off);
                ctx.response().response_stream().write(body.data() + off, n);
                ctx.response().flush();
                off += n;
            } while (off < body.size());
        }
        else
        {
            ctx.response().response_stream().write(body.data(), body.size());
        }
    }

    void echo_controller::handle_form(http_context & ctx)
    {
        http_request & req = ctx.request();
        if (!req.is_multipart_form())
        {
            ctx.response().status_code_bad_request();
            return;
        }

        // Selects how each part body is consumed.
        std::string mode = req.query_parameter("read");
        if (mode.empty())
        {
            mode = "full";
        }
        bool stream = ci_equals(mode, "stream");
        bool partial = ci_equals(mode, "partial");

        // Single combined FNV-1a folding each part's metadata and body so the
        // client can verify the whole traversal with one number.
        uint64_t h = 1469598103934665603ULL;
        uint64_t total = 0;
        uint64_t count = 0;

        http_request::form_part part;
        while (req.next_form(part, BODY_TIMEOUT_MS))
        {
            ++count;
            fnv_update_field(h, part.name);
            fnv_update_field(h, part.filename);
            fnv_update_field(h, part.content_type);

            if (stream || partial)
            {
                char buf[STREAM_CHUNK];
                size_t n;
                while ((n = req.read(buf, sizeof(buf), BODY_TIMEOUT_MS)) > 0)
                {
                    fnv_update(h, buf, n);
                    total += n;
                }
            }
            else
            {
                std::istream & is = req.read_fully(BODY_TIMEOUT_MS);
                std::string body((std::istreambuf_iterator<char>(is)),
                                 std::istreambuf_iterator<char>());
                fnv_update(h, body.data(), body.size());
                total += body.size();
            }
            char z = 0;
            fnv_update(h, &z, 1); // body terminator

            if (partial)
            {
                // Stop after the first part; the server drains the remaining
                // form data via null_body_read after the handler returns.
                break;
            }
        }

        ctx.response().status_code_success();
        ctx.response().content_type_json();
        ctx.response().response_stream()
            << "{\"count\":" << count << ",\"length\":" << total
            << ",\"checksum\":" << h << "}";
    }

    void echo_controller::handle_formgen(http_context & ctx)
    {
        // Any request body is consumed by the server after the handler returns.
        // Generate a deterministic multipart/form-data response. The part set
        // is a pure function of (parts, seed, base size) so a client can
        // reconstruct and verify the exact bytes given the random boundary.
        uint32_t parts = 3;
        uint32_t seed = 0;
        size_t base = 1000;

        std::string parts_s = ctx.request().query_parameter("parts");
        if (!parts_s.empty())
        {
            parts = static_cast<uint32_t>(std::strtoul(parts_s.c_str(), nullptr, 10));
        }
        std::string seed_s = ctx.request().query_parameter("seed");
        if (!seed_s.empty())
        {
            seed = static_cast<uint32_t>(std::strtoul(seed_s.c_str(), nullptr, 10));
        }
        std::string size_s = ctx.request().query_parameter("size");
        if (!size_s.empty())
        {
            base = static_cast<size_t>(std::strtoull(size_s.c_str(), nullptr, 10));
        }

        std::string mode = ctx.request().query_parameter("mode");
        bool chunked = ci_equals(mode, "chunked");

        ctx.response().status_code_success();
        ctx.response().begin_multipart();

        for (uint32_t k = 0; k < parts; ++k)
        {
            bool is_file = (k % 2 == 1);
            bool has_ct = is_file || (k % 3 == 0);
            std::string name = "field" + std::to_string(k);
            std::string filename = is_file ? ("file" + std::to_string(k) + ".bin")
                                           : std::string();
            std::string content_type = has_ct ? std::string("application/octet-stream")
                                              : std::string();
            size_t size = base + static_cast<size_t>(k) * 4096;
            std::string body = test_payload::generate(seed + k, size);

            ctx.response().begin_part(name, filename, content_type);

            if (chunked)
            {
                size_t off = 0;
                do
                {
                    size_t n = std::min(STREAM_CHUNK, body.size() - off);
                    ctx.response().response_stream().write(body.data() + off, n);
                    ctx.response().flush();
                    off += n;
                } while (off < body.size());
            }
            else
            {
                ctx.response().response_stream().write(body.data(), body.size());
            }
        }

        ctx.response().end_multipart();
    }
}
