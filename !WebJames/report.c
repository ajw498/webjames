#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "cache.h"
#include "stat.h"
#include "webjames.h"
#include "ip.h"
#include "openclose.h"
#include "report.h"
#include "write.h"
#include "attributes.h"


struct reportcache reports[REPORTCACHECOUNT];

// global structure describing the keywords and their values
static struct substitute subs[20];
static char *header[20];


typedef struct reportname {
  int code;                   // http status code
  char *string;               // text
} reportname;

static struct reportname rnames[] = {
          { HTTP_NOCONTENT,      "No content"},
          { HTTP_MOVED,          "File has been moved"},
          { HTTP_TEMPMOVED,      "File has been moved temporarily"},
          { HTTP_NOTMODIFIED,    "Not modified"},
          { HTTP_BADREQUEST,     "Bad request"},
          { HTTP_UNAUTHORIZED,   "Unauthorized access"},
          { HTTP_FORBIDDEN,      "No permission"},
          { HTTP_NOTFOUND,       "File not found"},
          { HTTP_NOTIMPLEMENTED, "Not implemented"},
          { HTTP_BUSY,           "Busy"},
          { HTTP_SERVERERR,      "Unexpected server error"},
          { -1, ""}
   };


static char *findmatch(char *start, char *end) {

  char *ptr;

  for (ptr = start; ptr < end; ptr++)
     if (*ptr == '%')  return ptr;
  return NULL;
}



void report_flushcache() {

  int i;
  for (i = 0; i < REPORTCACHECOUNT; i++)
    reports[i].reload = 1;
}



static char *report_substitute(struct reportcache *report, struct substitute subs[], int num, int *size) {
// substitutes all occurences of specific strings
//
// report           cached report-html-file
// subs[]           array holding the string and the substitute-values
// num              size of subs[]
// size             (on exit) holds the size of the substituted file
//
// returns a pointer to the substituted file in memory, or NULL
//

  int newsize, match, matches;
  char *search, *end, *buffer, *read, *write;

  // add the webmaster-email entry to the list of strings to substitute
  subs[num].name = "%EMAIL%";
  subs[num].value = configuration.webmaster;
  num++;

  // calculate the length of all the entries
  for (match = 0; match < num; match++) {
    subs[match].namelen = strlen(subs[match].name);
    subs[match].valuelen = strlen(subs[match].value);
  }

  matches = 0;
  end = 0;
  newsize = report->size;
  if (report->substitute != REPORT_SUBSTITUTE_NOT_NEEDED) {
    search = report->buffer;
    end = search + report->size;
    // loop through all occurences of '%' in the file
    search = findmatch(search, end);
    while (search) {
      for (match = 0; match <= num; match++) {
        if (strncmp(search, subs[match].name, subs[match].namelen) == 0) {
          newsize += subs[match].valuelen - subs[match].namelen;
          matches++;
          search += subs[match].namelen;
          break;
        }
      }
      search = findmatch(search+1, end);
    }
  }

  buffer = malloc(newsize + 1);
  if (!buffer)  return NULL;

  if (!matches) {
    report->substitute = REPORT_SUBSTITUTE_NOT_NEEDED;
    memcpy(buffer, report->buffer, report->size);
    *size = report->size;
    return buffer;
  }

  report->substitute = REPORT_SUBSTITUTE_NEEDED;

  // do substitution
  read = report->buffer;
  write = buffer;
  // loop through all occurences of '%' in the file
  search = findmatch(read, end);
  while (search) {
    if (search > read) {
      // skipped some chars, so copy them to the output
      int bytes, i;
      bytes = (int)(search-read);
      for (i = 0; i < bytes; i++)   *write++ = read[i];
    }
    for (match = 0; match <= num; match++) {
      if (strncmp(search, subs[match].name, subs[match].namelen) == 0) {
        strcpy(write, subs[match].value);
        write += subs[match].valuelen;
        search += subs[match].namelen;
        break;
      }
    }

    read = search;
    search = findmatch(read, end);
  }
  for (; read < end;)   *write++ = *read++;

  *size = (int)(write-buffer);
  return buffer;
}


