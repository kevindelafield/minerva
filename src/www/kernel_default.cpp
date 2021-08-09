#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jsoncpp/json/json.h>
#include <owl/component_visor.h>
#include <owl/httpd.h>
#include <owl/http_request.h>
#include <owl/http_response.h>
#include <owl/string_utils.h>
#include <owl/file_utils.h>
#include <owl/log.h>
#include "kernel_default.h"

namespace www
{

    void kernel_default::initialize()
    {
        auto svr = get_component<owl::httpd>(owl::httpd::NAME);
        if (svr)
        {
            svr->register_default_controller(this);
        }

        m_root_dir = visor->config("www_root_dir");
        m_default_file = visor->config("www_default_file");

        if (!owl::file_is_directory(m_root_dir))
        {
            FATAL("www root directory does not exist: " << m_root_dir);
        }

        std::string def_file = m_root_dir + "/" + m_default_file;
        if (!owl::file_is_file(def_file))
        {
            FATAL("www default file does not exist: " << def_file);
        }

        require_authorization(false);
    }

    bool kernel_default::auth_callback(const std::string & user,
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

    void kernel_default::handle_request(owl::http_context & ctx, const std::string & op)
    {
        ctx.response().add_header("Pragma", "no-cache");
        ctx.response().add_header("Cache-Control", "no-cache");

        std::string filename = ctx.request().path();

        // don't allow up directory
        std::stringstream ss(filename);
        std::string segment;
        while (next_path_segment(ss, segment))
        {
            if (segment == "..")
            {
                ctx.response().status_code_forbidden();
                return;
            }
        }

        if (filename == "/")
        {
            filename = m_root_dir + "/" + m_default_file;
        }
        else
        {
            filename = m_root_dir + filename;
        }

        if (!owl::file_is_file(filename))
        {
            LOG_DEBUG("didn't find file: " << filename);
            ctx.response().status_code_not_found();
            return;
        }

        LOG_DEBUG("found file: " << filename);

        send_file(filename, ctx);

        ctx.response().status_code_success();
    }
}