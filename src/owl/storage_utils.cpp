#include <sys/statvfs.h>
#include <cstring>
#include "storage_utils.h"

namespace owl
{

    long get_free_space(const std::string & path)
    {
        struct statvfs stat;
        
        if (statvfs(path.c_str(), &stat) != 0) {
            // error happens, just quits here
            return -1;
        }
        // the available size is f_bsize * f_bavail
        return stat.f_bsize * stat.f_bavail;
    }
}
