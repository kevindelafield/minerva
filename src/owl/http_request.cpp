#include <sstream>
#include <regex>
#include <cassert>
#include <curl/curl.h>
#include "http_context.h"
#include "http_request.h"
#include "connection.h"
#include "log.h"
#include "string_utils.h"

namespace owl
{

    static const char* firstLineRegex = "^(.+)\\s+(.+)\\s+HTTP/(1.0|1.1)\r$";
    static const char* headerRegex = "^(.+):\\s+(.+)\r$";
    static const char* contentLength = "content-length";
    static const char* contentType = "content-type";
    static const char* connectionKey = "connection";
    static const char* keepAlive = "keep-alive";
    static const char* expectKey = "expect";
    static const char* continue100 = "100-continue";

    static char tmpBuf[1];

    http_request::http_request(http_context * ctx) :
        m_http11(false), m_method(METHOD::GET),
        m_content_length(0),
        m_content_type(http_content_type::code::CONTENT_TYPE_UNKNOWN),
        _ctx(ctx)
    {
    }

    std::string http_request::url_decode(const std::string & value)
    {
        char* output = curl_unescape(value.c_str(), static_cast<int>(value.size()));
        assert(output);
        std::string res(output);
        curl_free(output);
        return res;
    }

    static bool is_number(const std::string& s)
    {
        std::string::const_iterator it = s.begin();
        while (it != s.end() && std::isdigit(*it)) ++it;
        return !s.empty() && it == s.end();
    }

