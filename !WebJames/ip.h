
#define IPERR_BROKENPIPE      32
#define IPERR_WOULDBLOCK      35
#define IPERR_INPROGRESS      36

#define IPADDR_FAMILY 0
#define IPADDR_PORT   2
#define IPADDR_ADDR   4
#define IPADDR_DUMMY  8


void ip_close(int socket);
int ip_create(int udp);
int ip_bind(int socket, int addr, int port);
int ip_connect(int socket, int addr, int port);
int ip_read(int socket, char *buffer, int size);
int ip_write(int socket, char *buffer, int size);
int ip_nonblocking(int socket);
int ip_resolve(char *host, int *addr);
void ip_writeaddr(char *ip, int addr, int port);
int ip_setsocketopt(int socket, int opt, void *arg, int argsize);
int ip_linger(int socket, int secs);
int ip_listen(int socket);
int ip_ready(int socket);
int ip_accept(int socket, char *host);
int ip_select(int max, char *read, char *write, char *except);

void fd_clear(char *select, int socket);
void fd_set(char *select, int socket);
int fd_is_set(char *select, int socket);
