/*
	$Id: ip.c,v 1.6 2001/09/02 19:00:45 AJW Exp $
	Socket access
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "kernel.h"

#include "oslib/socket.h"

#include "ip.h"
#include "webjames.h"

socket_s ip_create(int udp)
/* returns a socket or -1 (failed) */
{
	socket_s socket;

	if (udp) {
		if (xsocket_creat(socket_AF_INET, socket_SOCK_DGRAM, socket_IPPROTO_IP, &socket))  return socket_CLOSED;
	} else {
		if (xsocket_creat(socket_AF_INET, socket_SOCK_STREAM, socket_IPPROTO_IP, &socket))  return socket_CLOSED;
	}
	return socket;
}


int ip_bind(socket_s socket, int addr, int port)
/* returns 0 (failed) or 1 (ok) */
{
	socket_sockaddr_in sockaddr;
	int i, p;

	p = (port &255)<<8;
	p |= port>>8;

	sockaddr.af = 2;
	sockaddr.port = p;
	sockaddr.addr = addr;
	for (i = 0; i < 8; i++)  sockaddr.data[i] = 0;

	if (xsocket_bind(socket, (socket_sockaddr *)&sockaddr, 16))
		return 0;
	return 1;
}


void ip_close(socket_s socket)
{
	xsocket_close(socket);
}


int ip_read(socket_s socket, char *buffer, int size, os_error **err)
/* returns number of bytes read */
/* err is non-null if there was an error */
{
	int read;
	_kernel_swi_regs regs;

	regs.r[0]=(int)socket;
	regs.r[1]=(int)buffer;
	regs.r[2]=size;
	*err=(os_error *)_kernel_swi(Socket_Read,&regs,&regs);
	read=regs.r[0];

	if (*err) {
		if ((*err)->errnum==socket_EWOULDBLOCK) {
			*err=NULL;
		} else {
			read=0;
		}
	}
	return read;
}


int ip_write(socket_s socket, char *buffer, int size, os_error **err)
/* returns the number of bytes written */
/* err is non-null if there was an error */
{
	int written;
	_kernel_swi_regs regs;

	regs.r[0]=(int)socket;
	regs.r[1]=(int)buffer;
	regs.r[2]=size;
	*err=(os_error *)_kernel_swi(Socket_Write,&regs,&regs);
	written=regs.r[0];

	if (*err) {
		if ((*err)->errnum==socket_EWOULDBLOCK) {
			*err=NULL;
		} else {
			written=0;
		}
	}
	return written;
}


int ip_nonblocking(socket_s socket)
/* returns 0 (failed) or 1 (ok) */
{
	int arg;

	arg = 1;
	if (xsocket_ioctl(socket, socket_FIONBIO, (byte *)&arg))  return 0;
	return 1;
}


int ip_ready(socket_s socket)
/* returns number of bytes available on the socket */
{
	int arg;

	if (xsocket_ioctl(socket, socket_FIONREAD, (byte *)&arg))  return -1;
	return arg;
}


int ip_linger(socket_s socket, int secs)
/* returns 0 (failed) or 1 (ok) */
{
	int arg[2];

	arg[0] = 1;
	arg[1] = secs;
	return ip_setsocketopt(socket, socket_SO_LINGER, &arg, 8);
}



int ip_setsocketopt(socket_s socket, socket_so opt, void *arg, int argsize)
{
	if (xsocket_setsockopt(socket, socket_SOL_SOCKET, opt, arg, argsize))  return 0;
	return 1;
}



int ip_select(int max, char *read, char *write, char *except)
/* returns 0 (nothing) or -1 (error) or count */
{
	socket_timeval timeout;
	int found;

	timeout.sec = 0;
	timeout.usec = 0;

	if (xsocket_select(max, (socket_fdset *)read, (socket_fdset *)write, (socket_fdset *)except,
						&timeout, &found))  return -1;

	return found;
}



int ip_listen(socket_s socket)
/* returns 0 (failed) or 1 (ok) */
{
	if (xsocket_listen(socket, 8))  return 0;
	return 1;
}


socket_s ip_accept(socket_s socket, char *host)
/* returns a socket or -1 (failed) */
{
	int size=16;
	socket_s sock;

	if (xsocket_accept(socket, (socket_sockaddr *)host, &size, &sock))    return socket_CLOSED;
	return sock;
}


void fd_clear(char *select, socket_s sock)
{
	int socket=(int)sock;
	select[socket>>3] &= ~(1<< (socket &7));
}

void fd_set(char *select, socket_s sock)
{
	int socket=(int)sock;
	select[socket>>3] |= 1<< (socket &7);
}

int fd_is_set(char *select, socket_s sock)
{
	int socket=(int)sock;
	return select[socket>>3] & (1<< (socket &7));
}

