#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/osmodule.h"
#include "oslib/osfscontrol.h"
#include "oslib/osfile.h"
#include "oslib/territory.h"

#include "cache.h"
#include "webjames.h"
#include "ip.h"
#include "stat.h"
#include "report.h"
#include "write.h"
#include "read.h"
#include "openclose.h"
#include "attributes.h"
#include "resolve.h"
#include "handler.h"
#include "content.h"


/* configuration */
struct config configuration;

/* connection */
struct globalserverinfo serverinfo;
static int quitwhenidle;
struct connection *connections[MAXCONNECTIONS];

/* various */
char temp[HTTPBUFFERSIZE];
static int serverscount;


/* ------------------------------------------------ */
/* init, kill, poll */

int webjames_init(char *config) {
/* initialise the webserver */
/* config           name of configuration file */
/* returns 1 (ok) or 0 (failed) */

	int i, arg, active;

	quitwhenidle = 0;

	/* default configuration */
	serverscount = 0;

	configuration.timeout = 200;
	configuration.bandwidth = 0;
	configuration.reversedns = -1;
	configuration.casesensitive=0;
	*configuration.clflog = *configuration.weblog = *configuration.webmaster = *configuration.site = *configuration.serverip = '\0';
	*configuration.put_script = *configuration.delete_script = *configuration.htaccessfile = '\0';
	*configuration.cgi_in = *configuration.cgi_out;
	configuration.numimagedirs=0;
	configuration.syslog=0;
	configuration.loglevel = 5;
	configuration.log_close_delay = 10; /* seconds */
	configuration.log_max_copies = 0;
	configuration.logbuffersize = 4096;

	configuration.maxrequestsize = 100000;
	configuration.xheaders = 0;
	configuration.logheaders = 0;
	strcpy(configuration.server, WEBJAMES_SERVER_SOFTWARE);
	configuration.webjames_h_revision=WEBJAMES_H_REVISION; /*used by PHP module to ensure that it was compiled with the correct version of webjames.h */
	read_config(config);

	/* create out directory in !Scrap (or wherever) */
	xosfile_create_dir("<WebJames$Scrap>",0);
	/* Supply defaults for cgi_in/out if the user hasn't specified them */
	/* They must be canonicalised if possible as some programs (eg Perl) don't like <foo$dir> as arguments */
	if (*configuration.cgi_in=='\0') {
		if (xosfscontrol_canonicalise_path("<WebJames$Scrap>.In",configuration.cgi_in,NULL,NULL,256,NULL)) strcpy(configuration.cgi_in,"<WebJames$Scrap>.In");
	}
	if (*configuration.cgi_out=='\0') {
		if (xosfscontrol_canonicalise_path("<WebJames$Scrap>.Out",configuration.cgi_out,NULL,NULL,256,NULL)) strcpy(configuration.cgi_out,"<WebJames$Scrap>.Out");
	}

	if ((*configuration.site == '\0') || (serverscount == 0) || (configuration.timeout < 0))
		return 0;

	/* reset everything */
	for (i = 0; i < MAXCONNECTIONS; i++)  connections[i] = NULL;

	for (i = 0; i < REPORTCACHECOUNT; i++)  reports[i].report = -1;

	for (i = 0; i < 32; i++) {
		serverinfo.select_read[i] = 0;
		serverinfo.select_write[i] = 0;
		serverinfo.select_except[i] = 0;
	}
	serverinfo.readcount = serverinfo.writecount = serverinfo.dnscount = 0;
	serverinfo.activeconnections = 0;

	/* reset statistics */
	init_statistics();

	/* create/reset cache */
	init_cache(NULL);

	/* initialise content negotiation*/
	content_init();

	/* slowdown is a delay (in cs) that is adjusted when the bandwidth */
	/* exceeds the max allowed bandwidth */
	serverinfo.slowdown = 0;

	/* initilise any handler modules */
	if (!init_handlers()) {
		webjames_writelog(LOGLEVEL_ALWAYS, "Couldn't initialise handlers...");
		return 0;
	}

	init_attributes(configuration.attributesfile);

#define SOCKETOPT_REUSEADDR   4

	for (i = 0; i < serverscount; i++) {
		socket_s listen;
		serverinfo.servers[i].socket = socket_CLOSED;

		/* start listening */
		listen = ip_create(0);
		if (listen == socket_CLOSED) {
			webjames_writelog(LOGLEVEL_ALWAYS, "Couldn't create socket...");
			continue;
		}


		ip_setsocketopt(listen, SOCKETOPT_REUSEADDR, &arg, 4);

		ip_linger(listen, 10*60);

		if (!ip_bind(listen, 0, serverinfo.servers[i].port)) {
			webjames_writelog(LOGLEVEL_ALWAYS, "Couldn't bind to port %d...", serverinfo.servers[i].port);
			ip_close(listen);
			continue;
		}

		if (!ip_listen(listen)) {
			webjames_writelog(LOGLEVEL_ALWAYS, "Couldn't listen on socket %d...", listen);
			ip_close(listen);
			continue;
		}
		webjames_writelog(LOGLEVEL_ALWAYS, "LISTEN on port %d on socket %d", serverinfo.servers[i].port, listen);

		serverinfo.servers[i].socket = listen;
	}

	active = 0;
	for (i = 0; i < serverscount; i++)
		if (serverinfo.servers[i].socket != socket_CLOSED)  active++;
	if (active == 0)  return 0;

	return 1;
}



