#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <iostream>
#include <cassert>
#include "name_resolver.h"
#include "log.h"

namespace owl
{

    int name_resolver::resolve(const char* server, const char* port,
                               bool ipv4, bool ipv6,
                               name_resolver& helper)
    {
        helper.name = server;

        struct in6_addr serveraddr;

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        // is it a numeric address?
        int rc = inet_pton(AF_INET, server, &serveraddr);
        if (rc == 1)
        {
            hints.ai_family = AF_INET;
            hints.ai_flags |= AI_NUMERICHOST;
        }
        else
        {
            rc = inet_pton(AF_INET6, server, &serveraddr);
            if (rc == 1)
            {
                hints.ai_family = AF_INET6;
                hints.ai_flags |= AI_NUMERICHOST;
            }
        }

        // get addr info
        int status = getaddrinfo(server, port, &hints, &res);
        int tries = 0;
        while ((status == EAI_AGAIN || (status == EAI_SYSTEM && errno == EINTR)) &&
               tries < 3)
        {
            if (status == EAI_AGAIN)
            {
                tries++;
            }
            status = getaddrinfo(server, port, &hints, &res);
        }

        if (status)
        {
            LOG_DEBUG("get addr info error: " << status << " " <<
                      gai_strerror(status));
            return status;
        }
    
        bool found = false;
        if (res)
        {
            struct addrinfo *res_original = res;
            while (res && !found)
            {
                switch (res->ai_family)
                {
                case AF_INET6:
                {
                    if (ipv6)
                    {
                        helper.addr = *(res->ai_addr);
                        helper.addr_len = res->ai_addrlen;
                        helper.family = res->ai_family;
                        helper.protocol = res->ai_protocol;
                        helper.socktype = res->ai_socktype;
                    
                        found = true;
                    }
                }
                break;
                case AF_INET:
                {
                    if (ipv4)
                    {
                        helper.addr = *(res->ai_addr);
                        helper.addr_len = res->ai_addrlen;
                        helper.family = res->ai_family;
                        helper.protocol = res->ai_protocol;
                        helper.socktype = res->ai_socktype;
                    
                        found = true;
                    }
                }
                break;
                default:
                    LOG_DEBUG("invalid address family");
                    break;
                }
                res = res->ai_next;
            }
            freeaddrinfo(res_original);
        }

        if (!found)
        {
            LOG_DEBUG("no connect res");
            return EAI_NODATA;
        }
        return 0;
    }
}
