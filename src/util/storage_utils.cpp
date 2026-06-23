#include <sys/statvfs.h>
#include "storage_utils.h"

namespace minerva
{
    std::optional<std::uint64_t>
    get_free_bytes_for_path(const std::string& path) noexcept
    {
        if (path.empty())
        {
            return std::nullopt;
        }

        struct statvfs st;
        if (statvfs(path.c_str(), &st) != 0)
        {
            return std::nullopt;
        }

        // f_frsize is the fundamental block size; f_bavail is counted in
        // those units. f_bsize is the *preferred I/O* block size and would
        // be wrong on filesystems where the two differ.
        return static_cast<std::uint64_t>(st.f_frsize) *
               static_cast<std::uint64_t>(st.f_bavail);
    }
}
