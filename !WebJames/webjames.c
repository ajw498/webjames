#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/osmodule.h"
#include "oslib/osfscontrol.h"
#include "oslib/territory.h"

#include "global.h"
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


/* configuration */
struct config configuration;

/* connection */
struct serverinfo servers[8];
int readcount, writecount, dnscount, slowdown;
int activeconnections;
static quitwhenidle;
struct connection *connections[MAXCONNECTIONS];
char select_read[32], select_write[32], select_except[32];

/* various */
char line[HTTPBUFFERSIZE], temp[HTTPBUFFERSIZE];
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
	*configuration.clflog = *configuration.weblog = *configuration.webmaster = *configuration.site = *configuration.serverip = *configuration.cgi_in = *configuration.cgi_out = '\0';
	*configuration.put_script = *configuration.delete_script = *configuration.htaccessfile = '\0';
	configuration.loglevel = 5;
	configuration.log_close_delay = 10; /* seconds */
	configuration.log_max_copies = 0;
	configuration.logbuffersize = 4096;

	configuration.maxrequestsize = 100000;
	configuration.xheaders = 0;
	configuration.logheaders = 0;
	strcpy(configuration.server, "WebJames");
	read_config(config);
	if ((*configuration.site == '\0') || (serverscount == 0) || (configuration.timeout < 0))
		return 0;

	/* reset everything */
	for (i = 0; i < MAXCONNECTIONS; i++)  connections[i] = NULL;

	for (i = 0; i < REPORTCACHECOUNT; i++)  reports[i].report = -1;

	for (i = 0; i < 32; i++) {
		select_read[i] = 0;
		select_write[i] = 0;
		select_except[i] = 0;
	}
	readcount = writecount = dnscount = 0;
	activeconnections = 0;

	/* reset statistics */
	init_statistics();

	/* create/reset cache */
	init_cache(NULL);

	/* slowdown is a delay (in cs) that is adjusted when the bandwidth */
	/* exceeds the max allowed bandwidth */
	slowdown = 0;

	/* initilise any handler modules */
	init_handlers();

	init_attributes(configuration.attributesfile);

#define SOCKETOPT_REUSEADDR   4

	for (i = 0; i < serverscount; i++) {
		int listen;
		servers[i].socket = -1;

		/* start listening */
		listen = ip_create(0);
		if (listen < 0) {
#ifdef LOG
			writelog(LOGLEVEL_ALWAYS, "Couldn't create socket...");
#endif
			continue;
		}


		ip_setsocketopt(listen, SOCKETOPT_REUSEADDR, &arg, 4);

		ip_linger(listen, 10*60);

		if (!ip_bind(listen, 0, servers[i].port)) {
#ifdef LOG
			sprintf(temp, "Couldn't bind to port %d...", servers[i].port);
			writelog(LOGLEVEL_ALWAYS, temp);
#endif
			ip_close(listen);
			continue;
		}

		if (!ip_listen(listen)) {
#ifdef LOG
			sprintf(temp, "Couldn't listen on socket %d...", listen);
			writelog(LOGLEVEL_ALWAYS, temp);
#endif
			ip_close(listen);
			continue;
		}
#ifdef LOG
		sprintf(temp, "LISTEN on port %d on socket %d", servers[i].port, listen);
		writelog(LOGLEVEL_ALWAYS, temp);
#endif

		servers[i].socket = listen;
	}

	active = 0;
	for (i = 0; i < serverscount; i++)
		if (servers[i].socket != -1)  active++;
	if (active == 0)  return 0;

	return 1;
}



