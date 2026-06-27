#pragma once

#include <httpd/http_context.h>
#include <httpd/controller.h>

namespace minerva
{

    // Controller registered under the "/echo" path.  It exposes a set of
    // operations that exercise the different request/response body paths in
    // http_request / http_response:
    //
    //   /echo/echo      - read the full request body and echo it back. The
    //                     response framing (chunked vs content-length) is
    //                     selected by the ?mode=chunked|cl query parameter and
    //                     defaults to mirroring the request framing.
    //   /echo/checksum  - read the body as a stream of byte arrays and return a
    //                     JSON object with the length and FNV-1a checksum.
    //   /echo/sink      - consume the body without buffering and return 204.
    //   /echo/stream    - generate a deterministic body of ?size= bytes seeded
    //                     by ?seed= and return it, chunked or content-length
    //                     based on ?mode=.
    //   /echo/form      - parse a multipart/form-data request. The ?read=
    //                     query parameter selects how each part body is
    //                     consumed: full (read_fully), stream (read loop) or
    //                     partial (read only the first part, leaving the rest
    //                     for the server's null-body drain). Returns a JSON
    //                     summary with the part count, total body length and a
    //                     combined FNV-1a checksum folding part metadata and
    //                     bodies.
    //   /echo/formgen   - generate a multipart/form-data response. The
    //                     ?parts=, ?seed= and ?size= query parameters select a
    //                     deterministic set of parts (alternating file/field
    //                     parts with deterministic bodies). ?mode=chunked|cl
    //                     selects the response framing.
    class echo_controller : public controller
    {
    public:
        echo_controller();
        virtual ~echo_controller() = default;

    private:
        void handle_echo(http_context & ctx);
        void handle_checksum(http_context & ctx);
        void handle_sink(http_context & ctx);
        void handle_stream(http_context & ctx);
        void handle_form(http_context & ctx);
        void handle_formgen(http_context & ctx);
    };
}
