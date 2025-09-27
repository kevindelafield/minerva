#include <cassert>
#include "http_response.h"
#include "http_context.h"
#include "http_exception.h"
#include "http_content_type.h"

namespace minerva
{

    std::string http_response::get_status_code_string(http_response_code code)
    {
        switch (code)
        {
        case HTTP_RETCODE_UNAUTHORIZED:
            return HTTP_RETDESC_UNAUTHORIZED;
        case HTTP_RETCODE_SUCCESS:
            return HTTP_RETDESC_SUCCESS;
        case HTTP_RETCODE_CREATED:
            return HTTP_RETDESC_CREATED;
        case HTTP_RETCODE_NO_CONTENT:
            return HTTP_RETDESC_NO_CONTENT;
        case HTTP_RETCODE_NOT_MODIFIED:
            return HTTP_RETDESC_NOT_MODIFIED;
        case HTTP_RETCODE_CONFLICT:
            return HTTP_RETDESC_CONFLICT;
        case HTTP_RETCODE_BAD_REQUEST:
            return HTTP_RETDESC_BAD_REQUEST;
        case HTTP_RETCODE_FORBIDDEN:
            return HTTP_RETDESC_FORBIDDEN;
        case HTTP_RETCODE_NOT_FOUND:
            return HTTP_RETDESC_NOT_FOUND;
        case HTTP_RETCODE_INT_SERVER_ERR:
            return HTTP_RETDESC_INT_SERVER_ERR;
        case HTTP_RETCODE_NOT_IMPLEMENTED:
            return HTTP_RETDESC_NOT_IMPLEMENTED;
        case HTTP_RETCODE_UNAVAILABLE:
            return HTTP_RETDESC_UNAVAILABLE;
        case HTTP_RETCODE_VER_NOT_SUPPORTED:
            return HTTP_RETDESC_VER_NOT_SUPPORTED;
        default:
            return HTTP_RETDESC_INT_SERVER_ERR;
        }
    }

    constexpr static size_t BUFFER_SIZE = 15*1024;

    bool http_response::send_buffer(std::istream & is)
    {
        is.seekg(0, is.end);
        auto length = is.tellg();
        is.seekg(0, is.beg);

        char buf[BUFFER_SIZE];

        bool writing = true;

        size_t to_read = std::min(static_cast<size_t>(length), BUFFER_SIZE);
        while (to_read > 0)
        {
            is.read(buf, to_read);
            length -= to_read;

            ssize_t total = 0;
            while (total < to_read)
            {
                ssize_t left = to_read - total;
                ssize_t sent;

                if (m_ctx->should_shutdown())
                {
                    return false;
                }

                // check for aggregate timeout
                if (m_ctx->timed_out())
                {
                    LOG_DEBUG("Socket write timeout");
                    return false;
                }

                bool read_flag = !writing;
                bool write_flag = true;
                bool error_flag = writing;

                int poll_status = 
                    m_ctx->conn()->poll(read_flag, write_flag, error_flag,
                                       500);

                if (poll_status < 0)
                {
                    LOG_WARN_ERRNO("Poll error", errno);
                    return false;
                }
                else if (poll_status == 0)
                {
                    // timeout
                    continue;
                }
                else if (error_flag)
                {
                    LOG_DEBUG("Poll write socket error");
                    return false;
                }

                auto status = m_ctx->conn()->write(buf + total, left, sent);
                switch (status)
                {
                case connection::CONNECTION_ERROR:
                {
                    LOG_DEBUG_ERRNO("Http client timeout or socketwrite error",
                                    errno);
                    return false;
                }
                break;
                case connection::CONNECTION_OK:
                {
                    writing = true;
                    total += sent;
                }
                break;
                case connection::CONNECTION_WANTS_WRITE:
                {
                    writing = true;
                }
                break;
                case connection::CONNECTION_WANTS_READ:
                {
                    writing = false;
                }
                break;
                case connection::CONNECTION_CLOSED:
                {
                    LOG_DEBUG("Connection closed during write");
                    return false;
                }
                break;
                }
            }
            to_read = std::min(static_cast<size_t>(length), BUFFER_SIZE);
        }
        return true;
    }

