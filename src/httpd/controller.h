#pragma once

#include <string>
#include <map>
#include <functional>

#include <util/string_utils.h>
#include "http_request.h"
#include "http_response.h"

namespace minerva
{

    class controller
    {
    public:
        virtual void handle_request(http_context & ctx,
                                    const std::string & operation);
    
        static bool next_path_segment(std::istream & is,
                                      std::string & next);

        bool require_authorization() const
        {
            return m_require_authorization;
        }

        void require_authorization(bool require)
        {
            m_require_authorization = require;
        }

        virtual bool auth_callback(const std::string & user, const std::string & op)
        {
            return true;
        }

    protected:

        void register_handler(const std::string & name,
                              std::function<void(http_context & ctx)> func);

        bool save_to_file(const std::string & filename,
                          http_context & ctx);

        bool send_file(const std::string & filename,
                       http_content_type::code content_type,
                       http_context & ctx);

        bool send_file(const std::string & filename,
                       http_context & ctx);

        bool send_file_text(const std::string & filename,
                            http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_TEXT_PLAIN,
                             ctx);
        }

        bool send_file_html(const std::string & filename,
                            http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_TEXT_HTML,
                             ctx);
        }

        bool send_file_xml(const std::string & filename,
                           http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_APPLICATION_XML,
                             ctx);
        }

        bool send_file_css(const std::string & filename,
                           http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_TEXT_CSS,
                             ctx);
        }

        bool send_file_javascript(const std::string & filename,
                                  http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_TEXT_JAVASCRIPT,
                             ctx);
        }

        bool send_file_jpeg(const std::string & filename,
                            http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_IMAGE_JPEG,
                             ctx);
        }

        bool send_file_png(const std::string & filename,
                           http_context & ctx)
        {
            return send_file(filename,
                             http_content_type::code::CONTENT_TYPE_IMAGE_PNG,
                             ctx);
        }

    private:

        bool m_require_authorization = true;
        std::map<std::string, std::function<void(http_context & ctx)>, minerva::ci_less> m_handlers;
    };

#define REGISTER_HANDLER(name, func)                        \
    {                                                       \
        register_handler(name,                              \
                         std::bind(&func,                   \
                                   this,                    \
                                   std::placeholders::_1)); \
    }
}
