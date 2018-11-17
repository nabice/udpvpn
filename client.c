#include <sys/timerfd.h>
#include <stdint.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>
#include "tun.h"
#include "config.h"
#include "die.h"
#include "epoll.h"
#include "crypt.h"

/*
  usage: nudpn_client [SERVER_IP [COMMAND_AFTER_CONNECT]]
 */
int main(int argc, char *argv[]) {
    struct sockaddr_in si_server;
    struct epoll_event events[3];
    struct itimerspec new_value;

    unsigned char buffer[BUFLEN];
    char setip_cmd[70];
    char command[10] = "NUDPN";
    const char *srv_ip;
    int sockfd, tunfd, timerfd, i, nfds, epfd, nread;
    int ip_done = 0;
    int idle = 1;
    socklen_t slen = sizeof(si_server);
    uint64_t exp;
    const char *client_ip_prefix;
    char *tun_name;

    if(argc > 1){
        srv_ip = argv[1];
    }else{
        srv_ip = SRV_IP;
    }
    
    if(argc > 2){
        client_ip_prefix = argv[2];
    } else {
        client_ip_prefix = CLIENT_IP_PREFIX;
    }

    if(argc > 4){
        tun_name = argv[4];
    } else {
        tun_name = TUN_NAME;
    }

    if((tunfd = tun_alloc(tun_name, IFF_TUN | IFF_NO_PI)) < 0){
        panic("Open tun failed\n");
    }

    if ((sockfd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
        panic("Open socket failed\n");
    }
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
    memset((char *) &si_server, 0, sizeof(si_server));
    si_server.sin_family = AF_INET;
    si_server.sin_port = htons(PORT);
    if (inet_aton(srv_ip, &si_server.sin_addr) == 0) {
        panic("inet_aton() failed\n");
    }

    if((timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1){
        panic("Timer failed\n");
    }

    if(daemon(0, 1) != 0){
        return -1;
    }

    new_value.it_value.tv_sec = 10;
    new_value.it_value.tv_nsec = 0;
    if(argc > 5){
        new_value.it_interval.tv_sec = 60;//add new super client
    } else {
        new_value.it_interval.tv_sec = 600;
    }
    new_value.it_interval.tv_nsec = 0;
    timerfd_settime(timerfd, 0, &new_value, NULL);

    epfd = epoll_create(3);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, tunfd, EPOLLIN | EPOLLET);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, sockfd, EPOLLIN | EPOLLET);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, timerfd, EPOLLIN | EPOLLET);
    if(argc > 5){
        command[5] = 'S';//add new super client
    } else {
        command[5] = 'C';//add new client
    }
    sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_server, slen);
    while(1){
        nfds = epoll_wait(epfd, events, 3, -1);
        for(i = 0; i < nfds; i++){
            if(events[i].data.fd == sockfd){
                while((nread=recvfrom(sockfd, buffer, BUFLEN, 0, (struct sockaddr *)&si_server, &slen)) >= 0){
                    idle = 1;
                    if(nread == 10 && buffer[0] == 'N' && buffer[1] == 'U' && buffer[2] == 'D' && buffer[3] == 'P' && buffer[4] == 'N'){
                        if(buffer[5] == 'S'){
                            sprintf(setip_cmd, "ip addr add %s%d/24 dev %s && ip link set %s up", client_ip_prefix, buffer[6] + 2, tun_name, tun_name);
                            if(system(setip_cmd)){
                                panic("Set ip failed\n");
                            }
                            if(argc > 3){
                                if(system(argv[3])){
                                    nopanic("Command after connect exec failed\n");
                                }
                            }
                            ip_done = 1;
                        }else if(buffer[5] == 'F'){
                            panic("Too many clients\n");
                        }else if(buffer[5] == 'L'){
                            //server received our keepalive packet, then sent us one
                        }else if(buffer[5] == 'K'){
                            command[5] = 'C';//add new client
                            sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_server, slen);
                            //server can't find me, I tell server to create new client
                        }
                    }else{
                        decrypt(buffer, nread);
                        if(write(tunfd, buffer, nread) < 0){
                            nopanic("Write tun failed\n");
                        }
                    }
                }
            }else if(events[i].data.fd == tunfd){
                while((nread = read(tunfd, buffer, sizeof(buffer))) >= 0){
                    encrypt(buffer, nread);
                    sendto(sockfd, buffer, nread, 0, (struct sockaddr *)&si_server, slen);
                }
            }else if(events[i].data.fd == timerfd){
                if(read(timerfd, &exp, sizeof(uint64_t)) != sizeof(uint64_t)){
                    nopanic("Timer read failed\n");
                }
                if(!ip_done){
                    panic("Can not connect to the server\n");
                }
                if(idle > 4){
                    panic("Server timeout\n");
                }
                idle++;
                command[5] = 'L';
                sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_server, slen);
            }
        }
    }
    close(sockfd);
    close(tunfd);
    return 0;
}