struct reportcache *report_getfile(int report) {
// return a pointer to a cached report-file or NULL if the file couldn't
// be found or cached
//
// report           http status code
//
  int rep, size;
  char filename[256];
  FILE *file;

  // check if the report is already cached
  for (rep = 0; rep < REPORTCACHECOUNT; rep++)
    if (reports[rep].report == report) {
      // it is cached...
      if (reports[rep].reload) {
        // ... but is is marked for reload
        free(reports[rep].buffer);
        reports[rep].report = -1;       // mark it as empty
      } else
        // ... so use it!
        break;
    }
  if (rep < REPORTCACHECOUNT)  return &reports[rep];

  // if it isn't cached, find an empty entry
  for (rep = 0; rep < REPORTCACHECOUNT; rep++)
    if (reports[rep].report == -1)  break;

  if (rep >= REPORTCACHECOUNT) {
  // if no empty slots could be found, make an empty entry
    int time, oldest;
    time = 0x7f000000;
    oldest = -1;
    for (rep = 0; rep < REPORTCACHECOUNT; rep++)
      if (reports[rep].time < time) {
        time = reports[rep].time;
        oldest = rep;
      }
    if (oldest == -1)  return NULL;     // unable to find the oldest one!
    rep = oldest;
    free(reports[rep].buffer);
    reports[rep].report = -1;
  }

  // cache the file
  sprintf(filename, "<WebJames$Dir>.Reports.%d", report);
  file = fopen(filename, "r");
  if (!file)  return NULL;
  fseek(file, 0, SEEK_END);
  size = (int)ftell(file);

  reports[rep].buffer = malloc(size+1);
  if (!reports[rep].buffer) {
    fclose(file);
    return NULL;
  }
  fseek(file, 0, SEEK_SET);
  fread(reports[rep].buffer, 1, size, file);
  fclose(file);
  reports[rep].size = size;
  reports[rep].report = report;
  reports[rep].substitute = REPORT_SUBSTITUTE_NOT_TESTED;
  reports[rep].time = clock();
  reports[rep].reload = 0;

  return &reports[rep];
}



static char *get_report_name(int report) {
// scans the list of http status codes and returns a string describing it
//
// report           http status code
//
  int i;

  for (i = 0; (rnames[i].code != -1); i++)
    if (rnames[i].code == report)  return rnames[i].string;

  return "";
}



static void report_quickanddirty(struct connection *conn, int report) {
// if all other fails, this attempts to write a fairly understandable
// reply to the socket
//
// conn             connection structure
// report           http status code
//
  char *name;

  if (conn->httpmajor >= 1) {
    name = get_report_name(report);
    sprintf(temp, "HTTP/1.0 %d %s\r\n", report, name);
    writestring(conn->socket, temp);
    sprintf(temp, "Server: %s\r\n", configuration.server);
    writestring(conn->socket, temp);
    writestring(conn->socket, "Content-Type: text/html\r\n");
    sprintf(temp, "Content-Length: %d\r\n\r\n", strlen(configuration.panic));
    writestring(conn->socket, temp);
  }
  writestring(conn->socket, configuration.panic);
  statistics.written += strlen(configuration.panic);
}



