#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "cache.h"
#include "webjames.h"
#include "ip.h"
#include "attributes.h"
#include "stat.h"
#include "openclose.h"
#include "resolve.h"


struct connection *create_conn(void) {
/* create and empty connection structure */

	struct connection *conn;

	conn = malloc(sizeof(struct connection));

	if (!conn) return NULL;

	conn->bodysize = conn->headersize = -1;
	conn->used = conn->fileinfo.size = conn->fileused = 0;
	conn->if_modified_since.tm_year = -1;
	/* various header lines */
	conn->uri = conn->body = conn->accept = conn->acceptcharset = conn->acceptencoding = conn->acceptlanguage = conn->header = NULL;
	conn->requestline = conn->type = conn->authorization = NULL;
	conn->useragent = conn->referer = conn->cookie = NULL;
	conn->requesturi = NULL;
	/* other stuff */
	conn->filebuffer = NULL;
	conn->timeoflastactivity = clock();
	conn->cache = NULL;
	conn->file = NULL;
	conn->vary[0]='\0';
	/* attributes */
	conn->homedir = configuration.site;
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

	conn->flags.releasefilebuffer = 0;
	conn->flags.deletefile = 0;
	conn->flags.cacheable = 1;
	conn->flags.is_cgi = 0;
	conn->flags.setcsd = 0;
	conn->flags.stripextensions = 0;
	conn->flags.multiviews = 0;
	conn->statuscode = HTTP_OK;
	conn->starttime = clock();

	return conn;
}


void open_connection(int socket, char *host, int port) {
/* called when an incoming connection has been accepted */
/* find an empty entry, and use that for the connection */

/* socket           socket number */
/* host             pointer to host structure as returned by accept() */
/* port             port number */

	struct connection *conn;

	conn = create_conn();

	/* no empty entries */
	if (!conn) {
		/* can't use report_busy() as we haven't allocated a connection-structure */
		writestring(socket, "HTTP/1.0 503 Server is busy\r\nContent-type: text/html\r\n\r\n<html><head><title>Server is busy</title><body><h1>The server is busy</h1>Please try again later...</body></html>");
		ip_close(socket);
		return;
	}

#ifdef LOG
	sprintf(temp, "OPEN request from %d.%d.%d.%d", host[4], host[5], host[6], host[7]);
	writelog(LOGLEVEL_OPEN, temp);
#endif

	conn->index = activeconnections;
	connections[activeconnections++] = conn;

	/* update statistics */
	statistics.access++;

	/* set up for reading the header */
	conn->status = WJ_STATUS_HEADER;
	fd_set(select_read, socket);
	readcount++;

	/* fill in the structure */
	conn->socket = socket;
	conn->port = port;
	conn->ipaddr[0] = host[4];
	conn->ipaddr[1] = host[5];
	conn->ipaddr[2] = host[6];
	conn->ipaddr[3] = host[7];

	/* default name of the remote host is the ip-address */
	sprintf(conn->host, "%d.%d.%d.%d", host[4], host[5], host[6], host[7]);
	if (configuration.reversedns >= 0) {
		conn->dnsstatus = DNS_TRYING;
		conn->dnsendtime = 0x7fffffff;      /* indefinately! */
		dnscount++;
		resolver_poll(conn);                /* in case the name is cached */
	} else
		conn->dnsstatus = DNS_FAILED;
}


void close(int cn, int force) {
/* close a connection; if dns lookup still in progress then try again later */
/* this funcion may be called twice, first when reading/writing is finished */
/* and again later when dns is done */

/* cn               connection entry */
/* force            set to 1 to to full close (including dns) */
	struct connection *conn;
	struct errordoc *errordocs;
	int socket, i, clk;

	conn = connections[cn];
	clk = clock();

	/* close socket and make sure it is no longer 'selected' */
	socket = conn->socket;
	if (conn->socket != -1) {
		if (fd_is_set(select_read, socket)) {
			fd_clear(select_read, socket);
			readcount--;
		}
		if (fd_is_set(select_write, socket)) {
			fd_clear(select_write, socket);
			writecount--;
		}
		ip_close(conn->socket);
		conn->socket = -1;
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
			sprintf(temp, "CLOSE %s", ptr);
		else if (bps < 4000)
			sprintf(temp, "CLOSE %s (%d bytes/sec)", ptr, bps);
		else
			sprintf(temp, "CLOSE %s (%d kb/sec)", ptr, bps>>10);
	} else
		sprintf(temp, "CLOSE %s", conn->uri);
	writelog(LOGLEVEL_OPEN, temp);
#endif

	/* close/release/reset everything that isn't needed for the clf-log */
	if (conn->header)         free(conn->header);
	if (conn->body)           free(conn->body);
	if (conn->type)           free(conn->type);
	if (conn->accept)         free(conn->accept);
	if (conn->acceptlanguage) free(conn->acceptlanguage);
	if (conn->acceptcharset)  free(conn->acceptcharset);
	if (conn->acceptencoding) free(conn->acceptencoding);
	if (conn->uri)            free(conn->uri);
	if (conn->cookie)         free(conn->cookie);
	if (conn->requesturi)     free(conn->requesturi);
	if (conn->authorization)  free(conn->authorization);
	if ((conn->filebuffer) && (conn->flags.releasefilebuffer)) free(conn->filebuffer);
	conn->filebuffer = conn->body = conn->header = NULL;
	conn->type = conn->accept =  conn->uri =  conn->requesturi = conn->cookie = NULL;
	conn->authorization = NULL;

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

	if (conn->dnsstatus == DNS_TRYING) {
		/* if we're still trying to do reverse dns... */
		if (force) {
			/* abort reverse dns */
			conn->dnsstatus = DNS_FAILED;
			dnscount--;
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
	if (!conn->flags.is_cgi)  clf_connection_closed(cn);

	/* close/release/reset everything that couldn't be released until */
	/* after the clf-log had been written */
	if (conn->requestline)  free(conn->requestline);
	if (conn->useragent)    free(conn->useragent);
	if (conn->referer)      free(conn->referer);

	free((char *)conn);

	activeconnections--;
	/* make sure it's the bottom entries in the stack that are used */
	connections[cn] = connections[activeconnections];
	connections[activeconnections] = NULL;

	for (i = 0; i < activeconnections; i++)  connections[i]->index = i;
}
