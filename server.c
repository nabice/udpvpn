#include <sys/timerfd.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdint.h>
#include "tun.h"
#include "config.h"
#include "die.h"
#include "epoll.h"
#include "crypt.h"

struct client_t {
    char idle;  //idle time, 0 for not used client
    struct sockaddr_in addr;  //real addr info
    int slen;      //sizeof addr
};

int get_empty_client(struct client_t * clients)
{
    int i;
    for(i = 0;i < CLIENT_SIZE; i++){
        if(clients[i].idle == '\0'){
            return i;
        }
    }
    return -1;
}

int get_client_by_addr(struct sockaddr_in * si_client, struct client_t * clients)
{
    int i;
    for(i = 0;i < CLIENT_SIZE; i++){
        if(si_client->sin_port == clients[i].addr.sin_port && si_client->sin_addr.s_addr == clients[i].addr.sin_addr.s_addr){
            return i;
        }
    }
    return -1;
}


int main(void)
{
    struct sockaddr_in si_server;
    struct sockaddr_in si_client;
    struct epoll_event events[3];
    struct client_t clients[CLIENT_SIZE];
    struct itimerspec new_value;

    char buffer[BUFLEN];
    char command[10] = "NUDPN";
    int sockfd, tunfd, timerfd, i, j, nfds, epfd, nread, client_id;
    uint64_t exp;
    socklen_t slen = sizeof(si_client);
    struct ip * client_ip;

    memset((char *) &clients, 0, sizeof(clients));
    
    if((tunfd = tun_alloc("tun5", IFF_TUN | IFF_NO_PI)) < 0){
        panic("Open tun failed\n");
    }
    if(system("/sbin/ifconfig tun5 172.16.200.1/24")){
        panic("Set ip failed\n");
    }
    if ((sockfd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        panic("Open socket failed\n");
    }

    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
    memset((char *) &si_server, 0, sizeof(si_server));
    si_server.sin_family = AF_INET;
    si_server.sin_port = htons(PORT);
    si_server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *)&si_server, sizeof(si_server))==-1) {
        panic("Bind failed\n");
    }

    if((timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1){
        panic("Timer failed\n");
    }
    new_value.it_value.tv_sec = 600;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 600;
    new_value.it_interval.tv_nsec = 0;
    timerfd_settime(timerfd, 0, &new_value, NULL);
    
    epfd = epoll_create(3);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, tunfd, EPOLLIN | EPOLLET);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, sockfd, EPOLLIN | EPOLLET);
    set_epoll_descriptor(epfd, EPOLL_CTL_ADD, timerfd, EPOLLIN | EPOLLET);

    while(1){
        nfds = epoll_wait(epfd, events, 3, -1);
        for(i = 0; i < nfds; i++){
            if(events[i].data.fd == sockfd){
                while((nread=recvfrom(sockfd, buffer, BUFLEN, 0, (struct sockaddr *)&si_client, &slen)) >= 0){
                    client_id = get_client_by_addr(&si_client, clients);
                    if(client_id != -1){
                        clients[client_id].idle = 1;
                    }
                    if(nread == 10 && buffer[0] == 'N' && buffer[1] == 'U' && buffer[2] == 'D' && buffer[3] == 'P' && buffer[4] == 'N'){
                        if(buffer[5] == 'C'){//add new client
                            if(client_id == -1){
                                if((client_id = get_empty_client(clients)) >= 0){
                                    clients[client_id].idle = 1;
                                    clients[client_id].addr = si_client;
                                    clients[client_id].slen = slen;
                                    command[5] = 'S';//set client ip
                                    command[6] = client_id;
                                    sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
                                    LOG("New client: %d, %u\n", client_id, si_client.sin_addr.s_addr);
                                }else{
                                    command[5] = 'F'; //too many clients
                                    sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
                                }
                            }
                        }else if(buffer[5] == 'L'){//Keep alive
                            if(client_id != -1){
                                command[5] = 'L';
                                sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
                            }
                        }
                    }else{
                        decrypt(buffer, nread);
                        if(client_id == -1){
                            client_ip = (struct ip *)buffer;
                            if(client_ip->ip_v == 4){
                                client_id = (client_ip->ip_src.s_addr >> 24) - 2;
                                if(client_id >= 0 && client_id < CLIENT_SIZE){
                                    //client_ip may be changed, or client has multiple wan ip
                                    LOG("Client ip/port changed: %d, %u\n", client_id, si_client.sin_addr.s_addr);
                                    clients[client_id].idle = 1;
                                    clients[client_id].addr = si_client;
                                    clients[client_id].slen = slen;
                                }else{
                                    LOG("Can't find client: %d, %u\n", client_id, client_ip->ip_src.s_addr);
                                    command[5] = 'K'; //can't find client, make client down
                                    sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
                                }
                            }
                        }
                        if(write(tunfd, buffer, nread) < 0){
                            nopanic("Write tun failed\n");
                        }
                    }
                }
            }else if(events[i].data.fd == tunfd){
                while((nread = read(tunfd, buffer, sizeof(buffer))) >= 0){
                    client_ip = (struct ip *)buffer;
                    if(client_ip->ip_v == 4){
                        client_id = (client_ip->ip_dst.s_addr >> 24) - 2;
                        if(client_id >= 0 && client_id < CLIENT_SIZE && clients[client_id].idle != 0){
                            encrypt(buffer, nread);
                            sendto(sockfd, buffer, nread, 0, (struct sockaddr *)&clients[client_id].addr, clients[client_id].slen);
                        }
                    }
                }
            }else if(events[i].data.fd == timerfd){
                if(read(timerfd, &exp, sizeof(uint64_t)) != sizeof(uint64_t)){
                    nopanic("Timer read failed\n");
                }
                for(j = 0;j < CLIENT_SIZE; j++){
                    if(clients[j].idle > 0){
                        if(clients[j].idle > 4){
                            clients[j].idle = 0;
                        }else{
                            clients[j].idle++;
                        }
                    }
                }
            }
        }
    }

    close(sockfd);
    close(tunfd);
    return 0;
}
