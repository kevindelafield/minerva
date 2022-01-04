#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <tuple>
#include <util/string_utils.h>
#include "http_content_type.h"

namespace httpd
{

    class http_context;

    class http_response
    {
    public:
        enum http_response_code
        {
            // Standard 200 codes
            HTTP_RETCODE_SUCCESS           = 200, // Success
            HTTP_RETCODE_PASS_UNITTEST     = 200, // Passed
            HTTP_RETCODE_CREATED           = 201, // Resource was created
            HTTP_RETCODE_NO_CONTENT        = 204, //Success. No content returned
            HTTP_RETCODE_RESET_DEVICE      = 204, // Success.Device reset required
            
            // Standard 300 codes
            HTTP_RETCODE_NOT_MODIFIED      = 304, // Not modified
            
            // Standard 400 codes
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
            
            // Custom 400 codes
            HTTP_RETCODE_MALFORMED_RESTAPI = 400, // Malformed REST API
            HTTP_RETCODE_MALFORMED_COOKIE  = 400, // Malformed cookie header
            HTTP_RETCODE_MALFORMED_QUERY   = 400, // Malformed query
            HTTP_RETCODE_MALFORMED_XML     = 400, // Malformed XML
            HTTP_RETCODE_ACTION_NOT_ALLOWED = 400, // Action not allowed
            HTTP_RETCODE_UNEXPECTED_CONTENT = 400, // Unexpected content type
            HTTP_RETCODE_AUTHENTICATION_FAILED = 400, // Authentication failed
            
            // OV specific 400 codes
            HTTP_RETCODE_OV_FAIL           = 400, // OV API call failed
            HTTP_RETCODE_OV_INVALID_RULE_ID = 404, // Invalid rule ID
            HTTP_RETCODE_OV_WARMUP         = 400, // Warmup period not over
            HTTP_RETCODE_OV_BAD_PARAMETER  = 400, // Bad parameters
            HTTP_RETCODE_OV_PARAM_VALUE_OUTOFRANGE = 400, // Param value out of range
            HTTP_RETCODE_OV_LICENSE_NOT_VALID = 400, // License not valid
            HTTP_RETCODE_OV_BAD_CALIBRATION_DATA = 400, // Bad calibration data
            HTTP_RETCODE_OV_STORAGE_WRITE_FAILED = 400, // No room in storage
            HTTP_RETCODE_OV_NO_ROOM_IN_STORAGE = 400, // Rule too big
            HTTP_RETCODE_OV_RULE_TOO_BIG   = 400, // Rule not found in storage
            HTTP_RETCODE_OV_RULE_NOT_FOUND_IN_STORAGE = 400, // Rule not found in storage
            HTTP_RETCODE_OV_INVALID_MAX_RULES = 400, // Invalid max rules
            HTTP_RETCODE_OV_RULE_CUSTOM_RESPONSE_MAX_EXCEEDED = 400, // Max custom response exceeded
            HTTP_RETCODE_OV_RULE_PRESENT   = 400, // Rule already exists
            HTTP_RETCODE_OV_VIDEO_FRAME_RETRIEVE_FAIL = 400, // Unable to retrieve frame
            HTTP_RETCODE_OV_VIDEO_BUFFER_POOL_INSUFFICIENT = 400, // Buffer pool insufficient
            HTTP_RETCODE_OV_FEATURE_NOT_ALLOWED = 403, // Feature not allowed
            HTTP_RETCODE_OV_WARNING_INSUFFICIENT_BUFFER = 400, // Insufficient buffer
            HTTP_RETCODE_OV_RESTART_REQUIRED = 204, // Restart required
            HTTP_RETCODE_OV_RULE_NOT_ALLOWED = 400, // Rule not allowed
            HTTP_RETCODE_OV_SLIDER_DISABLED = 400, // Slider disabled
            HTTP_RETCODE_OV_SLIDER_UPDATE_FAILED = 400, // Slider update failed
            HTTP_RETCODE_OV_NO_CALIBRATION_DATA = 403, // Sensor not calibrated
            HTTP_RETCODE_OV_NO_REFERENCE_SNAPSHOTS = 403, // Reference snapshots not set
            HTTP_RETCODE_OV_STATUS_BAD_SIGNAL = 400, // Already in Bad signal
            HTTP_RETCODE_OV_STATUS_RULE_DISABLED = 403, // Rule is not set or active
            
