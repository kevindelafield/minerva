#pragma once

#include <sstream>
#include <vector>
#include <string>
#include <tuple>
#include <map>
#include <istream>
#include <streambuf>
#include <deque>
#include <optional>
#include <util/string_utils.h>
#include <util/time_utils.h>
#include "http_content_type.h"
#include "http_exception.h"

namespace minerva
{

    class httpd;

    class http_context;

    class http_request
    {
    public:
        enum METHOD : int
        {
            GET,
                POST,
                HEAD,
                DELETE,
                PUT
                };
    
        static constexpr size_t MAX_CONTENT_LENGTH = 10 * 1024 * 1024; // Reduced from 120MB to 10MB
        static constexpr size_t MAX_HEADER_SIZE = 8 * 1024;            // Maximum size for individual headers
        static constexpr size_t MAX_HEADERS_COUNT = 100;               // Maximum number of headers
        static constexpr size_t MAX_CHUNK_SIZE = 16 * 1024 * 1024;     // 16MB max single chunk

        // Per-request override for the maximum body size, in bytes.
        // The default is MAX_CONTENT_LENGTH. The override applies only
        // to this request and is not persisted between requests on a
        // keep-alive connection.
        //
        // For Content-Length-framed requests, this must be set before
        // parse_header() runs (i.e. before httpd dispatches to a
        // controller); otherwise parse_header() will reject the request
        // against the default cap. For chunked requests, controllers may
        // raise the cap any time before reading the body.
        void max_content_length(size_t bytes) { m_max_content_length = bytes; }
        size_t max_content_length() const { return m_max_content_length; }

        http_request(http_context & ctx);
        ~http_request() = default;

        http_request(const http_request &)             = delete;
        http_request & operator=(const http_request &) = delete;
        http_request(http_request &&)                  = delete;
        http_request & operator=(http_request &&)      = delete;

        bool parse_header(const std::vector<char> & buf,
                          size_t offset);

        bool header(const char* key, std::string & value) const
        {
            auto it = m_headers.find(key);
            if (it != m_headers.end())
            {
                value = it->second;
                return true;
            }
            return false;
        }

        const std::map<std::string, std::string, minerva::ci_less> & headers() const
        {
            return m_headers;
        }

        const char * method_as_string() const
        {
            switch (m_method)
            {
            case METHOD::GET:
                return "GET";
            case METHOD::PUT:
                return "PUT";
            case METHOD::POST:
                return "POST";
            case METHOD::DELETE:
                return "DELETE";
            case METHOD::HEAD:
                return "HEAD";
            default:
                return "";
            }
        }

        METHOD method() const
        {
            return m_method;
        }
    
        bool keep_alive() const
        {
            return m_keep_alive;
        }

        void keep_alive(bool value)
        {
            m_keep_alive = value;
        }

        bool continue_100() const
        {
            return m_continue_100;
        }

        void continue_100(bool value)
        {
            m_continue_100 = value;
        }

        bool has_overflow();

        long long content_length() const
        {
            return m_content_length;
        }

        const std::string & path() const
        {
            return m_path;
        }

        const std::string & query_string() const
        {
            return m_query_string;
        }

        const std::map<std::string, std::string, minerva::ci_less> & query_parameters() const
        {
            return m_query_params;
        }

        std::string query_parameter(const char* key) const
        {
            auto it = m_query_params.find(key);
            if (it != m_query_params.end())
            {
                return it->second;
            }
            else
            {
                return std::string();
            }
        }

        bool is_http11() const
        {
            return m_http11;
        }

        std::istream & read_fully(int timeoutMs = 0);

        size_t read(char * buf, size_t len, int timeoutMs = 0);

        void read_chunk(std::vector<char> & buf, int timeoutMs = 0);

        http_content_type::code content_type() const
        {
            return m_content_type;
        }

        static std::string url_decode(const std::string & url);

        bool null_body_read(int timeoutMs = 0);

        bool chunked() const
        {
            return m_chunked;
        }

        // Maximum length accepted for a multipart boundary token.
        static constexpr size_t MAX_BOUNDARY_SIZE = 256;

        // Metadata describing a single part of a multipart/form-data request.
        // Populated by next_form().
        struct form_part
        {
            std::string name;          // the form field name
            std::string filename;      // file name for file uploads (may be empty)
            std::string content_type;  // the part's Content-Type (may be empty)
        };

        // True when the request carried a multipart/form-data Content-Type with
        // a valid boundary.
        bool is_multipart_form() const
        {
            return m_content_type ==
                http_content_type::code::CONTENT_TYPE_MULTIPART_FORM;
        }

