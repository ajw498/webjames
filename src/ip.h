
#include "oslib/socket.h"

#ifndef socket_EPIPE
#define socket_EPIPE 0x20u
#endif

#define socket_CLOSED ( (socket_s) -1 )

#define IPADDR_FAMILY 0
#define IPADDR_PORT   2
#define IPADDR_ADDR   4
#define IPADDR_DUMMY  8


void ip_close(socket_s socket);
socket_s ip_create(int udp);
int ip_bind(socket_s socket, int addr, int port);
int ip_connect(socket_s socket, int addr, int port);
int ip_read(socket_s socket, char *buffer, int size, os_error **err);
int ip_write(socket_s socket, const char *buffer, int size, os_error **err);
int ip_nonblocking(socket_s socket);
int ip_resolve(char *host, int *addr);
void ip_writeaddr(char *ip, int addr, int port);
int ip_setsocketopt(socket_s socket, int opt, void *arg, int argsize);
int ip_linger(socket_s socket, int secs);
int ip_listen(socket_s socket);
int ip_ready(socket_s socket, os_error **err);
socket_s ip_accept(socket_s socket, char *host);
int ip_select(int max, char *read, char *write, char *except);

void fd_clear(char *select, socket_s socket);
void fd_set(char *select, socket_s socket);
int fd_is_set(char *select, socket_s socket);