void webjames_command(char *cmd, int release) {
/* parse command from external program */

/* cmd              command-string */
/* release          when == 1, SYS OS_Module,7,,cmd is called on exit */

	if (strcmp(cmd, "closeall") == 0) {                       /* CLOSEALL */
		int i;
		for (i = 0; i < activeconnections; i++)
			if (connections[i]->socket >= 0)  close(i, 1);
#ifdef LOG
		writelog(LOGLEVEL_CMD, "ALL CONNECTIONS CLOSED");
#endif

	} else if (strncmp(cmd, "flushcache", 10) == 0) {         /* FLUSHCACHE */
		flushcache(NULL);
		report_flushcache();
#ifdef LOG
		writelog(LOGLEVEL_CMD, "Cache has been flushed");
#endif

	} else if (strncmp(cmd, "quitwhenidle", 12) == 0) {       /* QUITWHENIDLE */
		quitwhenidle = 1;

	} else if (strncmp(cmd, "log ", 4) == 0) {                /* LOG */
#ifdef LOG
		if (cmd[4])  writelog(LOGLEVEL_CMD, cmd+4);
#endif

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
	
	kill_cache();

	/* close all open sockets */
	for (i = 0; i < activeconnections; i++)
		if (connections[i]->socket >= 0)  close(i, 1);

#ifdef LOG
	closelog();
#endif

	/* close all servers */
	for (i = 0; i < serverscount; i++)
		if (servers[i].socket >= 0)
			ip_close(servers[i].socket);
	serverscount = 0;
}



int webjames_poll() {
/* called repeatedly to perform all the webserver operations */

/* returns polldelay (cs) or -1 to quit */

	int socket, cs, i, srv;
	char host[16], select[32];

	/* are there any reverse-dns lookups in progress? */
	if (dnscount) {

		/* attempt to reverse dns the connections */
		for (i = 0; i < activeconnections; i++)
			if (connections[i]->dnsstatus == DNS_TRYING)
				resolver_poll(connections[i]);
	}

	/* are we reading from any sockets? */
	if (readcount) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = ((int *)select_read)[i];
		if (ip_select(256, select, NULL, NULL) > 0) {
			for (i = 0; i < activeconnections; i++) {
				if ((connections[i]->status == WJ_STATUS_HEADER) ||
						(connections[i]->status == WJ_STATUS_BODY))     pollread(i);
			}
		}
	}

	/* are we writing to any sockets? */
	if (writecount) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = ((int *)select_write)[i];
		if (ip_select(256, NULL, select, NULL) > 0) {
			for (i = 0; i < activeconnections; i++)
				if (connections[i]->status == WJ_STATUS_WRITING)
					pollwrite(i);
		}
	}

	if (readcount + writecount) {
		/* check if any connections have timed out */
		int clk;
		clk = clock();
		for (i = 0; i < activeconnections; i++) {
			if ( ((connections[i]->status == WJ_STATUS_HEADER)  ||
						(connections[i]->status == WJ_STATUS_BODY)    ||
						(connections[i]->status == WJ_STATUS_WRITING)) &&
						(clk > connections[i]->timeoflastactivity + 100*configuration.timeout) )
							close(i, 1);
		}
	}

	xos_read_monotonic_time(&statistics.currenttime);

	/* calculate slowdown */
	if ((statistics.currenttime > statistics.adjusttime) && (configuration.bandwidth > 0)) {
		int used, secs, bytes;

		statistics.adjusttime = statistics.currenttime + 25;
		bytes = statistics.written + statistics.written2;
		secs = (statistics.currenttime - statistics.time)/100 + 60;
		used = 60*bytes / secs;
		if (used > configuration.bandwidth)
			slowdown += 5;
		else if (used < configuration.bandwidth/2)
			slowdown = 0;
		else if (slowdown > 5)
			slowdown -= 5;
		if (slowdown > 20)  slowdown = 20;
	}

	/* update statistics every minute */
	if (statistics.currenttime > statistics.time + 90*100) {
		update_statistics();
		statistics.time = statistics.currenttime;
	}

	/* if all slots are used, new connections cannot be accepted; also, use */
	/* as much CPU as possible to clear the backlog */
	if (activeconnections == MAXCONNECTIONS)  return slowdown;

	/* are there any new connections? */
	for (srv = 0; srv < serverscount; srv++) {
		for (i = 0; i < 8; i++)  ((int *)select)[i] = 0;
		select[servers[srv].socket>>3] |= 1 << (servers[srv].socket &7);

		do {
			socket = -1;
			if (ip_select(256, select, NULL, NULL) > 0) {
				socket = ip_accept(servers[srv].socket, host);
				if (socket >= 0) {
					ip_nonblocking(socket);
					open_connection(socket, host, servers[srv].port);
				}
			}
		} while (socket >= 0);
	}


	if ((activeconnections == 0) && (quitwhenidle))  quit = 1;

	/* calc when to return */
	cs = 10;
	if (activeconnections)      cs = 1;
	if (activeconnections > 1)  cs = 0;
	return cs+slowdown;
}


/* ------------------------------------------------ */

void abort_reverse_dns(struct connection *conn, int newstatus) {
/* stop trying to lookup the address */
/* if reading/writing has already been finished, close the connection */

/* conn             connection structure */
/* newstatus        new dns status */

	conn->dnsstatus = newstatus;
	dnscount--;
	if (conn->status == WJ_STATUS_DNS)   close(conn->index, 1);
}


/* ------------------------------------------------ */


void writestring(int socket, char *string) {
/* write a string to a socket */

	statistics.written += strlen(string);
	ip_writestring(socket, string);
}


