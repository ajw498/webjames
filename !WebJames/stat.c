#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SYSLOG
#include "swis.h"
#endif

#include "oslib/os.h"

#include "webjames.h"
#include "stat.h"
#include "ip.h"




struct stats statistics;


#ifndef SYSLOG
static char logclock[32];
static int logclocklen;
static int logupdatetime, logrotatetime;       /* clock()/100 values */
static FILE *logfile = NULL;
static int logbufferused=0;
static char *logbuffer=NULL;
#endif

static int clfupdatetime;
static int clfrotatetime;       /* clock()/100 values */
static FILE *clffile = NULL;
/*int clf_max_age, clf_max_size;
int clf_max_copies = 0;*/
static int clfbufferused = 0;
static char *clfbuffer = NULL;


void init_statistics() {
/* reset statistics */

	/* statistics logging start time (real time) */
	statistics.starttime = time(NULL);
	/* statistics logging start time (relative time) */
	xos_read_monotonic_time(&statistics.time);
	/* only adjust slowdown every 25 cs */
	statistics.adjusttime = statistics.time - 25;
	statistics.written = 0;
	statistics.access = 0;
	statistics.written2 = configuration.bandwidth;
	statistics.access2 = 0;

	clfupdatetime = -1;           /* clock() at last update */
	configuration.clf_max_age *= 3600;          /* max age in secs */
	if (configuration.clf_max_size <= 0)
		configuration.clf_max_size = 0x7f000000;  /* no limit to the size */
	else
		configuration.clf_max_size *= 1024;

	clfrotatetime = clock()/100 + configuration.clf_max_age;        /* clock()/100 value */

#ifndef SYSLOG
	logupdatetime = -1;           /* clock()/100 at last update */
	configuration.log_max_age *= 3600;          /* max age in secs */
	if (configuration.log_max_size <= 0)
		configuration.log_max_size = 0x7f000000;  /* no limit to the size */
	else
		configuration.log_max_size *= 1024;

	logrotatetime = clock()/100 + configuration.log_max_age;        /* clock()/100 value */

	if (configuration.logbuffersize > 0) logbuffer = malloc(configuration.logbuffersize); /* if malloc fails then don't use a buffer */
	logbufferused = 0;
#endif

	if (configuration.logbuffersize > 0) clfbuffer = malloc(configuration.logbuffersize); /* if malloc fails then don't use a buffer */
	clfbufferused = 0;

	if ((configuration.loglevel < 0) || (configuration.loglevel > 10))
		configuration.loglevel = 5;
	if ((configuration.log_close_delay < 1) || (configuration.log_close_delay > 3600))
		configuration.log_close_delay = 90;

}

static void rotate_log(char *logfile, int copies) {

	int f;
	char oldname[256], newname[256], cmd[256], *ptr;

	if (copies > 1) {
		/* remove the oldest log file */
		sprintf(oldname, "%s_%d", logfile, copies-1);
		remove(oldname);
		/* rotate the rest */
		for (f = copies-2; f > 0; f--) {
			sprintf(oldname, "%s_%d", logfile, f);
			sprintf(newname, "%s_%d", logfile, f+1);
			rename(oldname, newname);
		}
		/* rename the newest */
		if (*configuration.rename_cmd) {
			int read, write, len;
			len = strlen(configuration.rename_cmd);
			write = 0;
			for (read = 0; read < len; read++) {
				cmd[write] = configuration.rename_cmd[read];
				if (configuration.rename_cmd[read] == '%') {
					if (configuration.rename_cmd[read+1] == '%') {
						cmd[++write] = '%';
						read++;
					} else if (configuration.rename_cmd[read+1] == '0') {
						strcpy(cmd+write, logfile);
						write += strlen(logfile)-1;
						read++;
					} else if (configuration.rename_cmd[read+1] == '1') {
						sprintf(cmd+write, "%s_1", logfile);
						write = strlen(cmd)-1;
						read++;
					} else if (configuration.rename_cmd[read+1] == '2') {
						ptr = logfile;
						while (strchr(ptr, '.'))  ptr = strchr(ptr, '.') + 1;
						while (strchr(ptr, ':'))  ptr = strchr(ptr, ':') + 1;
						strcpy(cmd+write, ptr);
						write = strlen(cmd)-1;
						read++;
					}
				}
				write++;
			}
			cmd[write] = '\0';
			system(cmd);
		} else {
			sprintf(newname, "%s_1", logfile);
			rename(logfile, newname);
		}
	}

	remove(logfile);
}

#ifndef SYSLOG
static void write_buffer(void)
{
	if (logfile == NULL) logfile = fopen(configuration.weblog, "a+");
	if (logfile == NULL) logfile = fopen(configuration.weblog, "w");
	if (logfile) fwrite(logbuffer,1,logbufferused,logfile);
	logbufferused = 0;
}
#endif

static void write_clfbuffer(void)
{
	if (clffile == NULL) clffile = fopen(configuration.clflog, "a+");
	if (clffile == NULL) clffile = fopen(configuration.clflog, "w");
	if (clffile) fwrite(clfbuffer,1,clfbufferused,clffile);
	clfbufferused = 0;
}


void update_statistics() {

	int clk;

	clk = clock()/100;

	if (clk > clfupdatetime + configuration.log_close_delay) {
		if (clfbufferused) write_clfbuffer();
		if (clffile) {
			fclose(clffile);
			clffile = NULL;
		}
	}
#ifndef SYSLOG
	if (clk > logupdatetime + configuration.log_close_delay) {
		if (logbufferused) write_buffer();
		if (logfile) {
			fclose(logfile);
			logfile = NULL;
		}
	}
#endif

	statistics.written2 =  statistics.written;
	statistics.access2 =  statistics.access;
	statistics.written = statistics.access = 0;
}

