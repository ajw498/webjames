
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "oslib/socket.h"
#include "ip.h"


int ip_create(int udp) {
/* returns a socket or -1 (failed) */

	socket_s socket;

	if (udp) {
		if (xsocket_creat(socket_AF_INET, socket_SOCK_DGRAM, socket_IPPROTO_IP, &socket))  return -1;
	} else {
		if (xsocket_creat(socket_AF_INET, socket_SOCK_STREAM, socket_IPPROTO_IP, &socket))  return -1;
	}
	return (int)socket;
}


int ip_bind(int socket, int addr, int port) {
/* returns 0 (failed) or 1 (ok) */
	socket_sockaddr_in sockaddr;
	int i, p;

	p = (port &255)<<8;
	p |= port>>8;

	sockaddr.af = 2;
	sockaddr.port = p;
	sockaddr.addr = addr;
	for (i = 0; i < 8; i++)  sockaddr.data[i] = 0;

	if (xsocket_bind((socket_s)socket, (socket_sockaddr *)&sockaddr, 16))
		return 0;
	return 1;
}


void ip_close(int socket) {

	xsocket_close((socket_s)socket);
}


int ip_read(int socket, char *buffer, int size) {
/* returns number of bytes read or -1 */
	int read;
	if (xsocket_read((socket_s)socket, (byte *)buffer, size, &read))
		return -1;
	return read;
}


int ip_write(int socket, char *buffer, int size) {
/* returns the number of bytes written or -errorcode */
	int written;

	if (xsocket_write((socket_s)socket, (byte *)buffer, size, &written))
		return -written;
	return written;
}


int ip_writestring(int socket, char *string) {

	return ip_write(socket, string, strlen(string));
}


int ip_nonblocking(int socket) {
/* returns 0 (failed) or 1 (ok) */

	int arg;

	arg = 1;
	if (xsocket_ioctl((socket_s)socket, 0x8004667e, (byte *)&arg))  return 0;
	return 1;
}


int ip_ready(int socket) {
/* returns number of bytes available on the socket */

	int arg;

	if (xsocket_ioctl((socket_s)socket, 0x4004667f, (byte *)&arg))  return -1;
	return arg;
}


int ip_linger(int socket, int secs) {
/* returns 0 (failed) or 1 (ok) */
	int arg[2];

	arg[0] = 1;
	arg[1] = secs;
	return ip_setsocketopt(socket, 0x80, &arg, 8);
}



int ip_setsocketopt(int socket, int opt, void *arg, int argsize) {

	if (xsocket_setsockopt((socket_s)socket, 0xffff, (socket_so)opt, arg, argsize))  return 0;
	return 1;
}



int ip_select(int max, char *read, char *write, char *except) {
/* returns 0 (nothing) or -1 (error) or count */
	socket_timeval timeout;
	int found;

	timeout.sec = 0;
	timeout.usec = 0;

	if (xsocket_select(max, (void *)read, (void *)write, (void *)except,
						&timeout, &found))  return -1;

	return found;
}



int ip_listen(int socket) {
/* returns 0 (failed) or 1 (ok) */
	if (xsocket_listen((socket_s)socket, 8))  return 0;
	return 1;
}


int ip_accept(int socket, char *host) {
/* returns a socket or -1 (failed) */
	int size;
	socket_s sock;

	if (xsocket_accept((socket_s)socket, (socket_sockaddr *)host, &size, &sock))    return -1;
	return (int)sock;;
}


void fd_clear(char *select, int socket) {
	select[socket>>3] &= ~(1<< (socket &7));
}

void fd_set(char *select, int socket) {
	select[socket>>3] |= 1<< (socket &7);
}

int fd_is_set(char *select, int socket) {
	return select[socket>>3] & (1<< (socket &7));
}

