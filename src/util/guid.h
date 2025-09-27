#pragma once

#include <string>
#include <optional>
#include <system_error>

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
     * 
     * @throws std::runtime_error if entropy source is unavailable
     */
    std::string new_guid();

    /**
     * Generates a new strong Version 4 UUID with enhanced entropy verification.
     * 
     * This function performs additional checks on entropy sources and provides
     * better error reporting than the standard version.
     * 
     * @return Optional containing UUID string on success, nullopt on failure
     */
    std::optional<std::string> new_strong_guid() noexcept;

    /**
     * Verifies that cryptographic entropy sources are available.
     * 
     * @return true if /dev/urandom is accessible and working
     */
    bool verify_entropy_source() noexcept;

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
     * 
     * @throws std::runtime_error if entropy source is unavailable
     */
    std::string new_compact_guid();

    /**
     * Generates a strong compact GUID with enhanced entropy verification.
     * 
     * @return Optional containing compact UUID string on success, nullopt on failure
     */
    std::optional<std::string> new_strong_compact_guid() noexcept;

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
