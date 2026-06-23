#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace minerva
{

    // A minimal raw-socket HTTP/1.1 client.  It deliberately does NOT build
    // requests itself: callers send fully-formed request bytes via send_all so
    // that the basher can control framing (chunked vs content-length), produce
    // malformed requests, and reuse connections across requests.  The client
    // only handles connecting and parsing a single response (both chunked and
    // content-length framed).
    class http_client
    {
    public:
        struct response
        {
            int status_code = 0;
            std::map<std::string, std::string> headers; // lower-cased keys
            std::string body;
            bool keep_alive = true;
        };

        http_client(const std::string & host, int port, int timeout_ms);
        ~http_client();

        http_client(const http_client &) = delete;
        http_client & operator=(const http_client &) = delete;

        bool open();
        void close();
        bool is_open() const { return m_fd >= 0; }

        // Send raw request bytes. Returns false on transport error.
        bool send_all(const char * data, size_t len);

        // Half-close the write side so the peer observes EOF. Used by fault
        // injection to signal a truncated/abandoned request body.
        void shutdown_write();

        // Parse a full HTTP response. Returns false if the connection closed or
        // errored before a complete response was received.
        bool read_response(response & r);

    private:
        bool recv_more();
        bool read_line(std::string & line);
        bool read_n(std::string & out, size_t n);

        std::string m_host;
        int m_port;
        int m_timeout_ms;
        int m_fd = -1;
        std::string m_inbuf;
        size_t m_pos = 0;
    };
}