    bool http_request::parse_header(const std::vector<char> & buf,
                                    size_t offset)
    {
        m_offset = offset;

        std::string header(buf.data(), offset);
        std::stringstream ss(header);
        std::string line;

        if (!std::getline(ss, line))
        {
            return false;
        }
    
        // scan first line
        std::regex regex(firstLineRegex);
        std::smatch match;
        std::regex_search(line, match, regex);
        if (match.size() != 4)
        {
            LOG_WARN("Invalid http request header: " << line.c_str());
            return false;
        }
        std::string method = match[1];
        std::string path = match[2];
        std::string version = match[3];

        // handle the http://host::port prefix possibility
        if (path[0] != '/')
        {
            auto pos = path.find_first_of("/");
            if (pos == std::string::npos)
            {
                return false;
            }
            if (pos + 1 == path.size())
            {
                return false;
            }
            pos = path.find_first_of("/", pos+1);
            if (pos == std::string::npos)
            {
                return false;
            }
            if (pos + 1 == path.size())
            {
                return false;
            }
            pos = path.find_first_of("/", pos+1);
            if (pos == std::string::npos)
            {
                return false;
            }
            path = path.substr(pos);
        }

        LOG_DEBUG("path: " << path);

        // parse method
        if (method == "GET")
        {
            m_method = METHOD::GET;
        }
        else if (method == "POST")
        {
            m_method = METHOD::POST;
        }
        else if (method == "PUT")
        {
            m_method = METHOD::PUT;
        }
        else if (method == "DELETE")
        {
            m_method = METHOD::DELETE;
        }
        else if (method == "HEAD")
        {
            m_method = METHOD::HEAD;
        }
        else
        {
            LOG_WARN("Invalid http method: " << method.c_str());
            return false;
        }
    
        // set http version
        if (version == "1.1")
        {
            m_http11 = true;
        }

        // get path and query string
        size_t index = path.find('?');
        if (index != std::string::npos)
        {
            // set path
            m_path = path.substr(0, index);
            // parse query params
            if (index < path.size() - 1)
            {
                std::string qs = path.substr(index + 1, path.size() - index - 1);
                m_query_string = qs;
                std::stringstream query_stream(qs);
                std::string item;
                while (std::getline(query_stream, item, '&'))
                {
                    std::string key;
                    std::string value;
                    index = item.find('=');
                    if (index == std::string::npos)
                    {
                        key = url_decode(item);
                        value = "";
                    }
                    else if (index >= item.size() - 1)
                    {
                        key = url_decode(item);
                        value = "";
                    }
                    else
                    {
                        key = url_decode(item.substr(0, index));
                        value =
                            url_decode(item.substr(index + 1,
                                                   item.size() - index - 1));
                    }
                    m_query_params[key] = value;
                }
            }
        }
        // no query params = just a path
        else
        {
            m_path = path;
        }

        std::string content_length(contentLength);
        std::string content_type(contentType);

        bool has_content_length = false;

        // read headers line by line
        while (std::getline(ss, line))
        {
            // check for empty line
            if (line.size() == 1 && line[0] == '\r')
            {
                break;
            }
            // search header
            regex = headerRegex;
            std::regex_search(line, match, regex);
            if (match.size() != 3)
            {
                LOG_WARN("Invalid http header: " << line.c_str());
                return false;
            }
            // extract header
            std::string key = match[1];
            std::string value = match[2];

            LOG_DEBUG("header: " << key << "=" << value);

            // add to map
            m_headers[key] = value;
        
            // for content length - set it here
            if (ci_equals(key, content_length))
            {
                if (!is_number(value))
                {
                    LOG_WARN("Invalid content length on http request header: " <<
                             value.c_str());
                    return false;
                }
                try
                {
                    m_content_length = std::stoll(value);
                    if (m_content_length < 0)
                    {
                        LOG_WARN("Negative content length on http request header: " <<
                                 value.c_str());
                        return false;
                    }
                    if (m_content_length > MAX_CONTENT_LENGTH)
                    {
                        LOG_WARN("Content length exceeds maximum: " <<
                                 m_content_length);
                        return false;
                    }
                    has_content_length = true;
                }
                catch (const std::invalid_argument)
                {
                    LOG_WARN("Invalid content length on http request header: " <<
                             value.c_str());
                    return false;
                }
                catch (const std::out_of_range)
                {
                    LOG_WARN("Invalid content length on http request header: " <<
                             value.c_str());
                    return false;
                }
            }
        
            // for content type - set it here
            else if (ci_equals(key, content_type))
            {
                m_content_type = http_content_type::parse(value);
            }
            // for connection - set it here
            else if (ci_equals(key, connectionKey))
            {
                m_keep_alive = ci_equals(value, keepAlive);
            }
            // for connection - set it here
            else if (ci_equals(key, expectKey))
            {
                m_continue_100 = ci_equals(value, continue100);
            }
            else if (ci_equals(key, "transfer-encoding"))
            {
                if (ci_equals(value, "chunked"))
                {
                    m_chunked = true;
                }
                else
                {
                    LOG_WARN("invalid transfer encoding http header: " << 
                             value);
                    return false;
                }
            }
        }

        if (has_content_length && m_chunked)
        {
            LOG_WARN("received content length and chunked transfer encoding header");
            return false;
        }

        // validate post and put have content length
        if ((m_method == METHOD::GET || m_method == METHOD::HEAD) &&
            m_content_length > 0)
        {
            LOG_WARN("Invalid request: GET or HEAD method with content length");
            return false;
        }

        // add overflow for body reads
        _overflow.clear();
        _overflow.insert(_overflow.end(),
                         buf.begin() + offset,
                         buf.end());

        if (!m_chunked && _overflow.size() > m_content_length)
        {
            LOG_WARN("Invalid reuqest: more content then provided in content length");
            return false;
        }

        return true;
    }            

    std::istream & http_request::read_fully_cl(int timeoutMs)
    {
        std::copy(_overflow.begin(), _overflow.end(),
                  std::ostream_iterator<char>(_fullbuf));

        _total_read = _overflow.size();

        size_t left = m_content_length - _overflow.size();

        _overflow.clear();

        owl::timer _timer(true);

        while (left > 0)
        {
            char buf[10*1024];
            size_t to_read = std::min(sizeof(buf), left);
            size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
            _fullbuf.write(buf, read);
            left -= read;
            _total_read += read;
        }
        return _fullbuf;
    }

