#pragma once

#include <string>

namespace minerva
{
    /**
     * Generates a new Version 4 (random) UUID/GUID string.
     * 
     * Uses the system's cryptographically secure random number generator
     * via libuuid to create a properly formatted UUID string.
     * 
     * @return A string in the format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     *         where x is any hexadecimal digit and y is one of 8, 9, A, or B
     * 
     * Example: "550e8400-e29b-41d4-a716-446655440000"
     * 
     * Thread-safe: Yes, libuuid handles thread safety internally
     */
    std::string new_guid();

    /**
     * Generates a new Version 1 (time-based) UUID/GUID string.
     * 
     * Creates a UUID based on the current timestamp and MAC address.
     * Note: May leak some system information (MAC address, timestamp).
     * 
     * @return A string in standard UUID format
     */
    std::string new_time_guid();

    /**
     * Validates whether a string is a properly formatted UUID.
     * 
     * @param uuid_str The string to validate
     * @return true if the string is a valid UUID format, false otherwise
     */
    bool is_valid_guid(const std::string& uuid_str);

    /**
     * Generates a compact GUID string without hyphens.
     * 
     * @return A 32-character hexadecimal string (no hyphens)
     * Example: "550e8400e29b41d4a716446655440000"
     */
    std::string new_compact_guid();

    /**
     * Converts a standard GUID to compact format (removes hyphens).
     * 
     * @param guid_with_hyphens Standard GUID format with hyphens
     * @return Compact GUID without hyphens, or empty string if invalid
     */
    std::string guid_to_compact(const std::string& guid_with_hyphens);

    /**
     * Converts a compact GUID to standard format (adds hyphens).
     * 
     * @param compact_guid 32-character compact GUID
     * @return Standard GUID format with hyphens, or empty string if invalid
     */
    std::string compact_to_guid(const std::string& compact_guid);
}
