#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

int get_mac(char *name, char *str_mac)
{
        int fd;
        struct ifreq ifr;
        unsigned char *mac = NULL;

        memset(&ifr, 0, sizeof(ifr));

        fd = socket(AF_INET, SOCK_DGRAM, 0);

        ifr.ifr_addr.sa_family = AF_INET;
        strncpy(ifr.ifr_name , name, IFNAMSIZ-1);

        if (0 == ioctl(fd, SIOCGIFHWADDR, &ifr)) {
                mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;

                /* fmt mac address */
                snprintf(str_mac, 17, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        close(fd);

        return 0;
}
