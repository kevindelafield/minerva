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
#include <ovhttpd/exec_utils.h>
#include <ovhttpd/string_utils.h>
#include <ovhttpd/log.h>
#include <ovhttpd/unique_command.h>
#include <ovhttpd/connection.h>
#include <curl/curl.h>
#include <pugixml.hpp>
#include <basetypes.h>
#include <iav_common.h>
#include <iav_vin_ioctl.h>

#define DIV_REV(x, y) ((2 * (x))/(2 * (y) - 1))

int main(int argc, char** argv)
{
    // util::sensor_lock lock;

    // while (true)
    // {
    //     LOG_INFO("locking...");
    //     std::unique_lock<util::sensor_lock> lk(lock);
    //     LOG_INFO("locked");
    //     std::this_thread::sleep_for(std::chrono::seconds(5));
    // }

    if (argc != 2)
    {
        LOG_ERROR("usage: test <fps>");
        return 1;
    }

    int fps;

    if (!ovhttpd::try_parse_int(argv[1], fps))
    {
        LOG_ERROR("usage: test <fps>");
        return 1;
    }

    int fd = open("/dev/iav", O_RDWR, 0);
    if (fd < 0)
    {
        LOG_ERROR_ERRNO("failed top open /dev/iav", errno);
        return 1;
    }

    struct vindev_fps vsrc_fps;

    vsrc_fps.vsrc_id = 0;

    if (ioctl(fd, IAV_IOC_VIN_GET_FPS, &vsrc_fps))
    {
        LOG_ERROR_ERRNO("failed to set FPS on /dev/iav", errno);
        return 1;
    }

    LOG_INFO("current VIN FPS: " << DIV_REV(512000000, vsrc_fps.fps));

    vsrc_fps.vsrc_id = 0;
    vsrc_fps.fps = DIV_CLOSEST(512000000, fps);

    LOG_INFO("setting VIN FPS to: " << fps);

    if (ioctl(fd, IAV_IOC_VIN_SET_FPS, &vsrc_fps))
    {
        LOG_ERROR_ERRNO("failed to set FPS on /dev/iav", errno);
        return 1;
    }

    LOG_INFO("set VIN FPS to: " << fps);

    if (close(fd))
    {
        LOG_ERROR_ERRNO("failed to flose /dev/iav file descriptor", errno);
        return 1;
    }

    return 0;
}
