#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"

#include "webjames.h"
#include "stat.h"
#include "ip.h"
#include "kernel.h"

#ifdef SYSLOG
#include "swis.h"
#endif


struct stats statistics;
char weblog[256], clflog[256], rename_cmd[256];
static char logclock[32];
int loglevel = 5;
int log_close_delay = 90;               // seconds


#ifndef SYSLOG
int log_max_age, log_max_size;
int log_max_copies = 0;
static int logupdatetime, logrotatetime;       // clock()/100 values
static FILE *logfile = NULL;
#endif

int clfupdatetime;
static int clfrotatetime;       // clock()/100 values
FILE *clffile = NULL;
int clf_max_age, clf_max_size;
int clf_max_copies = 0;



void init_statistics() {
// reset statistics

  // statistics logging start time (real time)
  statistics.starttime = time(NULL);
  // statistics logging start time (relative time)
  xos_read_monotonic_time(&statistics.time);
  // only adjust slowdown every 25 cs
  statistics.adjusttime = statistics.time - 25;
  statistics.written = 0;
  statistics.access = 0;
  statistics.written2 = configuration.bandwidth;
  statistics.access2 = 0;

  clfupdatetime = -1;           // clock() at last update
  clf_max_age *= 3600;          // max age in secs
  if (clf_max_size <= 0)
    clf_max_size = 0x7f000000;  // no limit to the size
  else
    clf_max_size *= 1024;

  clfrotatetime = clock()/100 + clf_max_age;        // clock()/100 value

#ifndef SYSLOG
  logupdatetime = -1;           // clock()/100 at last update
  log_max_age *= 3600;          // max age in secs
  if (log_max_size <= 0)
    log_max_size = 0x7f000000;  // no limit to the size
  else
    log_max_size *= 1024;

  logrotatetime = clock()/100 + log_max_age;        // clock()/100 value
#endif

  if ((loglevel < 0) || (loglevel > 10))
    loglevel = 5;
  if ((log_close_delay < 1) || (log_close_delay > 3600))
    log_close_delay = 90;

}



void update_statistics() {

  int clk;

  clk = clock()/100;

  if ((clk > clfupdatetime + log_close_delay) && (clffile)) {
    fclose(clffile);
    clffile = NULL;
  }
#ifndef SYSLOG
  if ((clk > logupdatetime + log_close_delay) && (logfile)) {
    fclose(logfile);
    logfile = NULL;
  }
#endif

  statistics.written2 =  statistics.written;
  statistics.access2 =  statistics.access;
  statistics.written = statistics.access = 0;
}



void writelog(int level, char *string) {

#define SysLog_LogMessage 0x4c880

#ifdef SYSLOG
  if (level > loglevel)  return;
  level = (level<<8)/LOGLEVEL_NEVER;
  if (level > 255)  level = 255;

  _swix(SysLog_Message, _IN(0)|_IN(1)|_IN(2), "WebJames", string, level);

#else
  int newclock;

  if ((level > loglevel) || (*weblog == '\0'))   return;

  newclock = clock()/100;
  if (newclock != logupdatetime) {
    // only remake the clock-string if it has changed
    time_t clk;
    time(&clk);
    strftime(logclock, 31, "%d/%m/%Y %H:%M:%S", localtime(&clk));
    logupdatetime = newclock;
  }

  if (!logfile)  logfile = fopen(weblog, "a+");
  if (!logfile)  logfile = fopen(weblog, "w");
  if (logfile) {
    fprintf(logfile, "%s : %s\n", logclock, string);

    if (log_max_copies > 0) {
      if ( (logupdatetime > logrotatetime) || ((int)ftell(logfile) > log_max_size) ) {
        fclose(logfile);
        logfile = NULL;
        logrotatetime = logupdatetime + log_max_age;
        rotate_log(weblog, log_max_copies);
      }
    }

  } else
    *weblog = '\0';           // disable logging


#endif
}



void rotate_log(char *logfile, int copies) {

  int f;
  char oldname[256], newname[256], cmd[256], *ptr;

  if (copies > 1) {
    // remove the oldest log file
    sprintf(oldname, "%s_%d", logfile, copies-1);
    remove(oldname);
    // rotate the rest
    for (f = copies-2; f > 0; f--) {
      sprintf(oldname, "%s_%d", logfile, f);
      sprintf(newname, "%s_%d", logfile, f+1);
      rename(oldname, newname);
    }
    // rename the newest
    if (*rename_cmd) {
      int read, write, len;
      len = strlen(rename_cmd);
      write = 0;
      for (read = 0; read < len; read++) {
        cmd[write] = rename_cmd[read];
        if (rename_cmd[read] == '%') {
          if (rename_cmd[read+1] == '%') {
            cmd[++write] = '%';
            read++;
          } else if (rename_cmd[read+1] == '0') {
            strcpy(cmd+write, logfile);
            write += strlen(logfile)-1;
            read++;
          } else if (rename_cmd[read+1] == '1') {
            sprintf(cmd+write, "%s_1", logfile);
            write = strlen(cmd)-1;
            read++;
          } else if (rename_cmd[read+1] == '2') {
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



void closelog() {

#ifndef SYSLOG
  if (logfile)  fclose(logfile);
  logfile = NULL;
  if ((log_max_copies > 0) && (clock()/100 > logrotatetime)) {
    logrotatetime = clock()/100 + log_max_age;
    rotate_log(weblog, log_max_copies);
  }
#endif

  if (clffile)  fclose(clffile);
  clffile = NULL;
  if ((clf_max_copies > 0) && (clock()/100 > clfrotatetime)) {
    clfrotatetime = clock()/100 + clf_max_age;
    rotate_log(clflog, clf_max_copies);
  }
}



void clf_cgi_finished(int code, int bytes, char *host, char *request) {

  char clk[100];
  time_t rightnow;

  rightnow = time(NULL);
  strftime(clk, 100, "%d/%b/%Y:%H:%M:%S", localtime(&rightnow));
  sprintf(temp, "%s - - [%s +0000] \"%s\" %d %d \"\" \"\"", host,
                clk, request, code, bytes);
  writeclf(temp);
}



void clf_connection_closed(int cn) {
// host - userid day/month/year:hour:minute:sec request statuscode bytes

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
                conn->filesize, referer, useragent);
  writeclf(temp);
}



void writeclf(char *string) {

  // if clg-log enabled?
  if (*clflog == '\0')  return;

  // if the clf-log file is closed, open it
  if (!clffile) {
    clffile = fopen(clflog, "a+");
    if (!clffile)  clffile = fopen(clflog, "w");
  }

  // if the clf-log file is open, write the log data
  if (clffile) {
    clfupdatetime = clock()/100;
    fprintf(clffile, "%s\n", string);

    if (clf_max_copies > 0) {
      if ( (clfupdatetime > clfrotatetime) || ((int)ftell(clffile) > clf_max_size) ) {
        fclose(clffile);
        clffile = NULL;
        clfrotatetime = clfupdatetime + clf_max_age;
        rotate_log(clflog, clf_max_copies);
      }
    }

  } else
  // disable clf-log
    *clflog = '\0';
}
