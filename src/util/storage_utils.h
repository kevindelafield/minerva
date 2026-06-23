#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace minerva
{
    /**
     * Returns the number of bytes available to an unprivileged user on the
     * filesystem containing @p path (i.e. statvfs(2)'s f_frsize * f_bavail,
     * which excludes root-reserved space).
     *
     * Returns std::nullopt on failure (errno set by statvfs(2): typical
     * causes include ENOENT, EACCES, ELOOP, ENAMETOOLONG, EOVERFLOW). The
     * function does not log; callers that want diagnostics should inspect
     * errno themselves.
     */
    std::optional<std::uint64_t> get_free_bytes_for_path(const std::string& path) noexcept;
}