void webjames_command(char *cmd, int release) {
/* parse command from external program */

/* cmd              command-string */
/* release          when == 1, SYS OS_Module,7,,cmd is called on exit */

	if (strcmp(cmd, "closeall") == 0) {                       /* CLOSEALL */
		int i;
		for (i = 0; i < serverinfo.activeconnections; i++)
			if (connections[i]->socket != socket_CLOSED)  connections[i]->close(connections[i], 1);
		webjames_writelog(LOGLEVEL_CMD, "ALL CONNECTIONS CLOSED");

	} else if (strncmp(cmd, "flushcache", 10) == 0) {         /* FLUSHCACHE */
		flushcache(NULL);
		report_flushcache();
		webjames_writelog(LOGLEVEL_CMD, "Cache has been flushed");

	} else if (strncmp(cmd, "quitwhenidle", 12) == 0) {       /* QUITWHENIDLE */
		quitwhenidle = 1;

	} else if (strncmp(cmd, "log ", 4) == 0) {                /* LOG */
		if (cmd[4])  webjames_writelog(LOGLEVEL_CMD, cmd+4);

	} else if (strncmp(cmd, "clf ", 4) == 0) {                /* CLF */
		int bytes, code;
		char host[256];
		char *request;

		if (sscanf(cmd+4, "%d %d %s ", &code, &bytes, host) == 3) {
			if ((code > 0) && (code < 1000) && (bytes >= 0)) {
				request = strchr(cmd, '%');
				if (request) {
					request++;
					while (*request == ' ')  request++;
					clf_cgi_finished(code, bytes, host, request);
				}
			}
		}

	}

	if (release)  xosmodule_free(cmd);
}


void webjames_kill() {
/* stop webserver */

	int i;

	/* update statistics */
	update_statistics();

	quit_handlers();
	

	/* close all open sockets */
	for (i = 0; i < serverinfo.activeconnections; i++)
		if (connections[i]->socket != socket_CLOSED)  connections[i]->close(connections[i], 1);

	close_log();

	/* close all servers */
	for (i = 0; i < serverscount; i++)
		if (serverinfo.servers[i].socket != socket_CLOSED)
			ip_close(serverinfo.servers[i].socket);
	serverscount = 0;

	kill_cache();
}



