#include "guid.h"

#include <uuid/uuid.h>
#include <string>

namespace minerva
{
    namespace
    {
        constexpr size_t UUID_TEXT_BUFLEN = 37;  // 36 chars + NUL
    }

    std::string new_guid()
    {
        uuid_t uuid;
        uuid_generate_random(uuid);     // v4 UUID; libuuid handles thread safety

        char s[UUID_TEXT_BUFLEN];
        uuid_unparse_lower(uuid, s);
        return std::string(s);
    }
}
