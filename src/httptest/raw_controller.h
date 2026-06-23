#pragma once

#include <httpd/http_context.h>
#include <httpd/controller.h>

namespace minerva
{

    // Default controller used for any path that is not handled by a registered
    // controller.  It demonstrates "raw" request handling: it overrides
    // handle_request directly (rather than registering named operations) and
    // pulls the entire request body at once, echoing it back to the client.
    //
    // It also supports a /raw/bytes style path that reads the body using
    // fixed-size byte-array reads to exercise the partial-read boundaries.
    class raw_controller : public controller
    {
    public:
        raw_controller();
        virtual ~raw_controller() = default;

        void handle_request(http_context & ctx,
                            const std::string & operation) override;
    };
}