        // The boundary token extracted from the multipart/form-data header.
        const std::string & multipart_boundary() const
        {
            return m_mp_boundary;
        }

        // Advance to the next part of a multipart/form-data request.  Returns
        // true and fills 'part' with the part metadata when a part is
        // available; returns false once the closing boundary is reached.  Any
        // unread bytes from the previous part are drained automatically.  After
        // a successful call the part body is read with read()/read_fully(),
        // which then operate on the current part only.  Throws http_exception
        // on a malformed stream.
        bool next_form(form_part & part, int timeoutMs = 0);

    private:

        enum CHUNK_STATE
        {
            READING_CHUNK_HEADER,
            READING_CHUNK_BODY,
            READING_CHUNK_TERMINATOR,
            READING_END,
            DONE
        };

        CHUNK_STATE m_chunk_state = CHUNK_STATE::READING_CHUNK_HEADER;
        size_t m_chunk_size = 0;
        size_t m_chunk_read = 0;

        friend class httpd;

        void chunked(bool value)
        {
            m_chunked = value;
        }

        bool null_body_read_cl(int timeoutMs);

        bool null_body_read_chunked(int timeoutMs);

        std::istream & read_fully_cl(int timeoutMs);

        std::istream & read_fully_chunked(int timeoutMs);

        size_t read_cl(char * buf, size_t len, int timeoutMs);

        size_t read_chunked(char * buf, size_t len, int timeoutMs);

        size_t read_from_socket(char * buf, size_t len,
                                minerva::timer & timer,
                                int timeoutMs);

        // ---- multipart/form-data streaming engine ----

        enum MP_STATE
        {
            MP_INIT,     // before the opening boundary
            MP_HEADERS,  // positioned at a part's headers
            MP_BODY,     // a part body is available to read
            MP_DONE      // closing boundary consumed
        };

        // Pull up to len decoded body bytes from the transport (content-length
        // or chunked), returning 0 at end of body.
        size_t read_raw_body(char * buf, size_t len, int timeoutMs);

        // Read a single CRLF-terminated line from the decoded body into 'line'
        // (without the trailing CRLF).  Returns false at end of body.
        bool mp_read_line(std::string & line, int timeoutMs);

        // Locate the boundary delimiter ("\r\n--<boundary>") in m_mp_raw.
        // Returns the offset or m_mp_raw.size()+1 when not found.
        size_t mp_find_delim() const;

        // Consume the boundary delimiter at the front of m_mp_raw and move to
        // MP_HEADERS or MP_DONE.
        void mp_consume_delimiter(int timeoutMs);

        // Deliver (or discard) current part body bytes up to the delimiter.
        // Returns 0 when the current part is exhausted.
        size_t mp_body_read(char * out, size_t maxlen, bool discard,
                            int timeoutMs);

        // Skip the opening boundary (and any preamble) for the first part.
        void mp_init_first_boundary(int timeoutMs);

        // Parse the headers of the current part into 'part'.
        void mp_parse_part_headers(form_part & part, int timeoutMs);

        // Per-part read_fully / read used when multipart traversal is active.
        std::istream & mp_read_fully(int timeoutMs);
        size_t mp_read(char * buf, size_t len, int timeoutMs);

        // Extract a parameter value (e.g. name, filename) from a header value.
        static std::string mp_extract_param(const std::string & value,
                                            const char * key);

        bool                                                 m_chunked       = false;
        bool                                                 m_full_read     = false;
        bool                                                 m_partial_read  = false;
        size_t                                               m_total_read    = 0;
        size_t                                               m_offset        = 0;
        std::map<std::string, std::string, minerva::ci_less> m_headers;
        METHOD                                               m_method        {METHOD::GET};
        bool                                                 m_http11        {false};
        long long                                            m_content_length{0};
        std::string                                          m_path;
        std::map<std::string, std::string, minerva::ci_less> m_query_params;
        http_content_type::code                              m_content_type  {http_content_type::code::CONTENT_TYPE_UNKNOWN};
        std::string                                          m_query_string;
        http_context &                                       m_ctx;
        std::deque<char>                                     m_overflow;
        std::optional<std::stringstream>                     m_fullbuf;
        bool                                                 m_keep_alive    = true;
        bool                                                 m_continue_100  = false;
        size_t                                               m_max_content_length{MAX_CONTENT_LENGTH};

        // multipart/form-data parsing state
        std::string                                          m_mp_boundary;
        std::string                                          m_mp_delim;
        std::deque<char>                                     m_mp_raw;
        MP_STATE                                             m_mp_state          = MP_STATE::MP_INIT;
        bool                                                 m_multipart_active  = false;
    };
}