    bool http_response::write_header()
    {
        if (!should_write_header())
        {
            return true;
        }

        std::stringstream os;

        auto content_length = m_ctx->response().response_stream().tellp();
        // handle unwritten stream
        if (content_length < 0)
        {
            content_length = 0;
        }

        // get status code and message
        http_response::http_response_code code = status_code();
        std::string status_msg = status_message();
        if (status_msg.size() == 0)
        {
            status_msg = http_response::get_status_code_string(code);
        }

        LOG_DEBUG("HTTP response code: " << code);

        // get content type
        auto ct = content_type();
        std::string content_type_str = 
            http_content_type::get_content_type_string(ct);

        // write header
        if (is_http11())
        {
            os << "HTTP/1.1 ";
        }
        else
        {
            os << "HTTP/1.0 ";
        }
        os << code;
        os << " ";
        os << http_response::get_status_code_string(code);
        os << CRLF;
        if (ct != http_content_type::CONTENT_TYPE_UNKNOWN)
        {
            os << "Content-Type: ";
            os << content_type_str;
            if (ct == http_content_type::CONTENT_TYPE_MULTIPART_FORM)
            {
                os << "; boundary=" << multipart_boundary();
            }
            os << CRLF;
        }
        if (m_ctx->request().keep_alive())
        {
            os << "Connection: keep-alive";
        }
        else
        {
            os << "Connection: close";
        }
        os << CRLF;
        if (!no_size())
        {
            if (chunked())
            {
                os << "Transfer-Encoding: chunked";
                os << CRLF;
            }
            else
            {
                os << "Content-Length: ";
                os << content_length;
                os << CRLF;
            }
        }
        for (auto & pair : headers())
        {
            os << std::get<0>(pair);
            os << ": ";
            os << std::get<1>(pair);
            os << CRLF;
        }
        os << CRLF;
        os.flush();
        // check for write failures
        if (os.fail())
        {
            return false;
        }

        LOG_DEBUG("sending HTTP response header: " << os.str());

        return send_buffer(os);
    }

    bool http_response::flush_final_chunk()
    {
        assert(m_chunked);

        try
        {
            flush();
        }
        catch (http_exception & exc)
        {
            LOG_DEBUG("Http exception: " << exc.what());
            return false;
        }

        if (!no_size())
        {

            LOG_DEBUG("flushing final chunk");
            
            std::stringstream ss;
            
            ss << std::hex << "0\r\n\r\n";
            
            if (!send_buffer(ss))
            {
                return false;
            }
        }

        return true;
    }

    void http_response::flush()
    {
        m_chunked = true;
        if (!header_written())
        {
            if (!write_header())
            {
                throw http_exception("failed to write http response header");
            }
            m_header_written = true;
        }
        // send chunk
        m_response_stream.seekg(0, m_response_stream.end);
        auto sz = m_response_stream.tellg();
        m_response_stream.seekg(0, m_response_stream.beg);

        LOG_DEBUG("sending chunk of size " << sz);

        if (sz > 0)
        {
            if (!no_size())
            {

                std::stringstream ss;
                // write chunk size in hex
                ss << std::hex << sz << "\r\n";
                std::string hdr = ss.str();
                
                LOG_DEBUG("sending chunk header: " << hdr);
                
                if (!send_buffer(ss))
                {
                    throw http_exception("failed to write chunk header to http client");
                }
            }

            LOG_DEBUG("sending chunk body");

            // write data
            if (!send_buffer(m_response_stream))
            {
                throw http_exception("failed to write chunk body to http client");
            }
            if (!no_size())
            {
                LOG_DEBUG("sending chunk terminator");
                
                std::stringstream ss;
                ss << "\r\n";
                if (!send_buffer(ss))
                {
                    throw http_exception("failed to write chunk terminator to http client");
                }
            }
            
            // clear stream
            m_response_stream.str(std::string());
        }
    }
}
