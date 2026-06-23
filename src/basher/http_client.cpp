#include <cctype>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "http_client.h"

namespace minerva
{

    static std::string to_lower(std::string s)
    {
        for (char & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static std::string trim(const std::string & s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    http_client::http_client(const std::string & host, int port, int timeout_ms)
        : m_host(host), m_port(port), m_timeout_ms(timeout_ms)
    {
    }

    http_client::~http_client()
    {
        close();
    }

    bool http_client::open()
    {
        close();

        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        std::string portstr = std::to_string(m_port);
        struct addrinfo * res = nullptr;
        if (getaddrinfo(m_host.c_str(), portstr.c_str(), &hints, &res) != 0)
        {
            return false;
        }

        int fd = -1;
        for (struct addrinfo * p = res; p != nullptr; p = p->ai_next)
        {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0)
            {
                continue;
            }
            if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            {
                break;
            }
            ::close(fd);
            fd = -1;
        }
        freeaddrinfo(res);

        if (fd < 0)
        {
            return false;
        }

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (m_timeout_ms > 0)
        {
            struct timeval tv;
            tv.tv_sec = m_timeout_ms / 1000;
            tv.tv_usec = (m_timeout_ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        m_fd = fd;
        m_inbuf.clear();
        m_pos = 0;
        return true;
    }

    void http_client::close()
    {
        if (m_fd >= 0)
        {
            ::close(m_fd);
            m_fd = -1;
        }
        m_inbuf.clear();
        m_pos = 0;
    }

    bool http_client::send_all(const char * data, size_t len)
    {
        size_t off = 0;
        while (off < len)
        {
            ssize_t n = ::send(m_fd, data + off, len - off, MSG_NOSIGNAL);
            if (n <= 0)
            {
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }

    void http_client::shutdown_write()
    {
        if (m_fd >= 0)
        {
            ::shutdown(m_fd, SHUT_WR);
        }
    }

    bool http_client::recv_more()
    {
        char buf[16384];
        ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            return false;
        }
        m_inbuf.append(buf, static_cast<size_t>(n));
        return true;
    }

    bool http_client::read_line(std::string & line)
    {
        while (true)
        {
            size_t nl = m_inbuf.find("\r\n", m_pos);
            if (nl != std::string::npos)
            {
                line = m_inbuf.substr(m_pos, nl - m_pos);
                m_pos = nl + 2;
                return true;
            }
            if (!recv_more())
            {
                return false;
            }
        }
    }

    bool http_client::read_n(std::string & out, size_t n)
    {
        while (m_inbuf.size() - m_pos < n)
        {
            if (!recv_more())
            {
                return false;
            }
        }
        out.append(m_inbuf, m_pos, n);
        m_pos += n;
        return true;
    }

    bool http_client::read_response(response & r)
    {
        // Compact the buffer so it does not grow without bound across reuse.
        if (m_pos > 0)
        {
            m_inbuf.erase(0, m_pos);
            m_pos = 0;
        }

        std::string status_line;
        if (!read_line(status_line))
        {
            return false;
        }

        // Parse "HTTP/1.1 <code> <reason>".
        size_t sp = status_line.find(' ');
        if (sp == std::string::npos)
        {
            return false;
        }
        size_t sp2 = status_line.find(' ', sp + 1);
        std::string code = status_line.substr(
            sp + 1, (sp2 == std::string::npos ? status_line.size() : sp2) - (sp + 1));
        r.status_code = std::atoi(code.c_str());

        long long content_length = -1;
        bool chunked = false;
        r.keep_alive = true;

        std::string line;
        while (true)
        {
            if (!read_line(line))
            {
                return false;
            }
            if (line.empty())
            {
                break;
            }
            size_t c = line.find(':');
            if (c == std::string::npos)
            {
                continue;
            }
            std::string k = to_lower(trim(line.substr(0, c)));
            std::string v = trim(line.substr(c + 1));
            r.headers[k] = v;
            if (k == "content-length")
            {
                content_length = std::atoll(v.c_str());
            }
            else if (k == "transfer-encoding" &&
                     to_lower(v).find("chunked") != std::string::npos)
            {
                chunked = true;
            }
            else if (k == "connection" &&
                     to_lower(v).find("close") != std::string::npos)
            {
                r.keep_alive = false;
            }
        }

        if (chunked)
        {
            while (true)
            {
                std::string szline;
                if (!read_line(szline))
                {
                    return false;
                }
                size_t semi = szline.find(';');
                if (semi != std::string::npos)
                {
                    szline = szline.substr(0, semi);
                }
                unsigned long csize = std::strtoul(szline.c_str(), nullptr, 16);
                if (csize == 0)
                {
                    // Final chunk; consume the terminating CRLF (ignore trailers).
                    std::string tmp;
                    read_line(tmp);
                    break;
                }
                if (!read_n(r.body, csize))
                {
                    return false;
                }
                std::string crlf;
                if (!read_n(crlf, 2))
                {
                    return false;
                }
            }
        }
        else if (content_length >= 0)
        {
            if (content_length > 0)
            {
                if (!read_n(r.body, static_cast<size_t>(content_length)))
                {
                    return false;
                }
            }
        }
        else
        {
            // No framing: read until the server closes the connection.
            while (recv_more())
            {
            }
            r.body.append(m_inbuf, m_pos, m_inbuf.size() - m_pos);
            m_pos = m_inbuf.size();
            r.keep_alive = false;
        }

        return true;
    }
}
