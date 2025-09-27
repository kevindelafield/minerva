#include <string>
#include <istream>
#include <fstream>
#include <cstdio>
#include <util/string_utils.h>
#include "controller.h"
#include "http_context.h"

namespace minerva
{

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
        m_handlers[name] = func;
    }

    bool controller::next_path_segment(std::istream & is,
                                       std::string & next)
    {
        if (is.peek() == EOF)
        {
            next = "";
            return false;
        }
        while (is.peek() == static_cast<int>('/'))
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
        // Validate filename for security
        if (filename.find("..") != std::string::npos) {
            LOG_ERROR("Invalid filename contains path traversal: " << filename);
            ctx.response().status_code_bad_request();
            return false;
        }

        std::ofstream os(filename, std::ios::out | std::ios::binary);
        if (!os) {
            LOG_ERROR("Failed to open file for write: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        size_t read = 0;
        bool success = true;
        
        try {
            do {
                char buf[128*1024];
                read = ctx.request().read(buf, sizeof(buf), 30000); // 30 second timeout
                
                if (read > 0) {
                    os.write(buf, read);
                    if (os.fail()) {
                        LOG_ERROR("Failed to write to file: " << filename);
                        success = false;
                        break;
                    }
                }
            } while (read > 0);
            
            // Ensure all data is flushed
            os.flush();
            if (os.fail()) {
                LOG_ERROR("Failed to flush file: " << filename);
                success = false;
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception during file write: " << e.what());
            success = false;
        }

        os.close();
        
        if (!success) {
            // Clean up partial file
            std::remove(filename.c_str());
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
        // Validate filename for security  
        if (filename.find("..") != std::string::npos) {
            LOG_ERROR("Invalid filename contains path traversal: " << filename);
            ctx.response().status_code_not_found(); // Don't reveal path traversal attempt
            return false;
        }

        std::ifstream is(filename, std::ios::in | std::ios::binary);
        if (!is) {
            ctx.response().status_code_not_found();
            LOG_ERROR("Failed to open file for read: " << filename);
            return false;
        }

        ctx.response().content_type(content_type);
        
        // Get file size with proper error checking
        is.seekg(0, is.end);
        std::streampos file_size_pos = is.tellg();
        
        if (file_size_pos == std::streampos(-1)) {
            LOG_ERROR("Failed to get file size: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }
        
        size_t file_size = static_cast<size_t>(file_size_pos);
        
        // Check for reasonable file size limits (e.g., 100MB)
        const size_t MAX_FILE_SIZE = 100 * 1024 * 1024;
        if (file_size > MAX_FILE_SIZE) {
            LOG_ERROR("File too large: " << filename << " (" << file_size << " bytes)");
            ctx.response().status_code_internal_error();
            return false;
        }
        
        is.seekg(0, is.beg);
        if (is.fail()) {
            LOG_ERROR("Failed to seek to beginning: " << filename);
            ctx.response().status_code_internal_error();
            return false;
        }

        size_t remaining = file_size;
        
        try {
            while (remaining > 0) {
                char buf[10*1024];
                size_t to_read = std::min(sizeof(buf), remaining);
                
                is.read(buf, to_read);
                std::streamsize actually_read = is.gcount();
                
                if (actually_read <= 0) {
                    LOG_ERROR("Failed to read from file: " << filename);
                    ctx.response().status_code_internal_error();
                    return false;
                }
                
                ctx.response().response_stream().write(buf, actually_read);
                remaining -= actually_read;
                
                // Check for premature EOF
                if (actually_read < static_cast<std::streamsize>(to_read) && remaining > 0) {
                    LOG_ERROR("Unexpected EOF in file: " << filename);
                    ctx.response().status_code_internal_error();
                    return false;
                }
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception during file read: " << e.what());
            ctx.response().status_code_internal_error();
            return false;
        }

        ctx.response().status_code_success();
        return true;
    }
}
