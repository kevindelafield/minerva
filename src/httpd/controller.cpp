#include <string>
#include <istream>
#include <fstream>
#include <cstdio>
#include <util/string_utils.h>
#include <util/safe_ofstream.h>
#include "controller.h"
#include "http_context.h"

namespace minerva
{
    // 100 MiB default cap. Override via controller::max_send_file_size().
    size_t controller::s_max_send_file_size = 100 * 1024 * 1024;

    // Threshold above which send_file() switches to chunked transfer
    // encoding rather than buffering the whole file in m_response_stream.
    static constexpr size_t SEND_FILE_CHUNK_THRESHOLD = 1 * 1024 * 1024; // 1 MiB
    static constexpr size_t SEND_FILE_FLUSH_INTERVAL  = 256 * 1024;      // 256 KiB

    /**
     * Reject filenames that are unsafe for use with the local filesystem.
     *
     * Rejects:
     *   - empty strings
     *   - embedded NUL / CR / LF
     *   - absolute paths (anything beginning with '/')
     *   - any path containing a "." or ".." segment
     *
     * Note: this is a syntactic check only. Callers that accept user-supplied
     * path components should additionally canonicalize the result (e.g. with
     * realpath(3)) and verify it is within an allowed base directory before
     * touching the filesystem. Symlinks within the base directory can still
     * escape it.
     */
    static bool is_unsafe_filename(const std::string & filename)
    {
        if (filename.empty())
        {
            return true;
        }
        if (filename.find_first_of(std::string("\0\r\n", 3)) != std::string::npos)
        {
            return true;
        }
        if (filename.front() == '/')
        {
            return true;
        }
        size_t pos = 0;
        while (pos < filename.size())
        {
            size_t end = filename.find('/', pos);
            if (end == std::string::npos) end = filename.size();
            std::string seg = filename.substr(pos, end - pos);
            if (seg == "." || seg == "..")
            {
                return true;
            }
            pos = end + 1;
        }
        return false;
    }

    void controller::handle_request(http_context & ctx,
                                    const std::string & operation)
    {
        auto it = m_handlers.find(operation);
        if (it == m_handlers.end())
        {
            LOG_DEBUG("didn't find handler: " << operation);
            ctx.response().status_code_not_found();
            return;
        }
        it->second(ctx);
    }


    void controller::register_handler(const std::string & name,
                                      std::function<void(http_context &)> func)
    {
        auto it = m_handlers.find(name);
        if (it != m_handlers.end())
        {
            LOG_WARN("controller: replacing existing handler for '"
                     << name << "'");
        }
        m_handlers[name] = std::move(func);
    }

    bool controller::next_path_segment(std::istream & is,
                                       std::string & next)
    {
        if (is.peek() == EOF)
        {
            next = "";
            return false;
        }
        while (is.peek() == '/')
        {
            is.get();
        }
        if (is.peek() == EOF)
        {
            next = "";
            return false;
        }
        std::getline(is, next, '/');
        return true;
    }