void report(struct connection *conn, int code, int subno, int headerlines) {
// generates and writes a report
//
// conn             connection structure
// code             http status code (eg. HTTP_NOTMOVED)
// subno            size of subs[]
// headerlines      size of header[]
//
// on exit, the connection is closed (reverse dns may still be going on)
//
  struct reportcache *report;
  struct errordoc *errordoc;
  char *buffer = NULL, *reportname;
  int size, i;
  char url[HTTPBUFFERSIZE];

  conn->statuscode = code;

  reportname = get_report_name(code);
#ifdef LOG
  if (conn->uri)
    sprintf(temp, "REPORT %d %s (%s)", code, reportname, conn->uri);
  else
    sprintf(temp, "REPORT %d %s", code, reportname);
  writelog(LOGLEVEL_REPORT, temp);
#endif

  // see if there is an error report/redirection specified for this directory
  errordoc = conn->errordocs;
  while (errordoc) {
    if (errordoc->status == code) break;
    errordoc = errordoc->next;
  }

  if (errordoc) {
    char *reporttext;

    reporttext = errordoc->report;
    if (reporttext[0] == '\"') {
      // send message given in errordoc
      reporttext++;
      conn->flags.releasefilebuffer = 0;
      conn->filebuffer = reporttext;
      conn->filesize = strlen(reporttext);
      conn->file = NULL;
      conn->fileused = 0;
      conn->flags.is_cgi = 0;
    } else {
      // must be a redirect
      if (reporttext[0] == '/') {
        // file is stored on this server
        struct connection *tempconn;
        char *name;

        tempconn = create_conn();
        if (tempconn) {
          tempconn->uri = reporttext;
          get_attributes(tempconn->uri,tempconn);

          // build RISCOS filename
          name = tempconn->filename;
          strcpy(name, tempconn->homedir);
          name += strlen(name);
          // append requested URI, with . and / switched
          if (!uri_to_filename(tempconn->uri + tempconn->homedirignore,name)) {
            struct cache *cacheentry;

            strcpy(conn->filename,tempconn->filename);

            // check if object is cached
            if (tempconn->flags.cacheable) {
              cacheentry = get_file_through_cache(tempconn->uri, conn->filename);
            } else {
              cacheentry = NULL;
            }
            free(tempconn);

            // prepare to send file
            if (cacheentry) {
              conn->filebuffer = cacheentry->buffer;
              size = conn->filesize = cacheentry->size;
              conn->flags.releasefilebuffer = 0;
              conn->flags.is_cgi = 0;
              conn->file = NULL;
              conn->fileused = 0;
              conn->cache = cacheentry;
              cacheentry->accesses++;

              for (i = 0; i < headerlines; i++) {
                if (header[i]) {
                  free(header[i]);
                  header[i] = NULL;
                }
              }
              subno = 0;
              headerlines = 0;

            } else {
              FILE *handle;

              handle = fopen(conn->filename, "rb");
              if (handle) {
                // attempt to get a read-ahead buffer for the file
                // notice: things will still work if malloc fails
                conn->filebuffer = malloc(readaheadbuffer*1024);
                conn->flags.releasefilebuffer = 1;
                conn->flags.is_cgi = 0;
                conn->leftinbuffer = 0;
                // set the fields in the structure, and that's it!
                conn->file = handle;
                fseek(handle,0,SEEK_END);
                size = conn->filesize = (int)ftell(handle);
                fseek(handle,0,SEEK_SET);

                for (i = 0; i < headerlines; i++) {
                  if (header[i]) {
                    free(header[i]);
                    header[i] = NULL;
                  }
                }
                subno = 0;
                headerlines = 0;

              } else {
                errordoc = NULL;
              }
            }
          } else {
            errordoc = NULL;
          }
        } else {
          errordoc = NULL;
        }
      } else {
        // file is on another server (or this server, but the whole url including hostname was given)
        strcpy(url, reporttext);

        for (i = 0; i < headerlines; i++) {
          if (header[i]) {
            free(header[i]);
            header[i] = NULL;
          }
        }

        header[0] = malloc(strlen(url)+11);
        if (header[0])  sprintf(header[0], "Location: %s", url);

        subs[0].name = "%NEWURL%";
        subs[0].value = url;
        subs[1].name = "%URL%";
        subs[1].value = conn->uri;

        subno = 2;
        headerlines = 1;
        code = HTTP_TEMPMOVED;
        errordoc = NULL;
      }

    }
  }
  if (!errordoc) {
    // attempt to cache/read template HTML file
    report = report_getfile(code);
    if (!report) {
      // if this failed, do it the primitive way
      report_quickanddirty(conn, code);
      close(conn->index, 0);
      return;
    }

    // attempt to substitute the keywords with their values
    buffer = report_substitute(report, subs, subno, &size);
    if (!buffer) {
      report_quickanddirty(conn, code);
      close(conn->index, 0);
      return;
    }
    // send the generated file - the buffer is released afterwards
    conn->flags.releasefilebuffer = 1;
    conn->filebuffer = buffer;
    conn->filesize = size;
    conn->file = NULL;
    conn->fileused = 0;
    conn->flags.is_cgi = 0;
  }

  if (conn->httpmajor >= 1) {
    sprintf(temp, "HTTP/1.0 %d %s\r\n", code, reportname);
    writestring(conn->socket, temp);
    writestring(conn->socket, "Content-Type: text/html\r\n");
    // if no substitution was required, simply send the cached file
    sprintf(temp, "Content-Length: %d\r\n", size);
    writestring(conn->socket, temp);
    sprintf(temp, "Server: %s\r\n", configuration.server);
    writestring(conn->socket, temp);
    for (i = 0; i < headerlines; i++) {
      if (header[i]) {
        writestring(conn->socket, header[i]);
        free(header[i]);
        header[i] = NULL;
        writestring(conn->socket, "\r\n");
      }
    }
    writestring(conn->socket, "\r\n");
  }
}