int webjames_poll() {
/* called repeatedly to perform all the webserver operations */

/* returns polldelay (cs) or -1 to quit */

	socket_s socket;
	int cs, i, srv;
	char host[16], select[32];

	/* are there any reverse-dns lookups in progress? */
	if (serverinfo.dnscount) {

		/* attempt to reverse dns the connections */
		for (i = 0; i < serverinfo.activeconnections; i++)
			if (connections[i]->dnsstatus == DNS_TRYING)
				resolver_poll(connections[i]);
	}

	/* are we reading from any sockets? */
	if (serverinfo.readcount) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = ((int *)serverinfo.select_read)[i];
		if (ip_select(256, select, NULL, NULL) > 0) {
			for (i = 0; i < serverinfo.activeconnections; i++) {
				if ((connections[i]->status == WJ_STATUS_HEADER) ||
						(connections[i]->status == WJ_STATUS_BODY))     pollread(i);
			}
		}
	}

	/* are we writing to any sockets? */
	if (serverinfo.writecount) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = ((int *)serverinfo.select_write)[i];
		if (ip_select(256, NULL, select, NULL) > 0) {
			for (i = 0; i < serverinfo.activeconnections; i++)
				if (connections[i]->status == WJ_STATUS_WRITING)
					pollwrite(i);
		}
	}

	if (serverinfo.readcount + serverinfo.writecount) {
		/* check if any connections have timed out */
		int clk;
		clk = clock();
		for (i = 0; i < serverinfo.activeconnections; i++) {
			if ( ((connections[i]->status == WJ_STATUS_HEADER)  ||
						(connections[i]->status == WJ_STATUS_BODY)    ||
						(connections[i]->status == WJ_STATUS_WRITING)) &&
						(clk > connections[i]->timeoflastactivity + 100*configuration.timeout) )
							connections[i]->close(connections[i], 1);
		}
	}

	xos_read_monotonic_time(&statistics.currenttime);

	/* calculate serverinfo.slowdown */
	if ((statistics.currenttime > statistics.adjusttime) && (configuration.bandwidth > 0)) {
		int used, secs, bytes;

		statistics.adjusttime = statistics.currenttime + 25;
		bytes = statistics.written + statistics.written2;
		secs = (statistics.currenttime - statistics.time)/100 + 60;
		used = 60*bytes / secs;
		if (used > configuration.bandwidth)
			serverinfo.slowdown += 5;
		else if (used < configuration.bandwidth/2)
			serverinfo.slowdown = 0;
		else if (serverinfo.slowdown > 5)
			serverinfo.slowdown -= 5;
		if (serverinfo.slowdown > 20)  serverinfo.slowdown = 20;
	}

	/* update statistics every minute */
	if (statistics.currenttime > statistics.time + 90*100) {
		update_statistics();
		statistics.time = statistics.currenttime;
	}

	/* if all slots are used, new connections cannot be accepted; also, use */
	/* as much CPU as possible to clear the backlog */
	if (serverinfo.activeconnections == MAXCONNECTIONS)  return serverinfo.slowdown;

	/* are there any new connections? */
	for (srv = 0; srv < serverscount; srv++) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = 0;
		select[(int)serverinfo.servers[srv].socket>>3] |= 1 << ((int)serverinfo.servers[srv].socket &7);

		do {
			socket = socket_CLOSED;
			if (ip_select(256, select, NULL, NULL) > 0) {
				socket = ip_accept(serverinfo.servers[srv].socket, host);
				if (socket != socket_CLOSED) {
					ip_nonblocking(socket);
					open_connection(socket, host, serverinfo.servers[srv].port);
				}
			}
		} while (socket != socket_CLOSED);
	}


	if ((serverinfo.activeconnections == 0) && (quitwhenidle)) return -1;

	/* calc when to return */
	cs = 10;
	if (serverinfo.activeconnections)      cs = 1;
	if (serverinfo.activeconnections > 1)  cs = 0;
	return cs+serverinfo.slowdown;
}


/* ------------------------------------------------ */

void abort_reverse_dns(struct connection *conn, int newstatus)
/* stop trying to lookup the address */
/* if reading/writing has already been finished, close the connection */
/* conn             connection structure */
/* newstatus        new dns status */
{
	conn->dnsstatus = newstatus;
	serverinfo.dnscount--;
	if (conn->status == WJ_STATUS_DNS)   conn->close(conn, 1);
}


/* ------------------------------------------------ */


int webjames_writestring(struct connection *conn, char *string)
/* write a string to a socket (and block until it is sent), and update statistics */
/* return bytes written (length of string) or -1 if error */
{
	os_error *err;
	int len, written=0;

	len = strlen(string);
	while (len>0) {
		written = ip_write(conn->socket, string, len, &err);
		statistics.written += written;
		len-=written;
		if (err) {
			if (err->errnum==socket_EPIPE) {
				webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
			} else {
				webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s",err->errmess);
			}
			conn->close(conn,1);
			len=written=-1;
		}
	}
	return written;
}

