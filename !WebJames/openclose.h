struct connection *create_conn(void);
void open_connection(int socket, char *host, int port);
void close(int cn, int force);
