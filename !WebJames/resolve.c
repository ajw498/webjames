#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "stat.h"
#include "webjames.h"
#include "resolve.h"
#include "ip.h"

#ifdef ANTRESOLVER
#include "swis.h"
#endif



void resolver_poll(struct connection *conn) {

#ifdef ANTRESOLVER
	char **ip;
	int dnsstat;

	if (_swix(Resolver_GetHost, _IN(0)|_OUT(0)|_OUT(1), conn->host, &dnsstat, &ip))
		abort_reverse_dns(conn, DNS_FAILED);
	else if (dnsstat == IPERR_INPROGRESS) {
		/* still trying, so check timeout */
		if (clock() > conn->dnsendtime)  abort_reverse_dns(conn, DNS_FAILED);
	} else {
		/* lookup successfully completed */
#ifdef MemCheck_MEMCHECK
		MemCheck_RegisterMiscBlock_Ptr(ip);
		MemCheck_RegisterMiscBlock_String(ip[0]);
#endif
#ifdef LOG
		sprintf(temp, "DNS %s is %s", conn->host, ip[0]);
		writelog(LOGLEVEL_DNS, temp);
#endif
		strncpy(conn->host, ip[0], 127);
#ifdef MemCheck_MEMCHECK
		MemCheck_UnRegisterMiscBlock(ip[0]);
		MemCheck_UnRegisterMiscBlock(ip);
#endif
		abort_reverse_dns(conn, DNS_OK);
	}
#else

	abort_reverse_dns(conn, DNS_FAILED);
#endif

}
