#include <sys/epoll.h>
#include <string.h>
#include "die.h"
#include "epoll.h"

void set_epoll_descriptor(int fd_epoll, int action, int fd_toadd, int events)
{
    int ret;
    struct epoll_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd_toadd;

    ret = epoll_ctl(fd_epoll, action, fd_toadd, &ev);
    if (ret < 0){
        panic("Cannot add item for epoll!\n");
    }
}
