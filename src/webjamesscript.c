/*
	$Id: webjamesscript.c,v 1.1 2002/02/17 22:50:10 ajw Exp $
	Handler for WebJames style CGI scripts
*/

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

#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#include "webjames.h"
#include "wjstring.h"
#include "webjamesscript.h"
#include "openclose.h"
#include "report.h"
#include "stat.h"
#include "ip.h"
#include "write.h"


#ifdef WEBJAMESSCRIPT

void webjamesscript_start(struct connection *conn)
/* start a 'webjames-style' cgi-script */
{
	int size;
	char *ptr;
	void *blk;
	wimp_t handle;

	size = conn->headersize ;
	if (conn->bodysize > 0)  size += conn->bodysize;
	if (xosmodule_alloc(size+16, &blk)) {
		/* failed to allocate rma-block */
		report_badrequest(conn, "cannot allocate memory");
		return;
	}
	ptr = blk;
	
#ifdef MemCheck_MEMCHECK
	MemCheck_RegisterMiscBlock(ptr,size+16);
#endif

	memcpy(ptr, conn->header, conn->headersize);
	if (conn->bodysize > 0)
		memcpy(ptr + conn->headersize, conn->body, conn->bodysize);

	/* start cgi-script */
	snprintf(temp, TEMPBUFFERSIZE,
			"*%s -http %d -socket %d -remove -size %d -rma %d -bps %d -port %d -host %d.%d.%d.%d",
			conn->filename, conn->httpmajor*10+conn->httpminor, (int)conn->socket, size,
			(int)ptr, configuration.bandwidth/100, conn->port, conn->ipaddr[0], conn->ipaddr[1],
			conn->ipaddr[2], conn->ipaddr[3]);
	if (conn->method == METHOD_HEAD)
		wjstrncat(temp, " -head", TEMPBUFFERSIZE);
	else if (conn->method == METHOD_POST)
		wjstrncat(temp, " -post", TEMPBUFFERSIZE);
	else if (conn->method == METHOD_DELETE)
		wjstrncat(temp, " -delete", TEMPBUFFERSIZE);
	else if (conn->method == METHOD_PUT)
		wjstrncat(temp, " -put", TEMPBUFFERSIZE);

	if (conn->pwd) {
		*strchr(conn->authorization, ':') = '\0';
		wjstrncat(temp, " -user ", TEMPBUFFERSIZE);
		wjstrncat(temp, conn->authorization, TEMPBUFFERSIZE);
	}

	webjames_writelog(LOGLEVEL_CGISTART, temp);

	if (E(xwimp_start_task(temp, &handle))) {
		/* failed to start cgi-script */
		xosmodule_free(ptr);
		report_badrequest(conn, "script couldn't be started");
		return;
	}

#ifdef MemCheck_MEMCHECK
	MemCheck_UnRegisterMiscBlock(ptr);
#endif

	/* cgi-script started ok, so let the cgi-script close the socket */
	if (fd_is_set(serverinfo.select_read, conn->socket)) {
		fd_clear(serverinfo.select_read, conn->socket);
		serverinfo.readcount--;
	}
	if (fd_is_set(serverinfo.select_write, conn->socket)) {
		fd_clear(serverinfo.select_write, conn->socket);
		serverinfo.writecount--;
	}
	conn->socket = socket_CLOSED;
	conn->flags.is_cgi = 1;
	conn->close(conn, 0);
}

#endif
