/*
	$Id: resolve.c,v 1.1 2002/02/17 22:50:10 ajw Exp $
	Reverse DNS
*/

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "kernel.h"

#include "oslib/socket.h"

#include "webjames.h"
#include "wjstring.h"
#include "stat.h"
#include "resolve.h"
#include "ip.h"

#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif


void resolver_poll(struct connection *conn)
{
	char **ip;
	int dnsstat;
	_kernel_swi_regs regs;

	if (configuration.reversedns>=0) {
		regs.r[0]=(int)conn->host;
		if (E((os_error*)_kernel_swi(Resolver_GetHost, &regs, &regs))) {
			abort_reverse_dns(conn, DNS_FAILED);
		} else {
			dnsstat = regs.r[0];
			ip = (char **)regs.r[1];
			switch (dnsstat) {
				case socket_EINPROGRESS:
					/* still trying, so check timeout */
					if (clock() > conn->dnsendtime)  abort_reverse_dns(conn, DNS_FAILED);
					break;
				case 0:
					/* lookup successfully completed */
#ifdef MemCheck_MEMCHECK
					MemCheck_RegisterMiscBlock_Ptr(ip);
					MemCheck_RegisterMiscBlock_String(ip[0]);
#endif
					webjames_writelog(LOGLEVEL_DNS, "DNS %s is %s", conn->host, ip[0]);

					wjstrncpy(conn->host, ip[0], 127);
#ifdef MemCheck_MEMCHECK
					MemCheck_UnRegisterMiscBlock(ip[0]);
					MemCheck_UnRegisterMiscBlock(ip);
#endif
					abort_reverse_dns(conn, DNS_OK);
					break;
				case -1:
					webjames_writelog(LOGLEVEL_DNS, "DNS %s host not found", conn->host);
					abort_reverse_dns(conn, DNS_FAILED);
					break;
				default:
					webjames_writelog(LOGLEVEL_DNS, "DNS failure");
					abort_reverse_dns(conn, DNS_FAILED);
					break;
			}
		}
	} else {
		abort_reverse_dns(conn, DNS_FAILED);
	}
}
