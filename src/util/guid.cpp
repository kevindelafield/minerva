#include "guid.h"

#include <uuid/uuid.h>
#include <cstring>
#include <cstdio>

namespace minerva
{
    std::string new_guid()
    {
        uuid_t uuid;
        uuid_generate_random(uuid);  // Explicitly use random (v4) UUID
        char s[37];
        uuid_unparse_lower(uuid, s);  // Use lowercase for consistency
        return std::string(s);
    }

    std::string new_time_guid()
    {
        uuid_t uuid;
        uuid_generate_time(uuid);  // Explicitly use time-based (v1) UUID
        char s[37];
        uuid_unparse_lower(uuid, s);  // Use lowercase for consistency
        return std::string(s);
    }

    bool is_valid_guid(const std::string& uuid_str)
    {
        if (uuid_str.length() != 36) {
            return false;
        }

        uuid_t uuid;
        return uuid_parse(uuid_str.c_str(), uuid) == 0;
    }

    std::string new_compact_guid()
    {
        uuid_t uuid;
        uuid_generate_random(uuid);
        char s[33];  // 32 chars + null terminator
        
        // Format as compact hex string
        snprintf(s, sizeof(s), 
                "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
        
        return std::string(s);
    }

    std::string guid_to_compact(const std::string& guid_with_hyphens)
    {
        if (!is_valid_guid(guid_with_hyphens)) {
            return "";
        }

        std::string result;
        result.reserve(32);
        
        for (char c : guid_with_hyphens) {
            if (c != '-') {
                result += c;
            }
        }
        
        return result;
    }

    std::string compact_to_guid(const std::string& compact_guid)
    {
        if (compact_guid.length() != 32) {
            return "";
        }

        // Validate all characters are hex
        for (char c : compact_guid) {
            if (!((c >= '0' && c <= '9') || 
                  (c >= 'a' && c <= 'f') || 
                  (c >= 'A' && c <= 'F'))) {
                return "";
            }
        }

        // Insert hyphens at correct positions: 8-4-4-4-12
        std::string result;
        result.reserve(36);
        
        result += compact_guid.substr(0, 8);
        result += '-';
        result += compact_guid.substr(8, 4);
        result += '-';
        result += compact_guid.substr(12, 4);
        result += '-';
        result += compact_guid.substr(16, 4);
        result += '-';
        result += compact_guid.substr(20, 12);
        
        return result;
    }
}