            // Standard 500 codes
            HTTP_RETCODE_INT_SERVER_ERR    = 500, // Internal Server Error
            HTTP_RETCODE_NOT_IMPLEMENTED   = 501, // Not Implemented
            HTTP_RETCODE_UNAVAILABLE       = 503, // Service Unavailable
            HTTP_RETCODE_VER_NOT_SUPPORTED = 505, // HTTP Version Not Supported
            
            // Custom 500 codes
            HTTP_RETCODE_OVERFLOW          = 500, // Avoided buffer overflow
            HTTP_RETCODE_HEADERS_SENT      = 500, // Headers already sent
            HTTP_RETCODE_REASON_TOO_LARGE  = 500, // Reason too large
            HTTP_RETCODE_RETCODE_NOT_SET   = 500, // Response code not set
            HTTP_RETCODE_TOO_MANY_COOKIES  = 500, // Out of cookie storage
            HTTP_RETCODE_TOO_MANY_QUERIES  = 500, // Out of query storage
            HTTP_RETCODE_BAD_CONTENT_TYPE  = 500, // Unknown content type
            HTTP_RETCODE_REQ_HANDLER_FAIL  = 500, // Request handler failed
            HTTP_RETCODE_HEADER_VAL_OVER   = 500, // Header value buffer overflow
            HTTP_RETCODE_QUERY_NAME_OVER   = 500, // Cookie name buffer overflow
            HTTP_RETCODE_QUERY_VAL_OVER    = 500, // Cookie value buffer overflow
            HTTP_RETCODE_COOKIE_NAME_OVER  = 500, // Cookie name buffer overflow
            HTTP_RETCODE_COOKIE_VAL_OVER   = 500, // Cookie value buffer overflow
            HTTP_RETCODE_BAD_CACHE_TYPE    = 500 // Invalid cache control code
        };
        
        // Standard 200 codes
        constexpr static char HTTP_RETDESC_SUCCESS[] = 
            "Success";
        constexpr static char HTTP_RETDESC_PASS_UNITTEST[] = 
            "Passed a unit test";
        constexpr static char HTTP_RETDESC_CREATED[] = 
            "Resource created";
        constexpr static char HTTP_RETDESC_NO_CONTENT[] = 
            "No content";
        constexpr static char HTTP_RETDESC_RESET_DEVICE[] = 
            "Reset device required";

        // Standard 300 codes
        constexpr static char HTTP_RETDESC_NOT_MODIFIED[] = 
            "Not modified";

        // Standard 400 codes
        constexpr static char HTTP_RETDESC_BAD_REQUEST[] = 
            "Bad request";
        constexpr static char HTTP_RETDESC_UNAUTHORIZED[] = 
            "Unauthorized";
        constexpr static char HTTP_RETDESC_FORBIDDEN[] = 
            "Forbidden";
        constexpr static char HTTP_RETDESC_NOT_FOUND[] = 
            "Not Found";
        constexpr static char HTTP_RETDESC_METHOD_DENIED[] = 
            "Method Not Allowed";
        constexpr static char HTTP_RETDESC_GONE[] = 
            "Gone";
        constexpr static char HTTP_RETDESC_LENGTH_REQ[] = 
            "Length Required";
        constexpr static char HTTP_RETDESC_REQ_TOO_LARGE[] = 
            "Request Too Large";
        constexpr static char HTTP_RETDESC_URI_TOO_LONG[] = 
            "Request URI Too Long";
        constexpr static char HTTP_RETDESC_CONFLICT[] = 
            "Conflict";

