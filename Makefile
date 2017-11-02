CC=gcc
CFLAGS=-Wall -Wextra -O2
prefix=/usr

all: nudpn_server nudpn_client

clean:
	rm -f *.o
	rm -f nudpn_server nudpn_client

die.o: die.c die.h
	${CC} -c die.c ${CFLAGS}

epoll.o: epoll.c epoll.h
	${CC} -c epoll.c ${CFLAGS}

tun.o: tun.c tun.h
	${CC} -c tun.c ${CFLAGS}

server.o: server.c config.h
	${CC} -c server.c ${CFLAGS}

client.o: client.c config.h
	${CC} -c client.c ${CFLAGS}

crypt.o: crypt.c
	${CC} -c crypt.c ${CFLAGS}

nudpn_server: tun.o epoll.o die.o crypt.o server.o
	${CC} -o nudpn_server tun.o epoll.o die.o crypt.o server.o ${CFLAGS}

nudpn_client: tun.o epoll.o die.o crypt.o client.o
	${CC} -o nudpn_client tun.o epoll.o die.o crypt.o client.o ${CFLAGS}


install: all
	install -m 0755 nudpn_server $(prefix)/bin
	install -m 0755 nudpn_client $(prefix)/bin

