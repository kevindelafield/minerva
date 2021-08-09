#include <string>
#include <istream>
#include <fstream>
#include "controller.h"
#include "http_context.h"
#include "string_utils.h"

namespace owl
{

    void controller::handle_request(http_context & ctx,
                                    const std::string & operation)
    {
        auto it = m_handlers.find(operation);
        if (it == m_handlers.end())
        {
            LOG_DEBUG("didn't find handler: " << operation);
            ctx.response().status_code(http_response::http_response_code::HTTP_RETCODE_NOT_FOUND);
            return;
        }
        it->second(ctx);
    }


    void controller::register_handler(const std::string & name,
                                      std::function<void(http_context &)> func)
    {
        m_handlers[name] = func;
    }

    bool controller::next_path_segment(std::istream & is,
                                       std::string & next)
    {
        if (is.peek() == EOF)
        {
            next = "";
            return false;
        }
        while (is.peek() == static_cast<int>('/'))
        {
            is.get();
        }
        if (is.peek() == EOF)
        {
            next = "";
            return false;
        }
        std::getline(is, next, '/');
        return true;
    }

    bool controller::save_to_file(const std::string & filename,
                                  http_context & ctx)
    {
        std::ofstream os(filename, std::ios::out | std::ios::binary);

        if (!os)
        {
            LOG_ERROR("Failed to open file for write: " << filename);
            // internal error - send 500
            ctx.response().status_code_internal_error();
            return false;
        }

        size_t read = 0;

        do
        {
            char buf[128*1024];

            read = ctx.request().read(buf, sizeof(buf));
            os.write(buf, read);
            if (os.fail())
            {
                LOG_ERROR("Failed to write to file: " << filename);
                // internal error - send 500
                ctx.response().status_code_internal_error();
                return false;
            }
        }
        while (read > 0);

        ctx.response().status_code_no_content();

        return true;
    }


    bool controller::send_file(const std::string & filename,
                               http_context & ctx)
    {
        http_content_type::code ct = http_content_type::code::CONTENT_TYPE_UNKNOWN;

        size_t index = filename.find_last_of(".");
        if (index != std::string::npos)
        {
            std::string ext = filename.substr(index+1);
            if (ci_equals(ext, "jpg") || ci_equals(ext, "jpeg"))
            {
                ct = http_content_type::code::CONTENT_TYPE_IMAGE_JPEG;
            }
            else if (ci_equals(ext, "html") || ci_equals(ext, "html"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_HTML;
            }
            else if (ci_equals(ext, "txt") || ci_equals(ext, "log"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_PLAIN;
            }
            else if (ci_equals(ext, "xml"))
            {
                ct = http_content_type::code::CONTENT_TYPE_APPLICATION_XML;
            }
            else if (ci_equals(ext, "json"))
            {
                ct = http_content_type::code::CONTENT_TYPE_APPLICATION_JSON;
            }
            else if (ci_equals(ext, "js"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_JAVASCRIPT;
            }
            else if (ci_equals(ext, "png"))
            {
                ct = http_content_type::code::CONTENT_TYPE_IMAGE_PNG;
            }
            else if (ci_equals(ext, "css"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_CSS;
            }
        }
        return send_file(filename, ct, ctx);
    }


    bool controller::send_file(const std::string & filename,
                               http_content_type::code content_type,
                               http_context & ctx)
    {
        std::ifstream is(filename, std::ios::in | std::ios::binary);
        if (!is)
        {
            ctx.response().status_code_not_found();
            LOG_ERROR("Failed to open file for read: " << filename);
            return false;
        }

        ctx.response().content_type(content_type);
    
        is.seekg(0, is.end);
        size_t len = is.tellg();
        is.seekg(0, is.beg);


        while (len > 0)
        {
            char buf[10*1024];
            size_t to_read = std::min(sizeof(buf), len);
            is.read(buf, to_read);
            if (is.fail())
            {
                ctx.response().status_code_internal_error();
                // internal error - send back default 500 status
                LOG_ERROR("Failed to read from file: " << filename);
                return false;
            }
            ctx.response().response_stream().write(buf, to_read);
            len -= to_read;
        }

        ctx.response().status_code_success();

        return true;
    }
}
