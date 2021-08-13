#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <mutex>
#include "http_context.h"

namespace httpd
{
    static std::mutex _lock;

    bool http_context::get_server_ip(std::string & ip)
    {
        struct sockaddr_in out_addr;
        socklen_t out_addr_len = sizeof(out_addr);
        memset(&out_addr, 0, sizeof(out_addr));
        if (!_conn->get_local_addr(m_client_addr, m_client_addr_len, 
                                   out_addr, out_addr_len))
        {
            LOG_ERROR_ERRNO("failed to get remote address of socket", errno);
            return false;
        }

        std::unique_lock<std::mutex> lk(_lock);

        char * name = inet_ntoa(out_addr.sin_addr);
        if (name)
        {
            ip = name;
            return true;
        }
        return false;
    }
}
