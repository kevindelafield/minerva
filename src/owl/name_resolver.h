#pragma once

#include <cstring>
#include <sys/socket.h>

namespace owl
{

    class name_resolver
    {
    public:
    name_resolver() : addr_len(-1), family(-1), socktype(-1), protocol(-1)
        {
            std::memset(&addr, 0, sizeof(addr));
        }
    
    name_resolver(const name_resolver& copy) : 
        addr_len(copy.addr_len),
            family(copy.family), socktype(copy.socktype), protocol(copy.protocol)
        {
            memcpy(&addr, &copy.addr, sizeof(addr));
        }

        name_resolver & operator=(const name_resolver& other)
            {
                if (this != &other)
                {
                    addr = other.addr;
                    addr_len = other.addr_len;
                    family = other.family;
                    socktype = other.socktype;
                    protocol = other.protocol;
                }
                return *this;
            }
        virtual ~name_resolver() = default;

        static int resolve(const char* server, const char* port,
                           bool ipv4, bool ipv6,
                           name_resolver& helper);

        std::string name;
        struct sockaddr addr;
        socklen_t addr_len;
        int family;
        int socktype;
        int protocol;
    };
}