#ifndef SYSLOG
static void check_for_rotate(void)
/* Should only be called when logfile is already open */
{
	if (configuration.log_max_copies > 0) {
		if ( (logupdatetime > logrotatetime) || ((int)ftell(logfile) > configuration.log_max_size) ) {
			fclose(logfile);
			logfile = NULL;
			logrotatetime = logupdatetime + configuration.log_max_age;
			rotate_log(configuration.weblog, configuration.log_max_copies);
		}
	}
}
#endif

static void check_for_clfrotate(void)
/* Should only be called when logfile is already open */
{
	if (configuration.clf_max_copies > 0) {
		if ( (clfupdatetime > clfrotatetime) || ((int)ftell(clffile) > configuration.clf_max_size) ) {
			fclose(clffile);
			clffile = NULL;
			clfrotatetime = clfupdatetime + configuration.clf_max_age;
			rotate_log(configuration.clflog, configuration.clf_max_copies);
		}
	}
}

void writelog(int level, char *string) {

#define SysLog_LogMessage 0x4c880

#ifdef SYSLOG
	if (level > configuration.loglevel)  return;
	level = (level<<8)/LOGLEVEL_NEVER;
	if (level > 255)  level = 255;

	_swix(SysLog_LogMessage, _IN(0)|_IN(1)|_IN(2), "WebJames", string, level);

#else
	int newclock;


	if ((level > configuration.loglevel) || (*configuration.weblog == '\0'))   return;

	newclock = clock()/100;
	if (newclock != logupdatetime) {
		/* only remake the clock-string if it has changed */
		time_t clk;
		time(&clk);
		logclocklen=strftime(logclock, 31, "%d/%m/%Y %H:%M:%S", localtime(&clk));
		logupdatetime = newclock;
	}

	if (logbuffer) {
		int len = logclocklen + strlen(string) + 4 ;
		if (len > configuration.logbuffersize - logbufferused) {
			/* Not enough room in buffer */
			write_buffer();
			if (logfile) {
				fprintf(logfile, "%s : %s\n", logclock, string);
				check_for_rotate();
				fclose(logfile);
				logfile = NULL;
			}
		} else {
			sprintf(logbuffer + logbufferused, "%s : %s\n", logclock, string);
			logbufferused += len;
		}
	} else {
		if (!logfile)  logfile = fopen(configuration.weblog, "a+");
		if (!logfile)  logfile = fopen(configuration.weblog, "w");
		if (logfile) {
			fprintf(logfile, "%s : %s\n", logclock, string);

			check_for_rotate();
		} else {
			*configuration.weblog = '\0'; /* disable logging */
		}
	}

#endif
}


void close_log(void) {

#ifndef SYSLOG
	write_buffer();
	if (logfile)  fclose(logfile);
	logfile = NULL;
	if ((configuration.log_max_copies > 0) && (clock()/100 > logrotatetime)) {
		logrotatetime = clock()/100 + configuration.log_max_age;
		rotate_log(configuration.weblog, configuration.log_max_copies);
	}
#endif

	write_clfbuffer();
	if (clffile)  fclose(clffile);
	clffile = NULL;
	if ((configuration.clf_max_copies > 0) && (clock()/100 > clfrotatetime)) {
		clfrotatetime = clock()/100 + configuration.clf_max_age;
		rotate_log(configuration.clflog, configuration.clf_max_copies);
	}
}



void clf_cgi_finished(int code, int bytes, char *host, char *request) {

	char clk[100];
	time_t rightnow;

	rightnow = time(NULL);
	strftime(clk, 100, "%d/%b/%Y:%H:%M:%S", localtime(&rightnow));
	sprintf(temp, "%s - - [%s +0000] \"%s\" %d %d \"\" \"\"", host, clk, request, code, bytes);
	writeclf(temp);
}



void clf_connection_closed(int cn) {
/* host - userid day/month/year:hour:minute:sec request statuscode bytes */

	struct connection *conn;
	char clk[100], *referer, *useragent, dummy[1];
	time_t rightnow;

	dummy[0] ='\0';

	conn = connections[cn];
	useragent = conn->useragent;
	if (!useragent)  useragent = dummy;
	referer = conn->referer;
	if (!referer)  referer = dummy;

	rightnow = time(NULL);
	strftime(clk, 100, "%d/%b/%Y:%H:%M:%S", localtime(&rightnow));
	sprintf(temp, "%s - - [%s +0000] \"%s\" %d %d \"%s\" \"%s\"",
			conn->host, clk, conn->requestline, conn->statuscode,
			conn->fileinfo.size, referer, useragent);
	writeclf(temp);
}

void writeclf(char *string) {

	/* if clg-log enabled? */
	if (*configuration.clflog == '\0')  return;

	clfupdatetime = clock()/100;

	if (clfbuffer) {
		int len = strlen(string) + 1;
		if (len >= configuration.logbuffersize - clfbufferused) {
			/* Not enough room in buffer */
			write_clfbuffer();
			if (clffile) {
				fprintf(clffile, "%s\n", string);
				check_for_clfrotate();
				fclose(clffile);
				clffile = NULL;
			}
		} else {
			sprintf(clfbuffer + clfbufferused, "%s\n", string);
			clfbufferused += len;
		}
	} else {
		/* No buffering */
		/* if the clf-log file is closed, open it */
		if (!clffile) clffile = fopen(configuration.clflog, "a+");
		if (!clffile)  clffile = fopen(configuration.clflog, "w");

		/* if the clf-log file is open, write the log data */
		if (clffile) {
			fprintf(clffile, "%s\n", string);

			check_for_clfrotate();
		} else {
			/* disable clf-log */
			*configuration.clflog = '\0';
		}
	}
}
