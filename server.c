#include <sys/timerfd.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
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
#include <unistd.h>

struct client_t {
    char idle;  //idle time, 0 for not used client
    struct sockaddr_in6 addr;  //real addr info
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

int get_client_by_addr(struct sockaddr_in6 * si_client, struct client_t * clients)
{
    int i;
    for(i = 0;i < CLIENT_SIZE; i++){
        if(si_client->sin6_port == clients[i].addr.sin6_port && memcmp(si_client->sin6_addr.s6_addr, clients[i].addr.sin6_addr.s6_addr, 16) == 0){
            return i;
        }
    }
    return -1;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in6 si_server;
    struct sockaddr_in6 si_client;
    struct epoll_event events[3];
    struct client_t clients[CLIENT_SIZE];
    struct itimerspec new_value;

    unsigned char buffer[BUFLEN];
    char setip_cmd[70], subnet_s[16], ip_str[40];
    char command[10] = "NUDPN";
    int sockfd, tunfd, timerfd, i, j, nfds, epfd, nread, client_id;
    int super_client = -1;
    uint64_t exp;
    socklen_t slen = sizeof(si_client);
    struct ip *client_ip;
    const char *client_ip_prefix;
    if(argc > 1){
        client_ip_prefix = argv[1];
    } else {
        client_ip_prefix = CLIENT_IP_PREFIX;
    }
    memset((char *) &clients, 0, sizeof(clients));
    
    if((tunfd = tun_alloc("tun4", IFF_TUN | IFF_NO_PI)) < 0){
        panic("Open tun failed\n");
    }
    sprintf(setip_cmd, "ip addr add %s1/24 dev tun4 && ip link set tun4 up", client_ip_prefix);
    sprintf(subnet_s, "%s0", client_ip_prefix);
    in_addr_t subnet = inet_addr(subnet_s);
    if(system(setip_cmd)){
        panic("Set ip failed\n");
    }
    if(argc > 3){
        if(system(argv[3])){
            nopanic("Command after connect exec failed\n");
        }
    }

    if ((sockfd=socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        panic("Open socket failed\n");
    }

    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);
    memset((char *) &si_server, 0, sizeof(si_server));
    si_server.sin6_family = AF_INET6;
    si_server.sin6_port = htons(PORT);
    si_server.sin6_addr = in6addr_any;
    if (bind(sockfd, (struct sockaddr *)&si_server, sizeof(si_server))==-1) {
        panic("Bind failed\n");
    }

    if((timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1){
        panic("Timer failed\n");
    }
    if(argc < 3 || strncmp(argv[2], "-d", 2) != 0){
        if(daemon(0, 1) != 0){
            return -1;
        }
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
                        if(buffer[5] == 'C' || buffer[5] == 'S'){//add new client
                            if(client_id == -1){
                                if((client_id = get_empty_client(clients)) >= 0){
                                    if(buffer[5] == 'S') {
                                        super_client = client_id;
                                    }
                                    clients[client_id].idle = 1;
                                    clients[client_id].addr = si_client;
                                    clients[client_id].slen = slen;
                                    command[5] = 'S';//set client ip
                                    command[6] = client_id;
                                    sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
                                    LOG("New client: %d, %s\n", client_id, inet_ntop(AF_INET6, si_client.sin6_addr.s6_addr, ip_str, 40));
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
                                client_id = buffer[15] - 2;
                            } else if(client_ip->ip_v == 6){
                                client_id = buffer[23] - 2;
                            }
                            if(client_id >= 0 && client_id < CLIENT_SIZE){
                                //client_ip may be changed, or client has multiple wan ip
                                LOG("Client ip/port changed: %d, %s\n", client_id, inet_ntop(AF_INET6, si_client.sin6_addr.s6_addr, ip_str, 40));
                                clients[client_id].idle = 1;
                                clients[client_id].addr = si_client;
                                clients[client_id].slen = slen;
                            }else if(client_ip->ip_v == 4){
                                LOG("Can't find client: %d, %u\n", client_id, client_ip->ip_src.s_addr);
                                command[5] = 'K'; //can't find client, make client down
                                sendto(sockfd, command, 10, 0, (struct sockaddr *)&si_client, slen);
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
                    client_id = -1;
                    if(client_ip->ip_v == 4){
                        if((client_ip->ip_dst.s_addr & subnet) == subnet) {
                            client_id = buffer[19] - 2;
                        } else {
                            client_id = super_client;
                        }
                    } else if(client_ip->ip_v == 6) {
                        client_id = buffer[39] - 2;
                    }
                    if(client_id >= 0 && client_id < CLIENT_SIZE && clients[client_id].idle != 0){
                        encrypt(buffer, nread);
                        sendto(sockfd, buffer, nread, 0, (struct sockaddr *)&clients[client_id].addr, clients[client_id].slen);
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
                            if(j == super_client) {
                                super_client = -1;
                            }
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
