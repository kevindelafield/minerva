#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "name_resolver.h"
#include "log.h"

namespace minerva
{
    name_resolver::name_resolver()
        : addr_len(0), family(-1), socktype(-1), protocol(-1)
    {
        memset(&addr, 0, sizeof(addr));
    }

    namespace
    {
        // Copy a single addrinfo into the output object.
        void store(name_resolver& out, const struct addrinfo* res)
        {
            // Bounds check: the result list must fit in sockaddr_storage. It
            // always does for AF_INET/AF_INET6, but assert defensively.
            socklen_t len = res->ai_addrlen;
            if (len > static_cast<socklen_t>(sizeof(out.addr)))
            {
                len = sizeof(out.addr);
            }
            memset(&out.addr, 0, sizeof(out.addr));
            memcpy(&out.addr, res->ai_addr, len);
            out.addr_len = len;
            out.family   = res->ai_family;
            out.socktype = res->ai_socktype;
            out.protocol = res->ai_protocol;
        }
    }

    int name_resolver::resolve(const char* server, const char* port,
                               bool ipv4, bool ipv6,
                               name_resolver& out)
    {
        if (!server || !port)
        {
            errno = EINVAL;
            LOG_DEBUG("name_resolver::resolve: null server or port");
            return EAI_SYSTEM;
        }
        if (!ipv4 && !ipv6)
        {
            LOG_DEBUG("name_resolver::resolve: both ipv4 and ipv6 disabled");
            return EAI_NONAME;
        }

        out.name = server;

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        // Probe whether the input is a numeric address; if so, skip DNS.
        struct in6_addr probe;
        if (inet_pton(AF_INET, server, &probe) == 1)
        {
            hints.ai_family = AF_INET;
            hints.ai_flags |= AI_NUMERICHOST;
        }
        else if (inet_pton(AF_INET6, server, &probe) == 1)
        {
            hints.ai_family = AF_INET6;
            hints.ai_flags |= AI_NUMERICHOST;
        }

        // Retry transient failures with a short backoff.
        struct addrinfo* res = nullptr;
        int status = 0;
        const int max_tries = 3;
        for (int tries = 0; tries < max_tries; ++tries)
        {
            status = getaddrinfo(server, port, &hints, &res);
            if (status == 0)
            {
                break;
            }
            const bool transient =
                (status == EAI_AGAIN) ||
                (status == EAI_SYSTEM && errno == EINTR);
            if (!transient)
            {
                break;
            }
            // 50ms backoff between attempts.
            struct timespec ts = { 0, 50 * 1000 * 1000 };
            nanosleep(&ts, nullptr);
        }

        if (status != 0)
        {
            LOG_DEBUG("getaddrinfo(" << server << ":" << port << ") failed: "
                      << status << " " << gai_strerror(status));
            return status;
        }

        // Two-pass selection: prefer IPv6 if requested, then fall back to IPv4.
        // (Within a family, take the first result, matching glibc/gai.conf
        // ordering.)
        bool found = false;
        if (ipv6)
        {
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next)
            {
                if (p->ai_family == AF_INET6)
                {
                    store(out, p);
                    found = true;
                    break;
                }
            }
        }
        if (!found && ipv4)
        {
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next)
            {
                if (p->ai_family == AF_INET)
                {
                    store(out, p);
                    found = true;
                    break;
                }
            }
        }

        if (res)
        {
            freeaddrinfo(res);
        }

        if (!found)
        {
            LOG_DEBUG("getaddrinfo(" << server << ":" << port
                      << "): no result matched family filter (ipv4="
                      << ipv4 << " ipv6=" << ipv6 << ")");
            return EAI_NONAME;
        }
        return 0;
    }
}
