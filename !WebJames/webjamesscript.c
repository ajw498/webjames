#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/osfscontrol.h"
#include "oslib/osmodule.h"
#include "oslib/wimp.h"

#include "webjames.h"
#include "webjamesscript.h"
#include "openclose.h"
#include "report.h"
#include "stat.h"
#include "ip.h"
#include "write.h"




void webjamesscript_start(struct connection *conn)
/* start a 'webjames-style' cgi-script */
{
	int size;
	char *ptr;
	wimp_t handle;

	size = conn->headersize ;
	if (conn->bodysize > 0)  size += conn->bodysize;
	if (xosmodule_alloc(size+16, (void **)&ptr)) {
		/* failed to allocate rma-block */
		report_badrequest(conn, "cannot allocate memory");
		return;
	}
	
#ifdef MemCheck_MEMCHECK
	MemCheck_RegisterMiscBlock(ptr,size+16);
#endif

	memcpy(ptr, conn->header, conn->headersize);
	if (conn->bodysize > 0)
		memcpy(ptr + conn->headersize, conn->body, conn->bodysize);

	/* start cgi-script */
	sprintf(temp,
			"*%s -http %d -socket %d -remove -size %d -rma %d -bps %d -port %d -host %d.%d.%d.%d",
			conn->filename, conn->httpmajor*10+conn->httpminor, conn->socket, size,
			(int)ptr, configuration.bandwidth/100, conn->port, conn->ipaddr[0], conn->ipaddr[1],
			conn->ipaddr[2], conn->ipaddr[3]);
	if (conn->method == METHOD_HEAD)
		strcat(temp, " -head");
	else if (conn->method == METHOD_POST)
		strcat(temp, " -post");
	else if (conn->method == METHOD_DELETE)
		strcat(temp, " -delete");
	else if (conn->method == METHOD_PUT)
		strcat(temp, " -put");

	if (conn->pwd) {
		*strchr(conn->authorization, ':') = '\0';
		strcat(temp, " -user ");
		strcat(temp, conn->authorization);
	}

#ifdef LOG
	writelog(LOGLEVEL_CGISTART, temp);
#endif

	if (xwimp_start_task(temp, &handle)) {
		/* failed to start cgi-script */
		xosmodule_free(ptr);
		report_badrequest(conn, "script couldn't be started");
		return;
	}

#ifdef MemCheck_MEMCHECK
	MemCheck_UnRegisterMiscBlock(ptr);
#endif

	/* cgi-script started ok, so let the cgi-script close the socket */
	if (fd_is_set(select_read, conn->socket)) {
		fd_clear(select_read, conn->socket);
		readcount--;
	}
	if (fd_is_set(select_write, conn->socket)) {
		fd_clear(select_write, conn->socket);
		writecount--;
	}
	conn->socket = -1;
	conn->flags.is_cgi = 1;
	conn->close(conn, 0);
}

