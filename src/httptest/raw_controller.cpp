#include <string>
#include <istream>
#include <iterator>

#include <util/string_utils.h>
#include <util/log.h>
#include <httpd/http_request.h>
#include <httpd/http_response.h>

#include "raw_controller.h"

namespace minerva
{

    static constexpr int RAW_BODY_TIMEOUT_MS = 30000;

    raw_controller::raw_controller()
    {
        require_authorization(false);
    }

    void raw_controller::handle_request(http_context & ctx,
                                        const std::string & operation)
    {
        if (ci_equals(operation, "bytes"))
        {
            // Read the body using fixed-size byte-array reads and echo it back.
            char buf[16 * 1024];
            size_t n;
            ctx.response().status_code_success();
            ctx.response().content_type_octet_stream();
            while ((n = ctx.request().read(buf, sizeof(buf), RAW_BODY_TIMEOUT_MS)) > 0)
            {
                ctx.response().response_stream().write(buf, n);
            }
            return;
        }

        // Default: pull the entire request body at once and echo it back.
        std::istream & is = ctx.request().read_fully(RAW_BODY_TIMEOUT_MS);
        std::string body((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>());

        ctx.response().status_code_success();
        ctx.response().content_type_octet_stream();
        ctx.response().response_stream().write(body.data(), body.size());
    }
}
