#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <tuple>
#include <util/string_utils.h>
#include "http_content_type.h"

namespace minerva
{

    class http_context;

    class http_response
    {
    public:
        // Canonical HTTP status codes used by this server.
        // Note: application-specific aliases that previously collided in
        // this enum (HTTP_RETCODE_OV_*, HTTP_RETCODE_MALFORMED_*, etc.)
        // have been removed. They were never referenced and prevented
        // adding `case` labels in switch statements over this enum.
        enum http_response_code
        {
            // 2xx
            HTTP_RETCODE_SUCCESS           = 200, // Success
            HTTP_RETCODE_CREATED           = 201, // Resource was created
            HTTP_RETCODE_NO_CONTENT        = 204, // Success. No content returned

            // 3xx
            HTTP_RETCODE_NOT_MODIFIED      = 304, // Not modified

            // 4xx
            HTTP_RETCODE_BAD_REQUEST       = 400, // Bad request
            HTTP_RETCODE_UNAUTHORIZED      = 401, // Unauthorized
            HTTP_RETCODE_FORBIDDEN         = 403, // Forbidden
            HTTP_RETCODE_NOT_FOUND         = 404, // Not Found
            HTTP_RETCODE_METHOD_DENIED     = 405, // Method Not Allowed
            HTTP_RETCODE_CONFLICT          = 409, // Conflict
            HTTP_RETCODE_GONE              = 410, // Gone
            HTTP_RETCODE_LENGTH_REQ        = 411, // Length Required
            HTTP_RETCODE_REQ_TOO_LARGE     = 413, // Request Too Large
            HTTP_RETCODE_URI_TOO_LONG      = 414, // Request URI Too Long

            // 5xx
            HTTP_RETCODE_INT_SERVER_ERR    = 500, // Internal Server Error
            HTTP_RETCODE_NOT_IMPLEMENTED   = 501, // Not Implemented
            HTTP_RETCODE_UNAVAILABLE       = 503, // Service Unavailable
            HTTP_RETCODE_VER_NOT_SUPPORTED = 505  // HTTP Version Not Supported
        };

        http_response(http_context & ctx)
            : m_status_code(http_response_code::HTTP_RETCODE_INT_SERVER_ERR),
              m_content_type(http_content_type::code::CONTENT_TYPE_UNKNOWN),
              m_http11(true), m_ctx(ctx)
        {
        }

        ~http_response() = default;

        http_response(const http_response &)             = delete;
        http_response & operator=(const http_response &) = delete;
        http_response(http_response &&)                  = delete;
        http_response & operator=(http_response &&)      = delete;

        static std::string get_status_code_string(http_response_code code);

        http_response_code status_code() const
        {
            return m_status_code;
        }

        void status_code(http_response_code code)
        {
            m_status_code = code;
        }

        void status_code_success()
        {
            m_status_code = http_response_code::HTTP_RETCODE_SUCCESS;
        }

        void status_code_no_content()
        {
            m_status_code = http_response_code::HTTP_RETCODE_NO_CONTENT;
        }

        void status_code_bad_request()
        {
            m_status_code = http_response_code::HTTP_RETCODE_BAD_REQUEST;
        }

        void status_code_not_found()
        {
            m_status_code = http_response_code::HTTP_RETCODE_NOT_FOUND;
        }

        void status_code_forbidden()
        {
            m_status_code = http_response_code::HTTP_RETCODE_FORBIDDEN;
        }

        void status_code_internal_error()
        {
            m_status_code = http_response_code::HTTP_RETCODE_INT_SERVER_ERR;
        }

        const std::string & status_message() const
        {
            return m_status_message;
        }

        void status_message(const char* msg)
        {
            m_status_message = msg;
        }

        http_content_type::code content_type() const
        {
            return m_content_type;
        }

        void content_type(http_content_type::code contentType)
        {
            m_content_type = contentType;
        }

        void content_type_rtsp_tunnelled()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_RTSP_TUNNELLED;
        }

