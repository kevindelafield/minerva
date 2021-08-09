#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jsoncpp/json/json.h>
#include <ovhttpd/component_visor.h>
#include <ovhttpd/httpd.h>
#include <ovhttpd/http_request.h>
#include <ovhttpd/http_response.h>
#include "kernel_stats.h"

namespace epoll
{

    void kernel_stats::initialize()
    {
        auto svr = get_component<ovhttpd::httpd>(ovhttpd::httpd::NAME);
        if (svr)
        {
            svr->register_controller(PATH, this);
        }
    }

    void kernel_stats::handle_request(ovhttpd::http_context & ctx, const std::string & op)
    {
        Json::Value stats = visor->get_stats();

        stats["connections"] = ovhttpd::connection::get_stats();

        ctx.response().response_stream() << stats;
        ctx.response().content_type(ovhttpd::http_content_type::code::CONTENT_TYPE_APPLICATION_JSON);
        ctx.response().status_code(ovhttpd::http_response::http_response_code::HTTP_RETCODE_SUCCESS);
    }
}
