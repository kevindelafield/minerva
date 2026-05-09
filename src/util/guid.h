#pragma once

#include <string>

namespace minerva
{
    /**
     * Generates a new Version 4 (random) UUID string in canonical form
     * (lowercase, with hyphens).
     *
     * Uses libuuid's uuid_generate_random(), which draws from /dev/urandom
     * when available.
     *
     * Format:  xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
     *          (x = hex, y = one of 8/9/a/b)
     *
     * Thread-safe.  May throw std::bad_alloc.
     */
    std::string new_guid();
}
