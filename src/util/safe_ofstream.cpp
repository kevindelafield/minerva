#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ext/stdio_filebuf.h>   // libstdc++ extension
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "log.h"
#include "file_utils.h"
#include "safe_ofstream.h"

namespace minerva
{
    struct safe_ofstream::impl
    {
        // filebuf adopts the fd; ostream writes through filebuf. We keep the
        // fd separately so we can fsync it before close.
        std::unique_ptr<__gnu_cxx::stdio_filebuf<char>> filebuf;
        std::unique_ptr<std::ostream>                   ostream;
    };

    namespace
    {
        // Split path into "dir/" and "basename". For paths without '/',
        // dir is "." (with no trailing slash) and base is the whole input.
        void split_path(const std::string& path,
                        std::string& dir, std::string& base)
        {
            const auto pos = path.find_last_of('/');
            if (pos == std::string::npos)
            {
                dir  = ".";
                base = path;
            }
            else
            {
                dir  = path.substr(0, pos);
                if (dir.empty()) dir = "/";
                base = path.substr(pos + 1);
            }
        }

        // Create the temp file via mkstemp(3): O_CREAT|O_EXCL|O_RDWR, mode 0600,
        // template ends in XXXXXX. Returns fd >= 0 on success, -1 on failure
        // (errno set). Out-param @p path receives the actual path used.
        int create_temp(const std::string& dir,
                        const std::string& base,
                        std::string& path)
        {
            std::string templ = dir + "/." + base + ".tmp.XXXXXX";
            std::vector<char> buf(templ.begin(), templ.end());
            buf.push_back('\0');
            const int fd = mkstemp(buf.data());
            if (fd >= 0)
            {
                path.assign(buf.data());
            }
            return fd;
        }

        // fsync a path (used for the parent directory after rename).
        bool fsync_path(const std::string& path)
        {
            const int dfd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
            if (dfd < 0) return false;
            const int rc = ::fsync(dfd);
            const int saved = errno;
            ::close(dfd);
            if (rc != 0) errno = saved;
            return rc == 0;
        }
    }

    safe_ofstream::safe_ofstream(const std::string& filename)
        : m_impl(new impl()), m_realpath(filename)
    {
        if (filename.empty())
        {
            throw std::invalid_argument("Filename cannot be empty");
        }
        if (!is_path_safe(filename))
        {
            throw std::invalid_argument("Unsafe file path: " + filename);
        }

        std::string dir, base;
        split_path(filename, dir, base);

        m_fd = create_temp(dir, base, m_fakepath);
        if (m_fd < 0)
        {
            const int err = errno;
            LOG_ERROR_ERRNO("safe_ofstream: failed to create temp in dir "
                            << dir << " for " << filename, err);
            throw std::runtime_error("Failed to create temporary file in: " + dir);
        }

        // If the target exists, copy its mode/owner onto the temp so an
        // overwrite preserves the original's permissions.
        struct stat st{};
        if (::stat(m_realpath.c_str(), &st) == 0)
        {
            if (::fchmod(m_fd, st.st_mode & 07777) != 0)
            {
                LOG_WARN_ERRNO("safe_ofstream: fchmod(" << m_fakepath
                               << ") to match target failed", errno);
            }
            // fchown may fail (EPERM) when not root; that is expected and
            // not fatal -- we just keep the process's uid/gid.
            if (::fchown(m_fd, st.st_uid, st.st_gid) != 0 &&
                errno != EPERM)
            {
                LOG_WARN_ERRNO("safe_ofstream: fchown(" << m_fakepath
                               << ") to match target failed", errno);
            }
        }

        try
        {
            m_impl->filebuf.reset(new __gnu_cxx::stdio_filebuf<char>(
                m_fd, std::ios_base::out | std::ios_base::binary));
            m_impl->ostream.reset(new std::ostream(m_impl->filebuf.get()));
        }
        catch (...)
        {
            cleanup_temp();
            throw;
        }
        m_open = true;
    }

    safe_ofstream::~safe_ofstream()
    {
        if (m_open && !m_committed)
        {
            cleanup_temp();
        }
    }

