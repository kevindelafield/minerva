#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex>
#include <vector>
#include <sstream>
#include <jsoncpp/json/json.h>
#include <owl/component_visor.h>
#include <util/string_utils.h>
#include <util/file_utils.h>
#include <util/log.h>
#include <httpd/http_request.h>
#include <httpd/http_response.h>
#include <httpd/httpd.h>
#include "file_server.h"

namespace www
{

    void file_server::initialize()
    {
        auto svr = get_component<httpd::httpd>(httpd::httpd::NAME);
        if (svr)
        {
            svr->register_default_controller(this);
        }

        auto conf = config();
        if (!conf["www_root_dir"].isString())
        {
            FATAL("www_root_dir config file entry missing");
        }

        if (!conf["www_default_file"].isString())
        {
            FATAL("www_default_file config file entry missing");
        }

        m_root_dir = conf["www_root_dir"].asString();
        m_default_file = conf["www_default_file"].asString();

        if (!util::file_is_directory(m_root_dir))
        {
            FATAL("www root directory does not exist: " << m_root_dir);
        }

        std::string def_file = m_root_dir + "/" + m_default_file;
        if (!util::file_is_file(def_file))
        {
            FATAL("www default file does not exist: " << def_file);
        }
    }

    bool file_server::auth_callback(const std::string & user,
                                       const std::string & op) 
    {
        Json::Value root;
        Json::Value viewer;

        if (user == "root")
        {
            return true;
        }

        return false;
    }

    std::string file_server::resolve_secure_path(const std::string& requested_path)
    {
        // Handle root path
        if (requested_path == "/")
        {
            return m_root_dir + "/" + m_default_file;
        }

        // URL decode the path first to handle encoded directory traversal attempts
        std::string decoded_path = httpd::http_request::url_decode(requested_path);
        
        // Reject paths containing dangerous patterns
        if (decoded_path.find("..") != std::string::npos ||
            decoded_path.find("\\") != std::string::npos ||
            decoded_path.find("\0", 0, 1) != std::string::npos)
        {
            return "";
        }

        // Normalize path separators and remove double slashes
        std::string normalized_path = decoded_path;
        
        // Replace multiple consecutive slashes with single slash
        std::regex multi_slash("/+");
        normalized_path = std::regex_replace(normalized_path, multi_slash, "/");
        
        // Ensure path starts with /
        if (normalized_path.empty() || normalized_path[0] != '/')
        {
            return "";
        }

        // Split path into segments and rebuild canonically
        std::vector<std::string> segments;
        std::stringstream path_stream(normalized_path);
        std::string segment;
        
        while (std::getline(path_stream, segment, '/'))
        {
            if (segment.empty() || segment == ".")
            {
                // Skip empty segments and current directory references
                continue;
            }
            else if (segment == "..")
            {
                // Parent directory - remove last segment if possible
                if (!segments.empty())
                {
                    segments.pop_back();
                }
                // If we try to go above root, that's suspicious
                else
                {
                    return "";
                }
            }
            else
            {
                segments.push_back(segment);
            }
        }

        // Build the final path
        std::string final_path = m_root_dir;
        for (const auto& seg : segments)
        {
            final_path += "/" + seg;
        }

        // Additional security check: ensure the resolved path is still under m_root_dir
        // This protects against symlink attacks and other sophisticated bypasses
        try
        {
            // Get canonical (absolute, resolved) paths for comparison
            char* resolved_root = realpath(m_root_dir.c_str(), nullptr);
            char* resolved_final = realpath(final_path.c_str(), nullptr);
            
            if (!resolved_root)
            {
                // Root directory doesn't exist or isn't accessible
                return "";
            }
            
            std::string canonical_root(resolved_root);
            free(resolved_root);
            
            if (!resolved_final)
            {
                // File doesn't exist, but that's OK - we just need to check the parent directory
                // Try to resolve the parent directory instead
                std::string parent_path = final_path;
                size_t last_slash = parent_path.find_last_of('/');
                if (last_slash != std::string::npos)
                {
                    parent_path = parent_path.substr(0, last_slash);
                    resolved_final = realpath(parent_path.c_str(), nullptr);
                }
            }
            
            if (resolved_final)
            {
                std::string canonical_final(resolved_final);
                free(resolved_final);
                
                // Ensure the canonical final path starts with the canonical root path
                if (canonical_final.find(canonical_root) != 0)
                {
                    return "";
                }
                
                // Ensure there's a path separator or end of string after the root
                size_t root_len = canonical_root.length();
                if (canonical_final.length() > root_len && 
                    canonical_final[root_len] != '/')
                {
                    return "";
                }
            }
        }
        catch (...)
        {
            // If anything goes wrong with path resolution, deny access
            return "";
        }

        return final_path;
    }

    void file_server::handle_request(httpd::http_context & ctx, const std::string & op)
    {
        ctx.response().add_header("Pragma", "no-cache");
        ctx.response().add_header("Cache-Control", "no-cache");

        std::string requested_path = ctx.request().path();

        // Secure path validation and resolution
        std::string filename = resolve_secure_path(requested_path);
        if (filename.empty())
        {
            LOG_WARN("Path traversal attempt blocked: " << requested_path);
            ctx.response().status_code_forbidden();
            return;
        }

        if (!util::file_is_file(filename))
        {
            LOG_DEBUG("didn't find file: " << filename);
            ctx.response().status_code_not_found();
            return;
        }

        LOG_DEBUG("found file: " << filename);

        send_file(filename, ctx);

        ctx.response().status_code_success();
    }
}