    std::istream & http_request::read_fully_chunked(int timeoutMs)
    {
        owl::timer _timer(true);

        while (m_chunk_state != CHUNK_STATE::DONE)
        {

            switch (m_chunk_state)
            {
            case CHUNK_STATE::READING_CHUNK_HEADER:
            {
                bool found_nl = false;
                int nl_index = 0;
                for (int i = 0; i<_overflow.size(); i++)
                {
                    if (_overflow[i] == '\r' &&
                        _overflow.size() > i + 1 &&
                        _overflow[i+1] == '\n')
                    {
                        found_nl = true;
                        nl_index = i;
                        break;
                    }
                }
                if (found_nl)
                {
                    std::string hex;
                    for (int i=0; i<nl_index; i++)
                    {
                        hex += _overflow.front();
                        _overflow.pop_front();
                    }
                    _overflow.pop_front();
                    _overflow.pop_front();

                    if (hex.size() % 2 != 0)
                    {
                        LOG_WARN("invalid chunk header: " << hex);
                        throw http_exception("invalid chunk header");
                    }

                    std::stringstream ss;
                    try
                    {
                        ss << std::hex << hex;
                        ss >> m_chunk_size;
                    }
                    catch (std::exception & exc)
                    {
                        LOG_WARN("invalid chunk header: " << hex);
                        throw http_exception("invalid chunk header");
                    }
                    if (m_chunk_size == 0)
                    {
                        m_chunk_state = CHUNK_STATE::READING_END;
                    }
                    else
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_BODY;
                        m_chunk_read = 0;
                    }
                }
                else if (_overflow.size() < 100)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                else
                {
                    LOG_WARN("failed to find chunk header");
                    throw http_exception("missing chunk header");
                }
            }
            break;

            case CHUNK_STATE::READING_CHUNK_BODY:
            {
                if (!_overflow.empty())
                {
                    size_t to_read = 
                        std::min(_overflow.size(), 
                                 static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                    std::copy(_overflow.begin(), 
                              _overflow.begin() + to_read,
                              std::ostream_iterator<char>(_fullbuf));
                    _overflow.erase(_overflow.begin(),
                                    _overflow.begin() + to_read);
                    m_chunk_read += to_read;
                }

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    break;
                }

                char buf[20*1024];
                size_t to_read = 
                    std::min(sizeof(buf), 
                             static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
                // read will be > 0
                std::copy(buf, buf+read,
                          std::ostream_iterator<char>(_fullbuf));
                m_chunk_read += read;

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    break;
                }
            }
            break;

            case CHUNK_STATE::READING_CHUNK_TERMINATOR:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() > 1)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_HEADER;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
            }
            break;