        // Custom 400 codes
        constexpr static char HTTP_RETDESC_MALFORMED_COOKIE[] = 
            "Malformed cookie header";
        constexpr static char HTTP_RETDESC_MALFORMED_QUERY[] = 
            "Malformed query";
        constexpr static char HTTP_RETDESC_MALFORMED_RESTAPI[] = 
            "Invalid REST API";
        constexpr static char HTTP_RETDESC_MALFORMED_XML[] = 
            "Malformed XML";
        constexpr static char HTTP_RETDESC_ACTION_NOT_ALLOWED[] = 
            "Action not allowed";
        constexpr static char HTTP_RETDESC_UNEXPECTED_CONTENT[] = 
            "Unexpected content type";
        constexpr static char HTTP_RETDESC_UNEXPECTED_CONTENT_LEN[] = 
            "Unexpectedexpr static Contentength";
        constexpr static char HTTP_RETDESC_RESOURCE_LIMIT[] = 
            
            "Limit met for specified resource";
        constexpr static char HTTP_RETDESC_AUTHENTICATION_FAILED[] = 
            "Authentication failed";

        // Standard 500 codes
        constexpr static char HTTP_RETDESC_INT_SERVER_ERR[] = 
            "Internal Server Error";
        constexpr static char HTTP_RETDESC_NOT_IMPLEMENTED[] = 
            "Not Implemented";
        constexpr static char HTTP_RETDESC_UNAVAILABLE[] = 
            "Service Unavailable";
        constexpr static char HTTP_RETDESC_VER_NOT_SUPPORTED[] = 
            "HTTP Version Not Supported";

        // Custom 500 codes
        constexpr static char HTTP_RETDESC_OVERFLOW[] = 
            "Avoided buffer overflow";
        constexpr static char HTTP_RETDESC_HEADERS_SENT[] = 
            "Headers already sent";
        constexpr static char HTTP_RETDESC_REASON_TOO_LARGE[] = 
            "Reason too large";
        constexpr static char HTTP_RETDESC_RETCODE_NOT_SET[] = 
            "Response code not set";
        constexpr static char HTTP_RETDESC_TOO_MANY_COOKIES[] = 
            "Out of cookie storage";
        constexpr static char HTTP_RETDESC_TOO_MANY_QUERIES[] = 
            "Out of query storage";
        constexpr static char HTTP_RETDESC_BAD_CONTENT_TYPE[] = 
            "Unknown content type";
        constexpr static char HTTP_RETDESC_REQ_HANDLER_FAIL[] = 
            "Request handler failed";
        constexpr static char HTTP_RETDESC_HEADER_VAL_OVER[] = 
            "Header value buffer overflow";
        constexpr static char HTTP_RETDESC_QUERY_NAME_OVER[] = 
            "Cookie name buffer overflow";
        constexpr static char HTTP_RETDESC_QUERY_VAL_OVER[] = 
            "Cookie value buffer overflow";
        constexpr static char HTTP_RETDESC_COOKIE_NAME_OVER[] = 
            "Cookie name buffer overflow";
        constexpr static char HTTP_RETDESC_COOKIE_VAL_OVER[] = 
            "Cookie value buffer overflow";
        constexpr static char HTTP_RETDESC_CONTENT_TYPE_OVER[] = 
            "Content type was too long";
        constexpr static char HTTP_RETDESC_CONTENT_ENC_OVER[] = 
            "Content encoding was too long";
        constexpr static char HTTP_RETDESC_BAD_CACHE_TYPE[] = 
            "Invalid cache control type";

    http_response(http_context * ctx) 
        : m_status_code(http_response_code::HTTP_RETCODE_INT_SERVER_ERR),
            m_content_type(http_content_type::code::CONTENT_TYPE_UNKNOWN),
            m_http11(true), m_ctx(ctx)
        {
        }

        ~http_response() = default;

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

    private:
        
        constexpr static const char * CRLF = "\r\n";

        http_response_code m_status_code;
        http_content_type::code m_content_type;
        std::stringstream m_response_stream;
        std::string m_status_message;
        int m_http11;
        http_context * m_ctx;
        std::vector<std::tuple<std::string, std::string>> m_headers;
        bool m_chunked = false;
        bool m_nosize = false;
        bool m_should_write_header = true;
        bool m_header_written = false;
        std::string m_multipart_boundary;
    };
}
