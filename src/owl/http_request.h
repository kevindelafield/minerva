 #pragma once

#include <sstream>
#include <vector>
#include <string>
#include <tuple>
#include <map>
#include <istream>
#include <streambuf>
#include <deque>
#include "string_utils.h"
#include "time_utils.h"
#include "http_content_type.h"
#include "http_exception.h"

namespace owl
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
    
        const size_t MAX_CONTENT_LENGTH = 120 * 1024 * 1024;

        http_request(http_context * ctx);
        ~http_request() = default;

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

        const std::map<std::string, std::string, ci_less> & headers() const
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

        const std::map<std::string, std::string, ci_less> & query_parameters() const
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

        bool is_http11()
        {
            return m_http11;
        }

        std::istream & read_fully(int timeoutMs = 0);

        size_t read(char * buf, size_t len, int timeoutMs = 0);

        void read_chunk(std::vector<char> & buf, int timeoutMs = 0);

        http_content_type::code content_type()
        {
            return m_content_type;
        }

        static std::string url_decode(const std::string & url);

        bool null_body_read(int timeoutMs = 0);

        bool chunked() const
        {
            return m_chunked;
        }

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
        int m_chunk_size = 0;
        int m_chunk_read = 0;

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
                                owl::timer & timer,
                                int timeoutMs);

        bool m_chunked = false;
        bool _full_read = false;
        bool _partial_read = false;
        size_t _total_read = 0;
        size_t m_offset;
        std::map<std::string, std::string, ci_less> m_headers;
        METHOD m_method;
        bool m_http11;
        long long m_content_length;
        std::string m_path;
        std::map<std::string, std::string, ci_less> m_query_params;
        http_content_type::code m_content_type;
        std::string m_query_string;
        http_context * _ctx;
        std::deque<char> _overflow;
        std::stringstream _fullbuf;
        bool m_keep_alive = true;
        bool m_continue_100 = false;
    };
}
