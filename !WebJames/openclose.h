#ifndef OPENCLOSE_H
#define OPENCLOSE_H

#include "webjames.h"

void close_real(struct connection *conn, int force);
void close_connection(struct connection *conn, int force, int real);

struct connection *create_conn(void);
void open_connection(int socket, char *host, int port);

#endif
