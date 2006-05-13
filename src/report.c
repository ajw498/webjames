/*
	$Id$
	Error reporting functions
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "webjames.h"
#include "wjstring.h"
#include "cache.h"
#include "stat.h"
#include "datetime.h"
#include "openclose.h"
#include "report.h"
#include "write.h"
#include "attributes.h"
#include "handler.h"


struct reportcache reports[REPORTCACHECOUNT];

/* global structure describing the keywords and their values */
static struct substitute subs[20];
static char *header[20];


typedef struct reportname {
	int code;                   /* http status code */
	char *string;               /* text */
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
		{ HTTP_NOTACCEPTABLE,  "No acceptable content"},
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



static char *report_substitute(struct reportcache *report, struct substitute subs[], int num, int *size, struct connection *conn) {
/* substitutes all occurences of specific strings */

/* report           cached report-html-file */
/* subs[]           array holding the string and the substitute-values */
/* num              size of subs[] */
/* size             (on exit) holds the size of the substituted file */

/* returns a pointer to the substituted file in memory, or NULL */


	int newsize, match, matches;
	char *search, *end, *buffer, *read, *write;

	/* add the webmaster-email entry to the list of strings to substitute */
	subs[num].name = "%EMAIL%";
	subs[num].value = conn->vhost ? conn->vhost->serveradmin : configuration.webmaster; /* vhost entry may not be valid at this point */
	num++;

	/* calculate the length of all the entries */
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
		/* loop through all occurences of '%' in the file */
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

	buffer = EM(malloc(newsize + 1));
	if (!buffer)  return NULL;

	if (!matches) {
		report->substitute = REPORT_SUBSTITUTE_NOT_NEEDED;
		memcpy(buffer, report->buffer, report->size);
		*size = report->size;
		return buffer;
	}

	report->substitute = REPORT_SUBSTITUTE_NEEDED;

	/* do substitution */
	read = report->buffer;
	write = buffer;
	/* loop through all occurences of '%' in the file */
	search = findmatch(read, end);
	while (search) {
		if (search > read) {
			/* skipped some chars, so copy them to the output */
			int bytes, i;
			bytes = (int)(search-read);
			for (i = 0; i < bytes; i++)   *write++ = read[i];
		}
		for (match = 0; match <= num; match++) {
			if (strncmp(search, subs[match].name, subs[match].namelen) == 0) {
				wjstrncpy(write, subs[match].value,newsize+1-(write-buffer));
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
/* return a pointer to a cached report-file or NULL if the file couldn't */
/* be found or cached */

/* report           http status code */

	int rep, size;
	char filename[MAX_FILENAME];
	FILE *file;

	/* check if the report is already cached */
	for (rep = 0; rep < REPORTCACHECOUNT; rep++)
		if (reports[rep].report == report) {
			/* it is cached... */
			if (reports[rep].reload) {
				/* ... but is is marked for reload */
				free(reports[rep].buffer);
				reports[rep].report = -1;       /* mark it as empty */
			} else
				/* ... so use it! */
				break;
		}
	if (rep < REPORTCACHECOUNT)  return &reports[rep];

	/* if it isn't cached, find an empty entry */
	for (rep = 0; rep < REPORTCACHECOUNT; rep++)
		if (reports[rep].report == -1)  break;

	if (rep >= REPORTCACHECOUNT) {
	/* if no empty slots could be found, make an empty entry */
		int time, oldest;
		time = 0x7f000000;
		oldest = -1;
		for (rep = 0; rep < REPORTCACHECOUNT; rep++)
			if (reports[rep].time < time) {
				time = reports[rep].time;
				oldest = rep;
			}
		if (oldest == -1)  return NULL;     /* unable to find the oldest one! */
		rep = oldest;
		free(reports[rep].buffer);
		reports[rep].report = -1;
	}

	/* cache the file */
	snprintf(filename, MAX_FILENAME, "<WebJames$Dir>.Reports.%d", report);
	file = fopen(filename, "r");
	if (file==NULL) {
		webjames_writelog(LOGLEVEL_OSERROR,"ERROR couldn't open report file %s",filename);
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	size = (int)ftell(file);

	reports[rep].buffer = EM(malloc(size+1));
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
/* scans the list of http status codes and returns a string describing it */

/* report           http status code */

	int i;

	for (i = 0; (rnames[i].code != -1); i++)
		if (rnames[i].code == report)  return rnames[i].string;

	return "";
}



static void report_quickanddirty(struct connection *conn, int report) {
/* if all other fails, this attempts to write a fairly understandable */
/* reply to the socket */

/* conn             connection structure */
/* report           http status code */

	char *name;

	if (conn->flags.outputheaders && conn->httpmajor >= 1) {
		time_t now;
		char rfcnow[50];

		name = get_report_name(report);
		snprintf(temp, TEMPBUFFERSIZE, "HTTP/1.0 %d %s\r\n", report, name);
		webjames_writestringr(conn, temp);
		if (configuration.server[0]) snprintf(temp, TEMPBUFFERSIZE, "Server: %s\r\n", configuration.server);
		webjames_writestringr(conn, temp);
		time(&now);
		time_to_rfc(localtime(&now),rfcnow);
		snprintf(temp, TEMPBUFFERSIZE, "Date: %s\r\n", rfcnow);
		webjames_writestringr(conn, "Content-Type: text/html\r\n");
		snprintf(temp, TEMPBUFFERSIZE, "Content-Length: %d\r\n\r\n", strlen(configuration.panic));
		webjames_writestringr(conn, temp);
	}
	webjames_writestringr(conn, configuration.panic);
}



void report(struct connection *conn, int code, int subno, int headerlines, char *comment) {
/* generates and writes a report */

/* conn             connection structure */
/* code             http status code (eg. HTTP_NOTMOVED) */
/* subno            size of subs[] */
/* headerlines      size of header[] */
/* comment          extra info to write to the log */

/* on exit, the connection is closed (reverse dns may still be going on) */

	struct reportcache *report;
	struct errordoc *errordoc;
	char *buffer = NULL, *reportname;
	int size, i;
	char url[HTTPBUFFERSIZE];

	conn->statuscode = code;

	reportname = get_report_name(code);

	if (conn->uri) {
		webjames_writelog(LOGLEVEL_REPORT, "REPORT %d %s (%s) %s", code, reportname, conn->uri, comment);
	} else {
		webjames_writelog(LOGLEVEL_REPORT, "REPORT %d %s %s", code, reportname, comment);
	}

	/* see if there is an error report/redirection specified for this directory */
	errordoc = conn->errordocs;
	while (errordoc) {
		if (errordoc->status == code) break;
		errordoc = errordoc->next;
	}

	if (errordoc) {
		char *reporttext;

		reporttext = errordoc->report;
		if (reporttext[0] == '\"') {
			/* send message given in errordoc */
			reporttext++;
			conn->flags.releasefilebuffer = 0;
			conn->filebuffer = reporttext;
			conn->fileinfo.size = strlen(reporttext);
			conn->file = NULL;
			conn->fileused = 0;
			conn->flags.is_cgi = 0;
		} else {
			/* must be a redirect */
			if (reporttext[0] == '/') {
				/* file is stored on this server */
				struct connection *tempconn;
				char *name;

				tempconn = create_conn();
				if (tempconn) {
					tempconn->uri = reporttext;
					get_attributes(tempconn->uri,tempconn);

					/* build RISCOS filename */
					name = tempconn->filename;
					wjstrncpy(name, tempconn->homedir, MAX_FILENAME);
					name += strlen(name);
					/* append requested URI, with . and / switched */
					if (!uri_to_filename(tempconn->uri + tempconn->homedirignore,name,tempconn->flags.stripextensions)) {
						struct cache *cacheentry;

						wjstrncpy(conn->filename,tempconn->filename,MAX_FILENAME);

						tempconn->fileinfo.filetype = get_file_info(tempconn->filename, NULL, &tempconn->fileinfo.date, NULL, &tempconn->fileinfo.size, 1, 0);

						/* check if object is cached */
						if (tempconn->flags.cacheable) {
							cacheentry = get_file_through_cache(tempconn);
						} else {
							cacheentry = NULL;
						}

						/* prepare to send file */
						if (cacheentry) {
							conn->filebuffer = cacheentry->buffer;
							size = conn->fileinfo.size = cacheentry->size;
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
								/* attempt to get a read-ahead buffer for the file */
								/* notice: things will still work if malloc fails */
								conn->filebuffer = EM(malloc(configuration.readaheadbuffer*1024));
								conn->flags.releasefilebuffer = 1;
								conn->flags.is_cgi = 0;
								conn->leftinbuffer = 0;
								/* set the fields in the structure, and that's it! */
								conn->file = handle;
								fseek(handle,0,SEEK_END);
								size = conn->fileinfo.size = (int)ftell(handle);
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
					free(tempconn);
				} else {
					errordoc = NULL;
				}
			} else {
				/* file is on another server (or this server, but the whole url including hostname was given) */
				size_t len;

				wjstrncpy(url, reporttext, MAX_FILENAME);

				for (i = 0; i < headerlines; i++) {
					if (header[i]) {
						free(header[i]);
						header[i] = NULL;
					}
				}

				len=strlen(url)+11;
				header[0] = EM(malloc(len));
				if (header[0])  snprintf(header[0], len, "Location: %s", url);

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
		/* attempt to cache/read template HTML file */
		report = report_getfile(code);
		if (!report) {
			/* if this failed, do it the primitive way */
			report_quickanddirty(conn, code);
			conn->close(conn, 0);
			return;
		}

		/* attempt to substitute the keywords with their values */
		buffer = report_substitute(report, subs, subno, &size, conn);
		if (!buffer) {
			report_quickanddirty(conn, code);
			conn->close(conn, 0);
			return;
		}
		/* send the generated file - the buffer is released afterwards */
		conn->flags.releasefilebuffer = 1;
		conn->filebuffer = buffer;
		conn->fileinfo.size = size;
		conn->file = NULL;
		conn->fileused = 0;
		conn->flags.is_cgi = 0;
	}

	if (conn->flags.outputheaders && conn->httpmajor >= 1) {
		time_t now;
		char rfcnow[50];

		snprintf(temp, TEMPBUFFERSIZE, "HTTP/1.0 %d %s\r\n", code, reportname);
		webjames_writestringr(conn, temp);
		webjames_writestringr(conn, "Content-Type: text/html\r\n");
		/* if no substitution was required, simply send the cached file */
		snprintf(temp, TEMPBUFFERSIZE, "Content-Length: %d\r\n", size);
		webjames_writestringr(conn, temp);
		if (configuration.server[0]) snprintf(temp, TEMPBUFFERSIZE, "Server: %s\r\n", configuration.server);
		webjames_writestringr(conn, temp);
		time(&now);
		time_to_rfc(localtime(&now),rfcnow);
		snprintf(temp, TEMPBUFFERSIZE, "Date: %s\r\n", rfcnow);
		webjames_writestringr(conn, temp);
		for (i = 0; i < headerlines; i++) {
			if (header[i]) {
				webjames_writestringr(conn, header[i]);
				free(header[i]);
				header[i] = NULL;
				webjames_writestringr(conn, "\r\n");
			}
		}
		webjames_writestringr(conn, "\r\n");
	}

	conn->handler = get_handler("static-content");
}



void report_moved(struct connection *conn, char *newurl) {

	char url[MAX_FILENAME];
	size_t len;

	wjstrncpy(url, newurl, MAX_FILENAME);

	snprintf(temp, TEMPBUFFERSIZE, "Location: %s", url);
	len=strlen(temp)+1;
	header[0] = EM(malloc(len));
	if (header[0])  memcpy(header[0], temp, len);

	subs[0].name = "%NEWURL%";
	subs[0].value = url;
	subs[1].name = "%URL%";
	subs[1].value = conn->uri;

	report(conn, HTTP_MOVED, 2, 1, "");
}


void report_movedtemporarily(struct connection *conn, char *newurl) {

	char url[MAX_FILENAME];
	size_t len;

	wjstrncpy(url, newurl, MAX_FILENAME);

	snprintf(temp, TEMPBUFFERSIZE, "Location: %s", url);
	len=strlen(temp)+1;
	header[0] = EM(malloc(len));
	if (header[0])  memcpy(header[0], temp, len);

	subs[0].name = "%NEWURL%";
	subs[0].value = url;
	subs[1].name = "%URL%";
	subs[1].value = conn->uri;

	report(conn, HTTP_TEMPMOVED, 2, 1, "");
}


void report_notimplemented(struct connection *conn, char *request) {

	char *supported = "Public: GET, POST, HEAD";
	size_t len;

	subs[0].name = "%METHOD%";
	subs[0].value = request;

	len=strlen(supported)+1;
	header[0] = EM(malloc(len));
	if (header[0])  memcpy(header[0], supported, len);

	report(conn, HTTP_NOTIMPLEMENTED, 1, 1, request);
}


void report_notmodified(struct connection *conn) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	report(conn, HTTP_NOTMODIFIED, 1, 0, "");
}


void report_nocontent(struct connection *conn) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	report(conn, HTTP_NOCONTENT, 1, 0, "");
}

void report_notacceptable(struct connection *conn,struct varmap *map) {
	size_t len=0;
    struct varmap *tempmap=map;
    char *list;

	while (tempmap) {
		if (tempmap->uri) {
			len+=22+2*strlen(tempmap->uri);
			if (tempmap->description) len+=strlen(tempmap->description);
		}
		tempmap=tempmap->next;
	}

	list=EM(malloc(++len));
	if (!list) {
		report_quickanddirty(conn, HTTP_NOTACCEPTABLE);
		conn->close(conn, 0);
		return;
	}

	list[0]='\0';
	tempmap=map;
	while (tempmap) {
		if (tempmap->uri) {
			char buffer[256];
			char *s,*d;

			s=tempmap->uri;
			d=buffer;
			while (*s) {
				if (*s=='/') {
					*d='.';
				} else if (*s=='.') {
					*d='/';
				} else {
					*d=*s;
				}
				s++;
				d++;
			}
			*d='\0';
			wjstrncat(list,"<A HREF=\"",len);
			wjstrncat(list,buffer,len);
			wjstrncat(list,"\">",len);
			wjstrncat(list,buffer,len);
			wjstrncat(list,"</A> \n",len);
			if (tempmap->description) wjstrncat(list,tempmap->description,len);
			wjstrncat(list,"<BR>\n",len);
		}
		tempmap=tempmap->next;
	}

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	subs[1].name = "%LIST%";
	subs[1].value = list;

	report(conn, HTTP_NOTACCEPTABLE, 2, 0, "");
	free(list);
}

void report_badrequest(struct connection *conn, char *info) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	report(conn, HTTP_BADREQUEST, 1, 0, info);

}


void report_unauthorized(struct connection *conn, char *realm) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	header[0] = EM(malloc(MAX_FILENAME));
	if (header[0])
		snprintf(header[0], MAX_FILENAME, "WWW-Authenticate: basic realm=\"%s\"", realm);

	report(conn, HTTP_UNAUTHORIZED, 1, 1, realm);
}


void report_forbidden(struct connection *conn) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	report(conn, HTTP_FORBIDDEN, 1, 0, "");
}


void report_notfound(struct connection *conn) {

	subs[0].name = "%URL%";
	subs[0].value = conn->uri;

	report(conn, HTTP_NOTFOUND, 1, 0, "");
}


void report_busy(struct connection *conn, char *text) {

	subs[0].name = "%WHY%";
	subs[0].value = text;

	report(conn, HTTP_BUSY, 1, 0, text);
}


void report_servererr(struct connection *conn, char *info) {

	report(conn, HTTP_SERVERERR, 0, 0, info);
}
