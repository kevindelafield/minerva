#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>
#include "http_context.h"

namespace minerva
{
    bool http_context::get_server_ip(std::string & ip)
    {
        struct sockaddr_storage out_addr;
        socklen_t out_addr_len = sizeof(out_addr);
        std::memset(&out_addr, 0, sizeof(out_addr));
        if (!m_conn->get_local_addr(m_client_addr, m_client_addr_len,
                                    out_addr, out_addr_len))
        {
            LOG_ERROR_ERRNO("failed to get local address of socket", errno);
            return false;
        }

        char buf[INET6_ADDRSTRLEN] = {0};
        const char * name = nullptr;
        if (out_addr.ss_family == AF_INET)
        {
            const auto * a = reinterpret_cast<const sockaddr_in *>(&out_addr);
            name = ::inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
        }
        else if (out_addr.ss_family == AF_INET6)
        {
            const auto * a = reinterpret_cast<const sockaddr_in6 *>(&out_addr);
            name = ::inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
        }
        else
        {
            LOG_ERROR("unsupported address family: " << out_addr.ss_family);
            return false;
        }
        if (!name)
        {
            LOG_ERROR_ERRNO("inet_ntop failed", errno);
            return false;
        }
        ip = name;
        return true;
    }
}
