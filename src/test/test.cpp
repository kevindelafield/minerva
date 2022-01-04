#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdlib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/engine.h>
#include <openssl/rand.h>
#include <openssl/md5.h>
#include <chrono>
#include <mutex>
#include <util/exec_utils.h>
#include <util/string_utils.h>
#include <util/log.h>
#include <util/unique_command.h>
#include <owl/connection.h>
#include <curl/curl.h>
#include <pugixml.hpp>

using namespace owl;

int main(int argc, char** argv)
{
    LOG_INFO("running tar");

    std::vector<std::string> args;
    args.push_back("-C");
    args.push_back("/home/kevin/foo");
    args.push_back("-xzf");
    args.push_back("-");

    util::execl proc("/bin/tar", args);

    int pid;

    if (!proc.start(pid))
    {
        LOG_ERROR("failed to run tar");
        return 1;
    }

    LOG_INFO("running sleep");

    int pid2;
    int status = util::execbg("/bin/sleep 1000", pid2);
    if (status)
    {
        LOG_ERROR("failed to run sleep");
        return 1;
    }

    proc.close();

    LOG_INFO("waiting for tar");

    if (!proc.wait())
    {
        LOG_ERROR("failed to wait on tar");
        return 1;
    }

    LOG_INFO("success");

    return 0;
}