            case CHUNK_STATE::READING_END:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() == 2)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::DONE;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
                else if (_overflow.size() > 2)
                {
                    LOG_WARN("received data after final chunk terminator");
                    throw http_exception("invalid chunk end terminator");
                }
            }
            break;
            }
        }
        return _fullbuf;
    }

    void http_request::read_chunk(std::vector<char> & buffer, int timeoutMs)
    {
        if (!chunked())
        {
            throw new http_exception("chunk protocol violation");
        }
        _partial_read = true;

        buffer.clear();

        owl::timer _timer(true);

        if (m_chunk_state != CHUNK_STATE::READING_CHUNK_HEADER &&
            m_chunk_state != CHUNK_STATE::READING_END &&
            m_chunk_state != CHUNK_STATE::DONE)
        {
            throw http_exception("chunk protocol violation");
        }

        while (m_chunk_state != CHUNK_STATE::DONE)
        {

            switch (m_chunk_state)
            {
            case CHUNK_STATE::READING_CHUNK_HEADER:
            {
                bool found_nl = false;
                int nl_index = 0;
                for (int i = 0; i<_overflow.size(); i++)
                {
                    if (_overflow[i] == '\r' &&
                        _overflow.size() > i + 1 &&
                        _overflow[i+1] == '\n')
                    {
                        found_nl = true;
                        nl_index = i;
                        break;
                    }
                }
                if (found_nl)
                {
                    std::string hex;
                    for (int i=0; i<nl_index; i++)
                    {
                        hex += _overflow.front();
                        _overflow.pop_front();
                    }
                    _overflow.pop_front();
                    _overflow.pop_front();
                    std::stringstream ss;
                    try
                    {
                        ss << std::hex << hex;
                        ss >> m_chunk_size;
                    }
                    catch (std::exception & exc)
                    {
                        LOG_WARN("invalid chunk header: " << hex);
                        throw http_exception("invalid chunk header");
                    }
                    if (m_chunk_size == 0)
                    {
                        m_chunk_state = CHUNK_STATE::READING_END;
                    }
                    else
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_BODY;
                        m_chunk_read = 0;
                        buffer.reserve(m_chunk_size);
                    }
                }
                else if (_overflow.size() < 100)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                else
                {
                    LOG_WARN("failed to find chunk header");
                    throw http_exception("missing chunk header");
                }
            }
            break;

            case CHUNK_STATE::READING_CHUNK_BODY:
            {
                if (!_overflow.empty())
                {
                    size_t to_read = 
                        std::min(_overflow.size(), 
                                 static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                    std::copy(_overflow.begin(), 
                              _overflow.begin() + to_read,
                              std::back_inserter(buffer));
                    _overflow.erase(_overflow.begin(),
                                    _overflow.begin() + to_read);
                    m_chunk_read += to_read;
                }

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    return;
                }

                char buf[20*1024];
                size_t to_read = 
                    std::min(sizeof(buf), 
                             static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
                // read will be > 0
                std::copy(buf, buf+read,
                          std::back_inserter(buffer));
                m_chunk_read += read;

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    return;
                }
            }
            break;

            case CHUNK_STATE::READING_CHUNK_TERMINATOR:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() > 1)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_HEADER;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
            }
            break;

            case CHUNK_STATE::READING_END:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() == 2)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::DONE;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
                else if (_overflow.size() > 2)
                {
                    LOG_WARN("received data after final chunk terminator");
                    throw http_exception("invalid chunk end terminator");
                }
            }
            break;
            }
        }
    }

    std::istream & http_request::read_fully(int timeoutMs)
    {
        if (_partial_read)
        {
            throw http_exception("protocol violation");
        }
        if (_full_read)
        {
            throw http_exception("protocol violation");
        }
        _full_read = true;
    
        if (chunked())
        {
            return read_fully_chunked(timeoutMs);
        }
        else
        {
            return read_fully_cl(timeoutMs);
        }
    }

    bool http_request::has_overflow()
    {
        if (_overflow.size() > 0)
        {
            LOG_DEBUG("has overflow");
            return true;
        }

        bool read_flag = true;
        bool write_flag = false;
        bool error_flag = true;
        int poll_status = 
            _ctx->conn()->poll(read_flag, write_flag, error_flag, 0);
        if (poll_status < 0)
        {
            LOG_WARN_ERRNO("Poll error", _ctx->conn()->get_last_error());
            throw http_exception("poll error");
        }
        else if (poll_status == 0)
        {
            // timeout
            LOG_DEBUG("no overflow");
            return false;
        }
        else
        {
            LOG_DEBUG("overflow poll read");
            return true;
        }
    }

    size_t http_request::read_cl(char * buf, size_t len, int timeoutMs)
    {
        if (_total_read >= m_content_length)
        {
            return 0;
        }

        if (!_overflow.empty())
        {
            auto it = _overflow.cbegin();
            size_t to_copy = std::min(len, _overflow.size());
            for (int i=0; i<to_copy; i++, it++)
            {
                buf[i] = *it;
            }

            _overflow.erase(_overflow.begin(), _overflow.begin() + to_copy);
            _total_read += to_copy;
            return to_copy;
        }

        size_t to_read =
            std::min(len,
                     static_cast<unsigned long>(m_content_length) - _total_read);
        if (to_read == 0)
        {
            return 0;
        }

        owl::timer _timer(true);
        size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
        _total_read += read;
    
        return read;
    }

    size_t http_request::read_chunked(char * buf, size_t len, int timeoutMs)
    {
        owl::timer _timer(true);

        while (m_chunk_state != CHUNK_STATE::DONE)
        {

            switch (m_chunk_state)
            {
            case CHUNK_STATE::READING_CHUNK_HEADER:
            {
                bool found_nl = false;
                int nl_index = 0;
                for (int i = 0; i<_overflow.size(); i++)
                {
                    if (_overflow[i] == '\r' &&
                        _overflow.size() > i + 1 &&
                        _overflow[i+1] == '\n')
                    {
                        found_nl = true;
                        nl_index = i;
                        break;
                    }
                }
                if (found_nl)
                {
                    std::string hex;
                    for (int i=0; i<nl_index; i++)
                    {
                        hex += _overflow.front();
                        _overflow.pop_front();
                    }
                    _overflow.pop_front();
                    _overflow.pop_front();
                    std::stringstream ss;
                    try
                    {
                        ss << std::hex << hex;
                        ss >> m_chunk_size;
                    }
                    catch (std::exception & exc)
                    {
                        LOG_WARN("invalid chunk header: " << hex);
                        throw http_exception("invalid chunk header");
                    }
                    if (m_chunk_size == 0)
                    {
                        m_chunk_state = CHUNK_STATE::READING_END;
                    }
                    else
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_BODY;
                        m_chunk_read = 0;
                    }
                }
                else if (_overflow.size() < 100)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                else
                {
                    LOG_WARN("failed to find chunk header");
                    throw http_exception("missing chunk header");
                }
            }
            break;

            case CHUNK_STATE::READING_CHUNK_BODY:
            {
                if (!_overflow.empty())
                {
                    auto it = _overflow.cbegin();
                    size_t to_copy = 
                        std::min(static_cast<unsigned long>(m_chunk_size - m_chunk_read), 
                                 std::min(len, _overflow.size()));
                    for (int i=0; i<to_copy; i++, it++)
                    {
                        buf[i] = *it;
                    }

                    _overflow.erase(_overflow.begin(), 
                                    _overflow.begin() + to_copy);
                    _total_read += to_copy;
                    m_chunk_read += to_copy;
                    if (m_chunk_read == m_chunk_size)
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    }
                    return to_copy;
                }

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    break;
                }

                size_t to_read = 
                    std::min(len, 
                             static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
                m_chunk_read += read;
                _total_read += read;

                if (m_chunk_read == m_chunk_size)
                {
                    m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                }
                return read;
            }
            break;

            case CHUNK_STATE::READING_CHUNK_TERMINATOR:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() > 1)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_HEADER;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
            }
            break;

            case CHUNK_STATE::READING_END:
            {
                if (_overflow.size() < 2)
                {
                    char buf[100];
                    size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                    // read will be > 0
                    std::copy(buf, buf+read,
                              std::back_inserter(_overflow));
                }
                if (_overflow.size() == 2)
                {
                    if (_overflow[0] == '\r' &&
                        _overflow[1] == '\n')
                    {
                        _overflow.pop_front();
                        _overflow.pop_front();
                        m_chunk_state = CHUNK_STATE::DONE;
                    }
                    else
                    {
                        LOG_WARN("invalid chunk terminator");
                        throw http_exception("invalid chunk terminator");
                    }
                }
                else if (_overflow.size() > 2)
                {
                    LOG_WARN("received data after final chunk terminator");
                    throw http_exception("invalid chunk end terminator");
                }
            }
            break;
            }
        }
        return 0;
    }

    size_t http_request::read(char * buf, size_t len, int timeoutMs)
    {
        if (_full_read)
        {
            throw http_exception("protocol violation");
        }
        _partial_read = true;

        if (chunked())
        {
            return read_chunked(buf, len, timeoutMs);
        }
        else
        {
            return read_cl(buf, len, timeoutMs);
        }
    }

    bool http_request::null_body_read_cl(int timeoutMs)
    {
        size_t left = m_content_length - _total_read - _overflow.size();

        char buf[64*1024];
        owl::timer timer(true);
        size_t to_read = std::min(left, sizeof(buf));
        try
        {
            while (left)
            {
                size_t read = read_from_socket(buf, to_read, timer, timeoutMs);
                left -= read;
                to_read = std::min(left, sizeof(buf));
            }
            return true;
        }
        catch (http_exception & e)
        {
            LOG_DEBUG("Failed to do null body read: " << e.what());
            return false;
        }
    }

    bool http_request::null_body_read_chunked(int timeoutMs)
    {
        owl::timer _timer(true);

        try
        {

            while (m_chunk_state != CHUNK_STATE::DONE)
            {

                switch (m_chunk_state)
                {
                case CHUNK_STATE::READING_CHUNK_HEADER:
                {
                    bool found_nl = false;
                    int nl_index = 0;
                    for (int i = 0; i<_overflow.size(); i++)
                    {
                        if (_overflow[i] == '\r' &&
                            _overflow.size() > i + 1 &&
                            _overflow[i+1] == '\n')
                        {
                            found_nl = true;
                            nl_index = i;
                            break;
                        }
                    }
                    if (found_nl)
                    {
                        std::string hex;
                        for (int i=0; i<nl_index; i++)
                        {
                            hex += _overflow.front();
                            _overflow.pop_front();
                        }
                        _overflow.pop_front();
                        _overflow.pop_front();
                        std::stringstream ss;
                        try
                        {
                            ss << std::hex << hex;
                            ss >> m_chunk_size;
                        }
                        catch (std::exception & exc)
                        {
                            LOG_WARN("invalid chunk header: " << hex);
                            throw http_exception("invalid chunk header");
                        }
                        if (m_chunk_size == 0)
                        {
                            m_chunk_state = CHUNK_STATE::READING_END;
                        }
                        else
                        {
                            m_chunk_state = CHUNK_STATE::READING_CHUNK_BODY;
                            m_chunk_read = 0;
                        }
                    }
                    else if (_overflow.size() < 100)
                    {
                        char buf[100];
                        size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                        // read will be > 0
                        std::copy(buf, buf+read,
                                  std::back_inserter(_overflow));
                    }
                    else
                    {
                        LOG_WARN("failed to find chunk header");
                        throw http_exception("missing chunk header");
                    }
                }
                break;

                case CHUNK_STATE::READING_CHUNK_BODY:
                {
                    if (!_overflow.empty())
                    {
                        size_t to_erase = 
                            std::min(_overflow.size(), 
                                     static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                        _overflow.erase(_overflow.begin(), 
                                        _overflow.begin() + to_erase);
                        _total_read += to_erase;
                        m_chunk_read += to_erase;
                    }

                    if (m_chunk_read == m_chunk_size)
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                        break;
                    }

                    char buf[20*1024];

                    size_t to_read = 
                        std::min(sizeof(buf), 
                                 static_cast<unsigned long>(m_chunk_size - m_chunk_read));
                    size_t read = read_from_socket(buf, to_read, _timer, timeoutMs);
                    m_chunk_read += read;
                    _total_read += read;

                    if (m_chunk_read == m_chunk_size)
                    {
                        m_chunk_state = CHUNK_STATE::READING_CHUNK_TERMINATOR;
                    }
                }
                break;

                case CHUNK_STATE::READING_CHUNK_TERMINATOR:
                {
                    if (_overflow.size() < 2)
                    {
                        char buf[100];
                        size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                        // read will be > 0
                        std::copy(buf, buf+read,
                                  std::back_inserter(_overflow));
                    }
                    if (_overflow.size() > 1)
                    {
                        if (_overflow[0] == '\r' &&
                            _overflow[1] == '\n')
                        {
                            _overflow.pop_front();
                            _overflow.pop_front();
                            m_chunk_state = CHUNK_STATE::READING_CHUNK_HEADER;
                        }
                        else
                        {
                            LOG_WARN("invalid chunk terminator");
                            throw http_exception("invalid chunk terminator");
                        }
                    }
                }
                break;

                case CHUNK_STATE::READING_END:
                {
                    if (_overflow.size() < 2)
                    {
                        char buf[100];
                        size_t read = read_from_socket(buf, sizeof(buf), _timer, timeoutMs);
                        // read will be > 0
                        std::copy(buf, buf+read,
                                  std::back_inserter(_overflow));
                    }
                    if (_overflow.size() == 2)
                    {
                        if (_overflow[0] == '\r' &&
                            _overflow[1] == '\n')
                        {
                            _overflow.pop_front();
                            _overflow.pop_front();
                            m_chunk_state = CHUNK_STATE::DONE;
                        }
                        else
                        {
                            LOG_WARN("invalid chunk terminator");
                            throw http_exception("invalid chunk terminator");
                        }
                    }
                    else if (_overflow.size() > 2)
                    {
                        LOG_WARN("received data after final chunk terminator");
                        throw http_exception("invalid chunk end terminator");
                    }
                }
                break;
                }
            }
            return true;
        }
        catch (http_exception & e)
        {
            LOG_DEBUG("Failed to do null body read: " << e.what());
            return false;
        }
    }

    bool http_request::null_body_read(int timeoutMs)
    {
        if (chunked())
        {
            return null_body_read_chunked(timeoutMs);
        }
        else
        {
            return null_body_read_cl(timeoutMs);
        }
    }

    size_t http_request::read_from_socket(char * buf, size_t len,
                                          owl::timer & timer,
                                          int timeoutMs)
    {
        bool reading = true;
        while (true)
        {
            // read from socket
            bool done = false;
            do
            {
                if (_ctx->should_shutdown())
                {
                    throw http_exception("server shutdown");
                }
        
                if (_ctx->timed_out())
                {
                    throw http_exception("operation timeout");
                }
        
                if (timeoutMs > 0 && timer.get_elapsed_milliseconds() >= timeoutMs)
                {
                    throw http_exception("read timeout");
                }
        
                ssize_t read;
        
                bool read_flag = reading;
                bool write_flag = !reading;
                bool error_flag = true;
                int poll_status = 
                    _ctx->conn()->poll(read_flag, write_flag, error_flag, 100);
                if (poll_status < 0)
                {
                    LOG_WARN_ERRNO("Poll error", _ctx->conn()->get_last_error());
                    throw http_exception("poll error");
                }
                else if (poll_status == 0)
                {
                    // timeout
                    // TODO continue
                    done = true;
                }
                else if (error_flag)
                {
                    LOG_WARN("Poll read socket error");
                    throw http_exception("read error");
                }
                else
                {
                    done = true;
                }
            }
            while (!done);    
    
            ssize_t read;
    
            auto status = 
                _ctx->conn()->read(buf, len, read);
            switch (status)
            {
            case connection::CONNECTION_ERROR:
            {
                LOG_DEBUG_ERRNO("Http client timeout or socket read error",
                               _ctx->conn()->get_last_error());
                throw http_exception("read error");
            }
            break;
            case connection::CONNECTION_CLOSED:
            {
                LOG_DEBUG("Http client disconnected unexpectedly");
                throw http_exception("connection closed");
            }
            break;
            case connection::CONNECTION_OK:
            {
                if (read == 0)
                {
                    LOG_DEBUG("Http client disconnected unexpectedly");
                    throw http_exception("connection closed");
                }
                else
                {
                    return read;
                }
                break;
            }
            case connection::CONNECTION_WANTS_READ:
            {
                reading = true;
            }
            break;
            case connection::CONNECTION_WANTS_WRITE:
            {
                reading = false;
            }
            break;
            default:
                FATAL("Unknown connection state: " << _ctx->conn()->get_read_status());
                break;
            }
        }
    }
}
