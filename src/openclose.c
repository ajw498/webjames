/*
	$Id$
	Open and close connections
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "cache.h"
#include "ip.h"
#include "attributes.h"
#include "stat.h"
#include "openclose.h"
#include "resolve.h"


struct connection *create_conn(void)
/* create and empty connection structure */
{
	struct connection *conn;

	conn = EM(malloc(sizeof(struct connection)));

	if (!conn) return NULL;

	conn->parent = NULL;
	conn->bodysize = conn->headersize = -1;
	conn->used = conn->fileinfo.size = conn->fileused = 0;
	conn->if_modified_since.tm_year = -1;
	conn->protocol="HTTP/1.0";
	/* various header lines */
	conn->uri = conn->body = conn->accept = conn->acceptcharset = conn->acceptencoding = conn->acceptlanguage = conn->header = NULL;
	conn->requestline = conn->type = conn->authorization = NULL;
	conn->useragent = conn->referer = conn->cookie = conn->host = NULL;
	conn->requesturi = conn->header = conn->args = NULL;
	/* other stuff */
	conn->filebuffer = NULL;
	conn->timeoflastactivity = clock();
	conn->cache = NULL;
	conn->file = NULL;
	conn->vary[0]='\0';
	conn->contentlanguage = conn->contentlocation = NULL;
	conn->vhost = NULL;
	/* attributes */
	conn->homedir = NULL;
	conn->accessfile = conn->userandpwd = conn->realm = NULL;
	conn->moved = conn->tempmoved = NULL;
	conn->defaultfiles = NULL;
	conn->defaultfilescount = 0;
	conn->attrflags.accessallowed = 1;
	conn->attrflags.hidden = 0;
	conn->cgi_api = CGI_API_WEBJAMES;
	conn->homedirignore = 0;
	conn->allowedfiletypescount=0;
	conn->forbiddenfiletypescount=0;
	conn->errordocs = NULL;
	conn->handlers = NULL;
	conn->handler = NULL;
	conn->overridefilename = NULL;
	conn->regexmatch = NULL;

	conn->flags.releasefilebuffer = 0;
	conn->flags.outputheaders = 1;
	conn->flags.deletefile = 0;
	conn->flags.cacheable = 1;
	conn->flags.is_cgi = 0;
	conn->flags.setcsd = 0;
	conn->flags.autoindex = 0;
	conn->flags.mimeuseext = 0;
	conn->flags.stripextensions = 0;
	conn->flags.multiviews = 0;
	conn->statuscode = HTTP_OK;
	conn->starttime = clock();

	conn->close=close_real;
	conn->handlerinfo=NULL;
	conn->memlist=NULL;

	return conn;
}


void open_connection(socket_s socket, char *host, int port)
/* called when an incoming connection has been accepted */
/* find an empty entry, and use that for the connection */
/* socket           socket number */
/* host             pointer to host structure as returned by accept() */
/* port             port number */
{
	struct connection *conn;

	conn = create_conn();

	/* no empty entries */
	if (!conn) {
		/* can't use report_busy() as we haven't allocated a connection-structure */
		char *mess="HTTP/1.0 503 Server is busy\r\nContent-type: text/html\r\n\r\n<html><head><title>Server is busy</title><body><h1>The server is busy</h1>Please try again later...</body></html>";
		os_error *err;

		ip_write(socket, mess, strlen(mess), &err);
		if (err) webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s",err->errmess);
		ip_close(socket);
		return;
	}

	webjames_writelog(LOGLEVEL_OPEN, "OPEN request from %d.%d.%d.%d", host[4], host[5], host[6], host[7]);

	conn->index = serverinfo.activeconnections;
	connections[serverinfo.activeconnections++] = conn;

	/* update statistics */
	statistics.access++;

	/* set up for reading the header */
	conn->status = WJ_STATUS_HEADER;
	ip_fd_set(serverinfo.select_read, socket);
	serverinfo.readcount++;

	/* fill in the structure */
	conn->socket = socket;
	conn->port = port;
	conn->ipaddr[0] = host[4];
	conn->ipaddr[1] = host[5];
	conn->ipaddr[2] = host[6];
	conn->ipaddr[3] = host[7];

	/* default name of the remote host is the ip-address */
	snprintf(conn->remotehost, MAX_HOSTNAME, "%d.%d.%d.%d", host[4], host[5], host[6], host[7]);
	if (configuration.reversedns >= 0) {
		conn->dnsstatus = DNS_TRYING;
		conn->dnsendtime = 0x7fffffff;      /* indefinately! */
		serverinfo.dnscount++;
		resolver_poll(conn);                /* in case the name is cached */
	} else
		conn->dnsstatus = DNS_FAILED;
}