void report_moved(struct connection *conn, char *newurl) {

  char url[HTTPBUFFERSIZE];

  if (*configuration.serverip)
    sprintf(url, "http://%s%s", configuration.serverip, newurl);
  else
    strcpy(url, newurl);

  sprintf(temp, "Location: %s", url);
  header[0] = malloc(strlen(temp)+1);
  if (header[0])  strcpy(header[0], temp);

  subs[0].name = "%NEWURL%";
  subs[0].value = url;
  subs[1].name = "%URL%";
  subs[1].value = conn->uri;

  report(conn, HTTP_MOVED, 2, 1);
}


void report_movedtemporarily(struct connection *conn, char *newurl) {

  char url[HTTPBUFFERSIZE];

  if (*configuration.serverip)
    sprintf(url, "http://%s%s", configuration.serverip, newurl);
  else
    strcpy(url, newurl);

  sprintf(temp, "Location: %s", url);
  header[0] = malloc(strlen(temp)+1);
  if (header[0])  strcpy(header[0], temp);

  subs[0].name = "%NEWURL%";
  subs[0].value = url;
  subs[1].name = "%URL%";
  subs[1].value = conn->uri;

  report(conn, HTTP_TEMPMOVED, 2, 1);
}


void report_notimplemented(struct connection *conn, char *request) {

  char *supported = "Public: GET, POST, HEAD";

  subs[0].name = "%METHOD%";
  subs[0].value = request;

  header[0] = malloc(strlen(supported)+1);
  if (header[0])  strcpy(header[0], supported);

  report(conn, HTTP_NOTIMPLEMENTED, 1, 1);
}


void report_notmodified(struct connection *conn) {

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  report(conn, HTTP_NOTMODIFIED, 1, 0);
}


void report_nocontent(struct connection *conn) {

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  report(conn, HTTP_NOCONTENT, 1, 0);
}


void report_badrequest(struct connection *conn, char *info) {

  info = info;

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  report(conn, HTTP_BADREQUEST, 1, 0);
}


void report_unauthorized(struct connection *conn, char *realm) {

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  header[0] = malloc(256);
  if (header[0])
    sprintf(header[0], "WWW-Authenticate: basic realm=\"%s\"", realm);

  report(conn, HTTP_UNAUTHORIZED, 1, 1);
}


void report_forbidden(struct connection *conn) {

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  report(conn, HTTP_FORBIDDEN, 1, 0);
}


void report_notfound(struct connection *conn) {

  subs[0].name = "%URL%";
  subs[0].value = conn->uri;

  report(conn, HTTP_NOTFOUND, 1, 0);
}


void report_busy(struct connection *conn, char *text) {

  subs[0].name = "%WHY%";
  subs[0].value = text;

  report(conn, HTTP_BUSY, 1, 0);
}


void report_servererr(struct connection *conn) {

  report(conn, HTTP_SERVERERR, 0, 0);
}
