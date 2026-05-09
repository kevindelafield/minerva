#pragma once

#include <sys/socket.h>
#include <string>

namespace minerva
{
    /**
     * Resolves a hostname/port to a socket address suitable for connect(2).
     *
     * The class is a value-semantic bag of resolved fields. Use resolve()
     * to populate it.
     */
    class name_resolver
    {
    public:
        name_resolver();
        name_resolver(const name_resolver&)            = default;
        name_resolver(name_resolver&&)                 = default;
        name_resolver& operator=(const name_resolver&) = default;
        name_resolver& operator=(name_resolver&&)      = default;
        ~name_resolver()                               = default;

        /**
         * Resolve @p server / @p port to a single endpoint, written into @p out.
         *
         * @param server   Hostname or numeric address. Must not be null.
         * @param port     Port string ("80", "https", ...). Must not be null.
         * @param ipv4     Accept AF_INET results.
         * @param ipv6     Accept AF_INET6 results.
         * @param out      Output. On success, contains a usable address.
         * @return 0 on success, otherwise a getaddrinfo() EAI_* code
         *         (EAI_NONAME if no result matched the family filter,
         *         EAI_SYSTEM with errno=EINVAL if @p server / @p port is null).
         */
        static int resolve(const char* server, const char* port,
                           bool ipv4, bool ipv6,
                           name_resolver& out);

        // family == -1 means "unset" (resolve() has not populated this object).
        std::string             name;
        struct sockaddr_storage addr;
        socklen_t               addr_len;
        int                     family;
        int                     socktype;
        int                     protocol;
    };
}