int webjames_writebuffer(struct connection *conn, char *buffer, int size)
/* write a block of memory to a socket, and update statistics */
/* return bytes written or -1 if error */
{
	os_error *err;

	size = ip_write(conn->socket, buffer, size, &err);
	statistics.written += size;
	if (err) {
		if (err->errnum==socket_EPIPE) {
			webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
		} else {
			webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s",err->errmess);
		}
		conn->close(conn,1);
		size=-1;
	}
	return size;
}

int webjames_readbuffer(struct connection *conn, char *buffer, int size)
/* read from a socket to a block of memory */
/* return bytes read or -1 if error */
{
	os_error *err;

	size = ip_read(conn->socket, buffer, size, &err);
	if (err) {
		if (err->errnum==socket_EPIPE) {
			webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
		} else {
			webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s",err->errmess);
		}
		conn->close(conn,1);
		size=-1;
	}
	return size;
}

void webjames_writelog(int level, char *fmt, ...)
{
#ifdef LOG
	va_list ap;
	char temp[256];

	va_start(ap,fmt);
	vsprintf(temp,fmt,ap);
	va_end(ap);

	writelog(level, temp);
#endif
}

void read_config(char *config)
/* read all config-options from the config file */
/* config           configuration file name */
{
	FILE *file;
	char *cmd, *val;
	char line[256];

	file = fopen(config, "r");
	if (!file)  return;
	do {
		if (!fgets(line, 256, file))  break;          /* read line */
		cmd = line;
		/* skip whitespace */
		while (isspace(*cmd)) cmd++;
		/* remove trailing whitespace */
		val = cmd+strlen(cmd);
		while (val>cmd && isspace(*(val-1))) val--;
		*val = '\0';

		val = cmd;
		/* find end of command */
		while (*val!='\0' && *val!='=' && !isspace(*val)) val++;
		if (*val == '\0') {
			val=NULL;
		} else {
			*val = '\0';
			val++;
		}
		/* find start of value */
		if (val) while (*val!='\0' && (*val=='=' || isspace(*val))) val++;

		if ((*cmd != '#') && (*cmd != '\0') && (val)) {
			if (strcmp(cmd, "port") == 0) {
				do {
					int i, unused, port;

					port = atoi(val);
					unused = 1;                             /* check if port already used */
					for (i = 0; i < serverscount; i++)
						if (serverinfo.servers[i].port == port)  unused = 0;

					if ((unused) && (port > 0) && (port <= 65535)) { /* unused port, so grab it! */
						serverinfo.servers[serverscount].port = port;
						serverscount++;
					}

					if (strchr(val, ','))
						val = strchr(val, ',') + 1;
					else
						val = NULL;

				} while ((val) && (serverscount < 8));

			} else if (strcmp(cmd, "bandwidth") == 0)
				configuration.bandwidth = atoi(val);

			else if (strcmp(cmd, "timeout") == 0)
				configuration.timeout = atoi(val);

			else if (strcmp(cmd, "cachesize") == 0)
				configuration.cachesize = atoi(val);

			else if (strcmp(cmd, "maxcachefilesize") == 0)
				configuration.maxcachefilesize = atoi(val);

			else if (strcmp(cmd, "keep-log-open") == 0)
				configuration.log_close_delay = atoi(val);

			else if (strcmp(cmd, "logbuffersize") == 0)
				configuration.logbuffersize = atoi(val);

			else if (strcmp(cmd, "syslog") == 0)
				configuration.syslog = atoi(val);

			else if (strcmp(cmd, "log-rotate") == 0) {
				int age, size, copies;
				if (sscanf(val, "%d %d %d", &age, &size, &copies)  == 3) {
					if ((age > 0) && (age < 24*365) && (size >= 0) && (copies >= 0)) {
						configuration.log_max_age = age;
						configuration.log_max_size = size;
						configuration.log_max_copies = copies;
					}
				}

			} else if (strcmp(cmd, "clf-rotate") == 0) {
				int age, size, copies;
				if (sscanf(val, "%d %d %d", &age, &size, &copies)  == 3) {
					if ((age > 0) && (age < 24*365) && (size >= 0) && (copies >= 0)) {
						configuration.clf_max_age = age;
						configuration.clf_max_size = size;
						configuration.clf_max_copies = copies;
					}
				}

			} else if (strcmp(cmd, "readaheadbuffer") == 0) {
				configuration.readaheadbuffer = atoi(val);
				if (configuration.readaheadbuffer < 8)   configuration.readaheadbuffer = 8;
				if (configuration.readaheadbuffer > 64)  configuration.readaheadbuffer = 64;

			} else if (strcmp(cmd, "maxrequestsize") == 0) {
				configuration.maxrequestsize = atoi(val);
				if (configuration.maxrequestsize < 16)    configuration.maxrequestsize = 16;
				if (configuration.maxrequestsize > 2000)  configuration.maxrequestsize = 2000;

			} else if (strcmp(cmd, "server") == 0)
				strcpy(configuration.server, val);

			else if (strcmp(cmd, "clf") == 0)
				strcpy(configuration.clflog, val);

			else if (strcmp(cmd, "log") == 0)
				strcpy(configuration.weblog, val);

			else if (strcmp(cmd, "attributes") == 0)
				strcpy(configuration.attributesfile, val);

			else if (strcmp(cmd, "loglevel") == 0)
				configuration.loglevel = atoi(val);

			else if (strcmp(cmd, "rename-cmd") == 0)
				strcpy(configuration.rename_cmd, val);

			else if (strcmp(cmd, "homedir") == 0) {
				/* canonicalise the path, so <WebJames$Dir> etc are expanded, otherwise they can cause problems */
				if (xosfscontrol_canonicalise_path(val,configuration.site,NULL,NULL,255,NULL) != NULL) strcpy(configuration.site,val);
				
			} else if (strcmp(cmd, "put-script") == 0)
				strncpy(configuration.put_script, val, 255);

			else if (strcmp(cmd, "delete-script") == 0)
				strncpy(configuration.delete_script, val, 255);

			else if (strcmp(cmd, "cgi-input") == 0)
				strncpy(configuration.cgi_in, val, 255);

			else if (strcmp(cmd, "cgi-output") == 0)
				strncpy(configuration.cgi_out, val, 255);

			else if (strcmp(cmd, "accessfilename") == 0)
				strncpy(configuration.htaccessfile, val, 255);

			else if (strcmp(cmd, "casesensitive") == 0)
				configuration.casesensitive = atoi(val);

			else if (strcmp(cmd, "panic") == 0)
				strncpy(configuration.panic, val, 500);

			else if (strcmp(cmd, "serverip") == 0)
				strncpy(configuration.serverip, val, 255);

			else if (strcmp(cmd, "reversedns") == 0)
				configuration.reversedns = atoi(val);

			else if (strcmp(cmd, "webmaster") == 0)
				strcpy(configuration.webmaster, val);

			else if ((strcmp(cmd, "x-header") == 0) && (configuration.xheaders < 16)) {
				configuration.xheader[configuration.xheaders] = malloc(strlen(val)+1);
				if (configuration.xheader[configuration.xheaders])  strcpy(configuration.xheader[configuration.xheaders++], val);
			}

			else if ((strcmp(cmd, "log-header") == 0) && (configuration.logheaders < 16)) {
				configuration.logheader[configuration.logheaders] = malloc(strlen(val)+1);
				if (configuration.logheader[configuration.logheaders]) {
					char *src=val,*dest=configuration.logheader[configuration.logheaders++];
					do {
						*dest=toupper(*(src++));
					} while (*(dest++) != '\0');
				}
			}

			else if (strcmp(cmd, "imagedirs") == 0) {
				char *s, *f;

				s=f=val;
				while (*s && configuration.numimagedirs<MAX_IMAGEDIRS) {
					int notfound = 0;
					int filetype;

					while (*f && !isspace(*f) && *f!=',') f++;
					if (*f) *f++='\0';
					while (isspace(*f)) f++;

					if (strcmp(s,"ALL") == 0) {
						filetype = filetype_ALL;
					} else {
						if (xosfscontrol_file_type_from_string(s,(bits*)&filetype)) notfound = 1; /* ignore unknown filetypes */
					}
					if (!notfound) configuration.imagedirs[configuration.numimagedirs++] = filetype;
					s=f;
				}
			}
		}
	} while (!feof(file));
	fclose(file);
	/* change homedir to lowercase if needed */
	/* we can't do this when homedir is defined as the casesensitive option might not have been read at that time */
	if (!configuration.casesensitive) {
		char *ptr = configuration.site;
		while (*ptr) {
			*ptr = tolower(*ptr);
			ptr++;
		}
	}
}