void close_connection(struct connection *conn, int force, int real) {
/* close a connection; if dns lookup still in progress then try again later */
/* this funcion may be called twice, first when reading/writing is finished */
/* and again later when dns is done */

/* cn               connection entry */
/* force            set to 1 to to full close (including dns) */
/* real             set if this is a real connection rather than just a connection struct*/
	int cn=conn->index;
	struct errordoc *errordocs;
	socket_s socket;
	int i, clk;
	struct memlist *mem;

	clk = clock();

	if (real) {
		/* close socket and make sure it is no longer 'selected' */
		socket = conn->socket;
		if (conn->socket != socket_CLOSED) {
			if (ip_fd_is_set(serverinfo.select_read, socket)) {
				ip_fd_clear(serverinfo.select_read, socket);
				serverinfo.readcount--;
			}
			if (ip_fd_is_set(serverinfo.select_write, socket)) {
				ip_fd_clear(serverinfo.select_write, socket);
				serverinfo.writecount--;
			}
			ip_close(conn->socket);
			conn->socket = socket_CLOSED;
		}

#ifdef LOG
		if (clk > conn->starttime) {
			int bps;
			char dummy[1], *ptr;
			if (conn->uri)
				ptr = conn->uri;
			else {
				dummy[0] = '\0';
				ptr = dummy;
			}
			bps = 100*conn->fileinfo.size/(clk-conn->starttime);
			if (bps <= 0)
				webjames_writelog(LOGLEVEL_OPEN, "CLOSE %s", ptr);
			else if (bps < 4000)
				webjames_writelog(LOGLEVEL_OPEN,"CLOSE %s (%d bytes/sec)", ptr, bps);
			else
				webjames_writelog(LOGLEVEL_OPEN,"CLOSE %s (%d kb/sec)", ptr, bps>>10);
		} else {
			webjames_writelog(LOGLEVEL_OPEN, "CLOSE %s", conn->uri);
		}
#endif
	}

	/* close/release/reset everything that isn't needed for the clf-log */
	if (conn->header)         free(conn->header);
	if (conn->body)           free(conn->body);
	if (conn->type)           free(conn->type);
	if (conn->accept)         free(conn->accept);
	if (conn->acceptlanguage) free(conn->acceptlanguage);
	if (conn->acceptcharset)  free(conn->acceptcharset);
	if (conn->acceptencoding) free(conn->acceptencoding);
	if (conn->uri)            free(conn->uri);
	if (conn->host)           free(conn->host);
	if (conn->cookie)         free(conn->cookie);
	if (conn->requesturi)     free(conn->requesturi);
	if (conn->authorization)  free(conn->authorization);
	if (conn->contentlocation)  free(conn->contentlocation);
	if (conn->contentlanguage)  free(conn->contentlanguage);
	if (conn->regexmatch)       free(conn->regexmatch);
	if ((conn->filebuffer) && (conn->flags.releasefilebuffer)) free(conn->filebuffer);
	conn->filebuffer = conn->body = conn->header = conn->type = NULL;
	conn->accept = conn->acceptlanguage = conn->acceptcharset = conn->acceptencoding = NULL;
	conn->uri = conn->host = conn->requesturi = conn->cookie = conn->authorization = NULL;
	conn->contentlocation = conn->contentlanguage = NULL;
	conn->regexmatch = NULL;

	mem = conn->memlist;
	while (mem) {
		struct memlist *next = mem->next;

		free(mem);
		mem = next;
	}
	conn->memlist = NULL;

	/* free the linked list of custom error documents */
	errordocs = conn->errordocs;
	while (errordocs) {
		struct errordoc *next;

		next = errordocs->next;
		free(errordocs);
		errordocs = next;
	}
	conn->errordocs = NULL;

	if (conn->file)    fclose(conn->file);
	conn->file = NULL;

	if (conn->cache) {
	/* if the file was cached, decrement the access count */
		cache_release_file(conn->cache);
		conn->cache = NULL;
	}

	/* free any memory allocated by the handler */
	if (conn->handlerinfo) {
		free(conn->handlerinfo);
		conn->handlerinfo=NULL;
	}

	if (real && conn->dnsstatus == DNS_TRYING) {
		/* if we're still trying to do reverse dns... */
		if (force) {
			/* abort reverse dns */
			conn->dnsstatus = DNS_FAILED;
			serverinfo.dnscount--;
		} else {
			/* do only reverse dns */
			conn->status = WJ_STATUS_DNS;
			conn->dnsendtime = clk + 100*configuration.reversedns;
			return;
		}
	}

	/* delete file if necessary (output from cgi-script) */
	if (conn->flags.deletefile)  remove(conn->filename);

	/* write clf-log */
	if (real && !conn->flags.is_cgi)  clf_connection_closed(cn);

	/* close/release/reset everything that couldn't be released until */
	/* after the clf-log had been written */
	if (conn->requestline)  free(conn->requestline);
	if (conn->useragent)    free(conn->useragent);
	if (conn->referer)      free(conn->referer);

	free(conn);

	if (real) {
		serverinfo.activeconnections--;
		/* make sure it's the bottom entries in the stack that are used */
		connections[cn] = connections[serverinfo.activeconnections];
		connections[serverinfo.activeconnections] = NULL;

		for (i = 0; i < serverinfo.activeconnections; i++)  connections[i]->index = i;
	}
}

void close_real(struct connection *conn, int force)
{
	close_connection(conn,force,1);
}

