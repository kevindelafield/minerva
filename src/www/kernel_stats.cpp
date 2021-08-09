#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jsoncpp/json/json.h>
#include <owl/component_visor.h>
#include <owl/httpd.h>
#include <owl/http_request.h>
#include <owl/http_response.h>
#include "kernel_stats.h"

namespace www
{

    void kernel_stats::initialize()
    {
        auto svr = get_component<owl::httpd>(owl::httpd::NAME);
        if (svr)
        {
            svr->register_controller(PATH, this);
        }
    }

    bool kernel_stats::auth_callback(const std::string & user,
                                     const std::string & op) 
    {
        Json::Value root;
        Json::Value viewer;

        if (user == "root")
        {
            return true;
         }

        return false;
    }

    void kernel_stats::handle_request(owl::http_context & ctx, const std::string & op)
    {
        Json::Value stats = visor->get_stats();

        stats["connections"] = owl::connection::get_stats();

        ctx.response().response_stream() << stats;
        ctx.response().content_type(owl::http_content_type::code::CONTENT_TYPE_APPLICATION_JSON);
        ctx.response().status_code(owl::http_response::http_response_code::HTTP_RETCODE_SUCCESS);
    }
}
