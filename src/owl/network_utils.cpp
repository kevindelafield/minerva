#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <cstring>
#include "network_utils.h"
#include "log.h"

#define MAC_ADDRESS_LEN (6)
#define OVR_MAC_ADDRESS_SIZE 18

namespace owl
{

    bool get_mac_address(std::string & macAddress, bool colons)
    {
        struct ifreq buffer;
        int sock, retval;
        
        int maxLen = OVR_MAC_ADDRESS_SIZE;
        char macAddr[OVR_MAC_ADDRESS_SIZE];
        
        //Check the buffer size
        if (maxLen < (MAC_ADDRESS_LEN * 2 + 1))
        {
            LOG_ERROR("OVR_GetMACAddress: Buffer is not big enough to store the MAC address");
            return false;
        }

        sock = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (sock < 0)
        {
            LOG_ERROR_ERRNO("Failed to create a socket.  OVR_GetMACAddress returned an error", errno);
            return false;
        }

        std::memset(macAddr, 0, maxLen);
        std::memset(&buffer, 0x00, sizeof(buffer));

        //Get MAC add of Ethernet interface first
        std::strcpy(buffer.ifr_name, "eth0");
        retval = ioctl(sock, SIOCGIFHWADDR, &buffer);
        if(retval != 0)
        {
            //Try MAC address of WLAN interface if we fail Ethernet
            std::memset(&buffer, 0x00, sizeof(buffer));
            std::strcpy(buffer.ifr_name, "enp0s3");
            retval = ioctl(sock, SIOCGIFHWADDR, &buffer);
        }
        if(retval != 0)
        {
            //Try MAC address of WLAN interface if we fail Ethernet
            std::memset(&buffer, 0x00, sizeof(buffer));
            std::strcpy(buffer.ifr_name, "wlan0");
            retval = ioctl(sock, SIOCGIFHWADDR, &buffer);
        }
        if(retval != 0)
        {
            LOG_ERROR_ERRNO("OVR_GetMACAddress failed to get network address returned an error", errno);
            return false;
        }
        std::snprintf(macAddr, maxLen, "%02X%02X%02X%02X%02X%02X",
                      (unsigned char)buffer.ifr_hwaddr.sa_data[0],
                      (unsigned char)buffer.ifr_hwaddr.sa_data[1],
                      (unsigned char)buffer.ifr_hwaddr.sa_data[2],
                      (unsigned char)buffer.ifr_hwaddr.sa_data[3],
                      (unsigned char)buffer.ifr_hwaddr.sa_data[4],
                      (unsigned char)buffer.ifr_hwaddr.sa_data[5]);

        close(sock);

        macAddress = macAddr;

        if (colons)
        {
            std::stringstream ss;
            ss << macAddress[0] << macAddress[1] << ":";
            ss << macAddress[2] << macAddress[3] << ":";
            ss << macAddress[4] << macAddress[5] << ":";
            ss << macAddress[6] << macAddress[7] << ":";
            ss << macAddress[8] << macAddress[9] << ":";
            ss << macAddress[9] << macAddress[11];
            macAddress = ss.str();
        }

        return true;
    }
}