void read_config(char *config) {
/* read all config-options from the config file */
/* config           configuration file name */

	FILE *file;
	char *cmd, *val;

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
						if (servers[i].port == port)  unused = 0;

					if ((unused) && (port > 0) && (port <= 65535)) { /* unused port, so grab it! */
						servers[serverscount].port = port;
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

#ifdef ANTRESOLVER
			else if (strcmp(cmd, "reversedns") == 0)
				configuration.reversedns = atoi(val);
#endif

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


/* ------------------------------------------- */

int compare_time(struct tm *time1, struct tm *time2) {
/* compare to tm structures */
/* return either -1 (time1<time2), 0 (time1==time2) or 1 (time1>time2) */
	if (time1->tm_year < time2->tm_year)  return -1;
	if (time1->tm_year > time2->tm_year)  return 1;
	if (time1->tm_mon  < time2->tm_mon)   return -1;
	if (time1->tm_mon  > time2->tm_mon)   return 1;
	if (time1->tm_mday < time2->tm_mday)  return -1;
	if (time1->tm_mday > time2->tm_mday)  return 1;
	if (time1->tm_hour < time2->tm_hour)  return -1;
	if (time1->tm_hour > time2->tm_hour)  return 1;
	if (time1->tm_min  < time2->tm_min)   return -1;
	if (time1->tm_min  > time2->tm_min)   return 1;
	if (time1->tm_sec  < time2->tm_sec)   return -1;
	if (time1->tm_sec  > time2->tm_sec)   return 1;
	return 0;
}


void utc_to_time(char *utc, struct tm *time) {
/* converts 5 byte UTC to tm structure */

/* utc              char[5] holding the utc value */
/* time             tm structure to fill in */
	int ordinals[9];

	if (xterritory_convert_time_to_utc_ordinals((os_date_and_time *)utc, (territory_ordinals *)ordinals));
	time->tm_sec   = ordinals[1];
	time->tm_min   = ordinals[2];
	time->tm_hour  = ordinals[3];
	time->tm_mday  = ordinals[4];
	time->tm_mon   = ordinals[5];
	time->tm_year  = ordinals[6] - 1900;
	time->tm_wday  = ordinals[7];
	time->tm_yday  = ordinals[8];
	time->tm_isdst = 0;
}


void rfc_to_time(char *rfc, struct tm *time) {
/* converts a RFC timestring to a tm structure */

/* rfc              pointer to string */
/* time             tm structure to fill in */
	char *start, days[12];
	int date, month, year, hour, min, sec, letter1, letter2, letter3;
	int wday, yday, m;

	/* array holding (the number days in the month)-28 */
	strcpy(days, "303232332323");

	date = 0;
	month = 6;
	year = 1998;
	hour = 0;
	min = 0;
	sec = 0;
	wday = 0;
	yday = 0;

	start = rfc;
	while (*start == ' ')  start++;
	letter1 = toupper(start[0]);
	letter2 = toupper(start[1]);
	letter3 = toupper(start[2]);

	if (letter1 == 'M')
		wday = 0;
	else if ((letter1 == 'T') && (letter2 == 'U'))
		wday = 1;
	else if (letter1 == 'W')
		wday = 2;
	else if (letter1 == 'T')
		wday = 3;
	else if (letter1 == 'F')
		wday = 4;
	else if ((letter1 == 'S') && (letter2 == 'A'))
		wday = 5;
	else if (letter1 == 'S')
		wday = 6;

	start = strchr(rfc, ',');
	if (start == NULL)  start = rfc;

	while ((!isdigit(*start)) && (*start))  start++;
	if (!(*start))  return;
	date = atoi(start) - 1;

	while ((!isalpha(*start)) && (*start))  start++;
	if (!(*start))  return;

	letter1 = toupper(start[0]);
	letter2 = toupper(start[1]);
	letter3 = toupper(start[2]);
	if ((letter1 == 'J') && (letter2 == 'A'))
		month = 0;
	else if (letter1 == 'F')
		month = 1;
	else if ((letter1 == 'M') && (letter3 == 'R'))
		month = 2;
	else if ((letter1 == 'A') && (letter2 == 'P'))
		month = 3;
	else if (letter1 == 'M')
		month = 4;
	else if ((letter1 == 'J') && (letter3 == 'N'))
		month = 5;
	else if (letter1 == 'J')
		month = 6;
	else if (letter1 == 'A')
		month = 7;
	else if (letter1 == 'S')
		month = 8;
	else if (letter1 == 'O')
		month = 9;
	else if (letter1 == 'N')
		month = 10;
	else if (letter1 == 'D')
		month = 11;

	while ((!isdigit(*start)) && (*start))  start++;
	if (!(*start))  return;

	year = atoi(start);
	if (year < 40)
		year += 2000;
	else if (year < 100)
		year += 1900;
	if (year < 1990)  year = 1990;

	while ((*start != ' ') && (*start))  start++;
	if (!(*start))  return;

	hour = atoi(start);
	start = strchr(start, ':');
	if (start == NULL)  return;
	start++;

	min = atoi(start);
	start = strchr(start, ':');
	if (start == NULL)  return;
	sec = atoi(start+1);

	/* adjust the days[] array if it's a leap year */
	if ((year%4 == 0) && ((year%100 != 0) || (year%400 == 0)) )
		days[1] = '1';

	yday = date;
	for (m = 0; m < month; m++)
		yday += 28+days[m]-'0';

	time->tm_sec = sec;
	time->tm_min = min;
	time->tm_hour = hour;
	time->tm_mday = date;
	time->tm_mon = month;
	time->tm_year = year-1900;
	time->tm_wday = wday;
	time->tm_yday = yday;
	time->tm_isdst = 0;
}


void time_to_rfc(struct tm *time, char *out) {
/* converts a tm structure to a RFC timestring */

/* time             tm structure */
/* out              char-array to hold result, at least 26 bytes */

	strftime(out, 25, "%a, %d %b %Y %H:%M:%S", time);
}


