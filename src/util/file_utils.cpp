#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include "string_utils.h"
#include "log.h"
#include "file_utils.h"

namespace util
{

    bool file_is_empty(const std::string & path)
    {
        struct stat sb;
        int status = stat(path.c_str(), &sb);
        if (status)
        {
            return true;
        }
        return S_ISREG(sb.st_mode) && sb.st_size == 0;
    }

    bool file_exists(const std::string & path)
    {
        struct stat sb;
        return stat(path.c_str(), &sb) == 0;
    }

    bool file_is_directory(const std::string & dir)
    {
        struct stat sb;
        return stat(dir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
    }

    bool file_is_file(const std::string & file)
    {
        struct stat sb;
        return stat(file.c_str(), &sb) == 0 && S_ISREG(sb.st_mode);
    }

    bool touch(const std::string & file)
    {
        int fd = open(file.c_str(),
                      O_WRONLY|O_CREAT|O_NOCTTY|O_NONBLOCK|O_CLOEXEC,
                      0666);
        if (fd < 0)
        {
            LOG_ERROR_ERRNO("failed to open touch file: " << file, errno);
            return false;
        }
        int status = utimensat(AT_FDCWD,
                               file.c_str(),
                               nullptr,
                               0);
        if (status)
        {
            LOG_ERROR_ERRNO("couldn't touch file: " << file, errno);
            return false;
        }
        status = close(fd);
        if (status)
        {
            LOG_ERROR_ERRNO("couldn't close touch file: " << file, errno);
            return false;
        }
        return true;
    }

    static bool rmdir_r(const char * path, bool recursive)
    {
        DIR * d = opendir(path);
        if (!d)
        {
            LOG_ERROR("failed to list directory: " << path);
            return false;
        }
        struct dirent * dir;
        while ((dir = readdir(d)) != nullptr)
        {
            std::string name(dir->d_name);

            std::stringstream ss;
            ss << path;
            ss << "/";
            ss << name;
            std::string fp(ss.str());

            if (name == "." || name == "..")
            {
            }
            else if (dir->d_type == DT_DIR)
            {
                if (recursive)
                {
                    if (!rmdir_r(fp.c_str(), recursive))
                    {
                        if (closedir(d))
                        {
                            LOG_ERROR_ERRNO("error closing directory" << 
                                            path, errno);
                        }
                        return false;
                    }
                }
            }
            else
            {
                if (unlink(fp.c_str()))
                {
                    LOG_ERROR_ERRNO("failed to remove file system entry: " <<
                                    fp, errno);
                    if (closedir(d))

                    {
                        LOG_ERROR_ERRNO("error closing directory" << 
                                        path, errno);
                    }
                    return false;
                }
            }
        }
        if (closedir(d))
        {
            LOG_ERROR_ERRNO("error closing directory: " << path, errno);
        }
        if (::rmdir(path))
        {
            LOG_ERROR_ERRNO("failed to remove directory entry: " << path, 
                            errno);
            return false;
        }
        return true;
    }

    bool rmdir(const std::string & dir, bool recursive)
    {
        return rmdir_r(dir.c_str(), recursive);
    }

    bool enum_files(const std::string & path, 
                    std::vector<std::string> & files,
                    const std::string & end_pattern)
    {
        DIR * d = opendir(path.c_str());
        if (!d)
        {
            LOG_ERROR("failed to list directory: " << path);
            return false;
        }
        struct dirent * dir;
        while ((dir = readdir(d)) != nullptr)
        {
            std::string name(dir->d_name);

            if (!end_pattern.empty() && !ends_with(name, end_pattern))
            {
                continue;
            }

            std::stringstream ss;
            ss << path;
            ss << "/";
            ss << name;
            std::string fp(ss.str());

            if (name == "." || name == "..")
            {
            }
            else if (dir->d_type == DT_DIR)
            {
                continue;
            }
            else
            {
                files.push_back(name);
            }
        }
        if (closedir(d))
        {
            LOG_ERROR_ERRNO("error closing directory: " << path, errno);
        }
        return true;
    }

}