    bool controller::save_to_file(const std::string & filename,
                                  http_context & ctx)
    {
        if (is_unsafe_filename(filename))
        {
            LOG_ERROR("Invalid or unsafe filename: " << filename);
            ctx.response().status_code_bad_request();
            return false;
        }

        // Atomic write: stage to a sibling temp file, fsync, rename, fsync
        // parent. On failure (or if commit() is never called) the temp file
        // is unlinked by safe_ofstream's destructor; the destination is
        // left untouched.
        safe_ofstream os(filename);
        if (!os.is_open())
        {
            LOG_ERROR("Failed to open file for write: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        bool success = true;

        try
        {
            char buf[128 * 1024];
            size_t read = 0;
            do
            {
                // Pass 0 to defer to the per-context aggregate timeout
                // (http_context::timed_out()), preventing slow-loris uploads
                // from holding a worker thread indefinitely.
                read = ctx.request().read(buf, sizeof(buf), 0);
                if (read > 0)
                {
                    os.stream().write(buf, read);
                    if (os.fail())
                    {
                        LOG_ERROR("Failed to write to file: " << filename);
                        success = false;
                        break;
                    }
                }
            }
            while (read > 0);
        }
        catch (const std::exception & e)
        {
            LOG_ERROR("Exception during file write: " << e.what());
            success = false;
        }

        if (!success)
        {
            // safe_ofstream's destructor will remove the temp file.
            ctx.response().status_code_internal_error();
            return false;
        }

        if (!os.commit())
        {
            LOG_ERROR("Failed to commit file: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        ctx.response().status_code_no_content();
        return true;
    }


    bool controller::send_file(const std::string & filename,
                               http_context & ctx)
    {
        http_content_type::code ct = http_content_type::code::CONTENT_TYPE_UNKNOWN;

        size_t index = filename.find_last_of(".");
        if (index != std::string::npos)
        {
            std::string ext = filename.substr(index+1);
            if (minerva::ci_equals(ext, "jpg") || minerva::ci_equals(ext, "jpeg"))
            {
                ct = http_content_type::code::CONTENT_TYPE_IMAGE_JPEG;
            }
            else if (minerva::ci_equals(ext, "html") || minerva::ci_equals(ext, "htm"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_HTML;
            }
            else if (minerva::ci_equals(ext, "txt") || minerva::ci_equals(ext, "log"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_PLAIN;
            }
            else if (minerva::ci_equals(ext, "xml"))
            {
                ct = http_content_type::code::CONTENT_TYPE_APPLICATION_XML;
            }
            else if (minerva::ci_equals(ext, "json"))
            {
                ct = http_content_type::code::CONTENT_TYPE_APPLICATION_JSON;
            }
            else if (minerva::ci_equals(ext, "js"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_JAVASCRIPT;
            }
            else if (minerva::ci_equals(ext, "png"))
            {
                ct = http_content_type::code::CONTENT_TYPE_IMAGE_PNG;
            }
            else if (minerva::ci_equals(ext, "css"))
            {
                ct = http_content_type::code::CONTENT_TYPE_TEXT_CSS;
            }
        }
        return send_file(filename, ct, ctx);
    }


    bool controller::send_file(const std::string & filename,
                               http_content_type::code content_type,
                               http_context & ctx)
    {
        if (is_unsafe_filename(filename))
        {
            LOG_ERROR("Invalid or unsafe filename: " << filename);
            // Don't reveal the path-traversal attempt to the client.
            ctx.response().status_code_not_found();
            return false;
        }

        std::ifstream is(filename, std::ios::in | std::ios::binary);
        if (!is)
        {
            ctx.response().status_code_not_found();
            LOG_ERROR("Failed to open file for read: " << filename);
            return false;
        }

        ctx.response().content_type(content_type);

        is.seekg(0, is.end);
        std::streampos file_size_pos = is.tellg();

        if (file_size_pos == std::streampos(-1))
        {
            LOG_ERROR("Failed to get file size: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        size_t file_size = static_cast<size_t>(file_size_pos);

        if (file_size > s_max_send_file_size)
        {
            LOG_ERROR("File too large: " << filename
                      << " (" << file_size << " bytes, limit "
                      << s_max_send_file_size << ")");
            ctx.response().status_code_internal_error();
            return false;
        }

        is.seekg(0, is.beg);
        if (is.fail())
        {
            LOG_ERROR("Failed to seek to beginning: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        // For large files, switch to chunked transfer encoding so we don't
        // buffer the entire file in m_response_stream. The httpd dispatch
        // loop will call flush_final_chunk() once we return.
        const bool chunked = file_size >= SEND_FILE_CHUNK_THRESHOLD;
        size_t remaining   = file_size;
        size_t since_flush = 0;

        try
        {
            while (remaining > 0)
            {
                char buf[64 * 1024];
                size_t to_read = std::min(sizeof(buf), remaining);

                is.read(buf, to_read);
                std::streamsize actually_read = is.gcount();

                if (actually_read <= 0)
                {
                    LOG_ERROR("Failed to read from file: " << filename);
                    ctx.response().status_code_internal_error();
                    return false;
                }

                ctx.response().response_stream().write(buf, actually_read);
                remaining   -= actually_read;
                since_flush += actually_read;

                if (chunked && since_flush >= SEND_FILE_FLUSH_INTERVAL)
                {
                    // First flush() sets chunked=true on the response and
                    // writes the headers; subsequent calls send a chunk
                    // and clear m_response_stream.
                    ctx.response().status_code_success();
                    ctx.response().flush();
                    since_flush = 0;
                }

                if (actually_read < static_cast<std::streamsize>(to_read) && remaining > 0)
                {
                    LOG_ERROR("Unexpected EOF in file: " << filename);
                    ctx.response().status_code_internal_error();
                    return false;
                }
            }
        }
        catch (const std::exception & e)
        {
            LOG_ERROR("Exception during file read: " << e.what());
            ctx.response().status_code_internal_error();
            return false;
        }

        ctx.response().status_code_success();
        return true;
    }
}