        void content_type_octet_stream()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_OCTET_STREAM;
        }

        void content_type_csv()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_CSV;
        }

        void content_type_json()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_APPLICATION_JSON;
        }

        void content_type_xml()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_APPLICATION_XML;
        }

        void content_type_text()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_TEXT_PLAIN;
        }

        void content_type_jpeg()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_IMAGE_JPEG;
        }

        void content_type_png()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_IMAGE_PNG;
        }

        void content_type_tar()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_APPLICATION_X_TAR;
        }

        void content_type_zip()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_APPLICATION_ZIP;
        }

        void content_type_multipart_form()
        {
            m_content_type = http_content_type::code::CONTENT_TYPE_MULTIPART_FORM;
        }

        bool is_http11() const
        {
            return m_http11;
        }

        void is_http11(const bool value)
        {
            m_http11 = value;
        }

        std::iostream & response_stream()
        {
            return m_response_stream;
        }

        void add_header(const std::string & key, const std::string & value)
        {
            m_headers.push_back(std::make_tuple(key, value));
        }

        const std::vector<std::tuple<std::string, std::string>> & headers() const
        {
            return m_headers;
        }

        void no_size(bool no)
        {
            m_nosize = no;
        }

        bool no_size() const
        {
            return m_nosize;
        }

        void should_write_header(bool write)
        {
            m_should_write_header = write;
        }

        bool should_write_header() const
        {
            return m_should_write_header;
        }

        bool chunked() const
        {
            return m_chunked;
        }

        bool header_written() const
        {
            return m_header_written;
        }

        bool send_buffer(std::istream & is);

        bool write_header();

        void flush();

        bool flush_final_chunk();

        const std::string & multipart_boundary() const
        {
            return m_multipart_boundary;
        }

        void multipart_boundary(const std::string & boundary)
        {
            m_multipart_boundary = boundary;
        }

        // ---- multipart/form-data response generation ----
        //
        // Services emit a multipart/form-data response by calling
        // begin_multipart() once, then begin_part()/write_part() for each
        // part, and end_multipart() to close. Part bodies may be streamed
        // directly through response_stream() (with flush() for chunked
        // framing) after begin_part(); the trailing CRLF that separates a
        // part body from the next boundary is emitted lazily by the next
        // begin_part()/end_multipart() call. Content-Length framing buffers
        // the whole response; chunked framing is selected by calling flush()
        // between writes exactly as with any other streamed response.

        // Initialise a multipart response: set the Content-Type to
        // multipart/form-data and generate a random boundary (unless one was
        // already set via multipart_boundary()). Returns the boundary.
        const std::string & begin_multipart();

        // Open a new part: emits the boundary delimiter and the part headers
        // (Content-Disposition with the given name and optional filename, plus
        // an optional Content-Type) followed by the blank separator line. The
        // caller then writes the part body via response_stream(). Throws
        // http_exception if name/filename/content_type contain CR, LF or NUL.
        void begin_part(const std::string & name,
                        const std::string & filename = std::string(),
                        const std::string & content_type = std::string());

        // Convenience: open a part and write its complete body in one call.
        void write_part(const std::string & name,
                        const std::string & filename,
                        const std::string & content_type,
                        const char * data, size_t len);

        void write_part(const std::string & name,
                        const std::string & filename,
                        const std::string & content_type,
                        const std::string & data)
        {
            write_part(name, filename, content_type, data.data(), data.size());
        }

        // Close the current part (if any) and write the closing boundary
        // delimiter.
        void end_multipart();

    private:

        constexpr static const char * CRLF = "\r\n";

        http_response_code                                m_status_code;
        http_content_type::code                           m_content_type;
        std::stringstream                                 m_response_stream;
        std::string                                       m_status_message;
        bool                                              m_http11;
        http_context &                                    m_ctx;
        std::vector<std::tuple<std::string, std::string>> m_headers;
        bool                                              m_chunked            = false;
        bool                                              m_nosize             = false;
        bool                                              m_should_write_header = true;
        bool                                              m_header_written     = false;
        std::string                                       m_multipart_boundary;
        bool                                              m_part_open          = false;
    };
}
