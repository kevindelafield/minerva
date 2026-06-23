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
        controller() = default;
        virtual ~controller() = default;

        // Not copyable or movable: m_handlers contains lambdas bound with
        // `this`, so a copy would carry handlers pointing at the original
        // object.
        controller(const controller &)             = delete;
        controller & operator=(const controller &) = delete;
        controller(controller &&)                  = delete;
        controller & operator=(controller &&)      = delete;

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

        /**
         * Authorization hook. Returns true if `user` is permitted to invoke
         * `op` on this controller.
         *
         * SECURITY NOTE: the default implementation returns true for every
         * caller. A subclass that calls require_authorization(true) and
         * forgets to override this method will accept any authenticated
         * user for any operation. Either:
         *   - keep require_authorization(false) (the public-controller
         *     case), or
         *   - override auth_callback to enforce the actual policy.
         */
        virtual bool auth_callback(const std::string & user, const std::string & op)
        {
            (void)user;
            (void)op;
            return true;
        }

        // Maximum file size that send_file() will return. Files larger than
        // this are rejected with 500. Default 100 MiB. Set at startup; not
        // synchronized for concurrent updates.
        static size_t max_send_file_size()              { return s_max_send_file_size; }
        static void   max_send_file_size(size_t bytes)  { s_max_send_file_size = bytes; }

    protected:

        /**
         * Register `func` as the handler for path operation `name`.
         *
         * THREAD SAFETY: register_handler() and handle_request() share
         * m_handlers without synchronization. Register all handlers
         * during construction / before httpd::start(); registering after
         * the server has started serving requests is a data race.
         *
         * If `name` is already registered, the new handler replaces the
         * old one and a warning is logged.
         */
        void register_handler(const std::string & name,
                              std::function<void(http_context & ctx)> func);

        // Templated overload: register a member function directly, no
        // std::bind required. Equivalent to:
        //     register_handler(name,
        //                      [this](http_context& c){ this->method(c); });
        template <typename Derived>
        void register_handler(const std::string & name,
                              void (Derived::*method)(http_context &))
        {
            auto * self = static_cast<Derived *>(this);
            register_handler(name,
                             [self, method](http_context & ctx)
                             {
                                 (self->*method)(ctx);
                             });
        }

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

        static size_t s_max_send_file_size;
    };

// Legacy registration macro. Prefer the templated register_handler overload
// above (no std::bind, fewer macros). Retained for source compatibility.
#define REGISTER_HANDLER(name, func)                        \
    {                                                       \
        register_handler(name,                              \
                         std::bind(&func,                   \
                                   this,                    \
                                   std::placeholders::_1)); \
    }
}