    safe_ofstream::safe_ofstream(safe_ofstream&& other) noexcept
        : m_impl(std::move(other.m_impl)),
          m_fakepath(std::move(other.m_fakepath)),
          m_realpath(std::move(other.m_realpath)),
          m_fd(other.m_fd),
          m_open(other.m_open),
          m_committed(other.m_committed)
    {
        other.m_fd        = -1;
        other.m_open      = false;
        other.m_committed = true; // suppress destructor cleanup
    }

    safe_ofstream& safe_ofstream::operator=(safe_ofstream&& other) noexcept
    {
        if (this != &other)
        {
            if (m_open && !m_committed)
            {
                cleanup_temp();
            }
            m_impl      = std::move(other.m_impl);
            m_fakepath  = std::move(other.m_fakepath);
            m_realpath  = std::move(other.m_realpath);
            m_fd        = other.m_fd;
            m_open      = other.m_open;
            m_committed = other.m_committed;

            other.m_fd        = -1;
            other.m_open      = false;
            other.m_committed = true;
        }
        return *this;
    }

    std::ostream& safe_ofstream::stream()
    {
        // After a moved-from / closed state, return a fail-state stream so
        // operator<< chains don't crash.
        static std::ostream null_stream(nullptr);
        return m_impl && m_impl->ostream ? *m_impl->ostream : null_stream;
    }

    bool safe_ofstream::fail() const
    {
        return !m_impl || !m_impl->ostream || m_impl->ostream->fail();
    }

    bool safe_ofstream::bad() const
    {
        return !m_impl || !m_impl->ostream || m_impl->ostream->bad();
    }

    void safe_ofstream::cleanup_temp() noexcept
    {
        // Tear down the ostream/filebuf first; the filebuf will close the fd
        // it adopted via stdio_filebuf.
        if (m_impl)
        {
            m_impl->ostream.reset();
            m_impl->filebuf.reset();
        }
        m_fd = -1;
        m_open = false;
        if (!m_fakepath.empty())
        {
            if (::unlink(m_fakepath.c_str()) != 0 && errno != ENOENT)
            {
                LOG_ERROR_ERRNO("safe_ofstream: failed to unlink temp file: "
                                << m_fakepath, errno);
            }
        }
    }

    bool safe_ofstream::commit()
    {
        if (m_committed) return true;
        if (!m_open || !m_impl || !m_impl->ostream)
        {
            LOG_ERROR("safe_ofstream::commit: stream not open");
            return false;
        }

        // Flush user-level buffer; check error state *after* the flush so
        // failures that surface at flush time aren't missed.
        m_impl->ostream->flush();
        if (m_impl->ostream->fail() || m_impl->ostream->bad())
        {
            LOG_ERROR("safe_ofstream::commit: stream in error state for "
                      << m_fakepath);
            return false;
        }

        // fsync the data file *before* closing the fd / renaming.
        if (m_fd >= 0 && ::fsync(m_fd) != 0)
        {
            LOG_ERROR_ERRNO("safe_ofstream::commit: fsync failed for "
                            << m_fakepath, errno);
            return false;
        }

        // Tear down the ostream/filebuf (closes the fd).
        m_impl->ostream.reset();
        const bool filebuf_ok = m_impl->filebuf
            ? (m_impl->filebuf->close() != nullptr)
            : true;
        m_impl->filebuf.reset();
        m_fd = -1;
        if (!filebuf_ok)
        {
            LOG_ERROR("safe_ofstream::commit: close failed for " << m_fakepath);
            return false;
        }

        // Atomic rename onto the target.
        if (::rename(m_fakepath.c_str(), m_realpath.c_str()) != 0)
        {
            LOG_ERROR_ERRNO("safe_ofstream::commit: rename(" << m_fakepath
                            << " -> " << m_realpath << ") failed", errno);
            return false;
        }

        // fsync the parent directory so the rename itself is durable.
        std::string dir, base;
        split_path(m_realpath, dir, base);
        if (!fsync_path(dir))
        {
            // Best-effort: log, but the rename already happened. Treat as a
            // soft failure so callers can decide whether to retry.
            LOG_WARN_ERRNO("safe_ofstream::commit: fsync(dir " << dir
                           << ") failed", errno);
        }

        m_committed = true;
        m_open      = false;
        m_fakepath.clear();
        return true;
    }
}
