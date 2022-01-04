#include "guid.h"

#include <uuid/uuid.h>

namespace util
{
    std::string new_guid()
    {
        uuid_t uuid;
        uuid_generate ( uuid );
        char s[37];
        uuid_unparse ( uuid, s );
        return s;
    }
}
