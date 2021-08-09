#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jsoncpp/json/json.h>
#include <ovhttpd/component_visor.h>
#include <ovhttpd/httpd.h>
#include <ovhttpd/http_request.h>
#include <ovhttpd/http_response.h>
#include <ovhttpd/json_utils.h>
#include "kernel_test.h"

namespace epoll
{
    void kernel_test::initialize()
    {
        auto svr = get_component<ovhttpd::httpd>(ovhttpd::httpd::NAME);
        if (svr)
        {
            svr->register_controller(PATH, this);
            REGISTER_HANDLER("upload",
                             kernel_test::upload);
            REGISTER_HANDLER("parse",
                             kernel_test::parse);
            REGISTER_HANDLER("sendfile",
                             kernel_test::sendfile);
        }
    }

    void kernel_test::sendfile(ovhttpd::http_context & ctx)
    {
        auto filename = ctx.request().query_parameter("filename");

        send_file(filename, ctx);
    }

    void kernel_test::upload(ovhttpd::http_context & ctx)
    {
        save_to_file("foo.tar.zip", ctx);
    }

    void kernel_test::parse(ovhttpd::http_context & ctx)
    {
        Json::Value test;

        if (!ovhttpd::parse_json(ctx.request().read_fully(), test))
        {
            LOG_WARN("Failed to parse JSON");
            ctx.response().status_code_bad_request();
            return;
        }

        ctx.response().response_stream() << test;

        ctx.response().content_type_json();
        ctx.response().status_code_success();
    }
}
