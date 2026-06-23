#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace minerva
{

    // Deterministic payload helpers shared in spirit with the basher client.
    // Both sides generate body bytes from (seed, index) so that payloads can be
    // regenerated and verified without transferring an expected copy.
    namespace test_payload
    {
        // FNV-1a 64 bit. Used so the client and server agree on a body checksum.
        inline uint64_t checksum(const char * data, size_t len)
        {
            uint64_t h = 1469598103934665603ULL;
            for (size_t i = 0; i < len; ++i)
            {
                h ^= static_cast<unsigned char>(data[i]);
                h *= 1099511628211ULL;
            }
            return h;
        }

        // Deterministic byte for a given seed and position.
        inline char byte_at(uint32_t seed, size_t index)
        {
            return static_cast<char>((seed + static_cast<uint32_t>(index)) & 0xFF);
        }

        // Generate a deterministic body of the requested length.
        inline std::string generate(uint32_t seed, size_t len)
        {
            std::string s;
            s.resize(len);
            for (size_t i = 0; i < len; ++i)
            {
                s[i] = byte_at(seed, i);
            }
            return s;
        }
    }
}
