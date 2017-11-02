#include <net/if.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <linux/if_tun.h>
#include <unistd.h>
#include "tun.h"

int tun_alloc(char *name, int flags) {
    struct ifreq ifr;
    int fd, err;
    char *clonedev = "/dev/net/tun";

    if ((fd = open(clonedev, O_RDWR)) < 0) {
        return fd;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
        close(fd);
        return err;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}
