/*
	$Id$
	Reading requests
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "wjstring.h"
#include "attributes.h"
#include "stat.h"
#include "ip.h"
#include "datetime.h"
#include "read.h"
#include "report.h"
#include "write.h"
#include "coding.h"

static char line[HTTPBUFFERSIZE];

/* must be called before calling report_XXXX() during reading */
static void read_report(struct connection *conn) {

	ip_fd_clear(serverinfo.select_read, conn->socket);
	serverinfo.readcount--;

	select_writing(conn->index);
}


static void donereading(struct connection *conn) {
/* header (and body) has been read, now process the request */

	if (conn->httpmajor < 1) {
		read_report(conn);
		report_badrequest(conn, "bad HTTP version");
		return;
	}

	/* deselect the socket for reading */
	ip_fd_clear(serverinfo.select_read, conn->socket);
	serverinfo.readcount--;

	send_file(conn);
}



int readline(char *buffer, int bytes, char *line) {
/* read a header-line from the buffer; the header-line may be split */
/* over multiple lines and may be terminated by either CR or CRLF */

/* buffer           input buffer */
/* bytes            size of buffer */
/* line             output buffer */

/* return either 0 (failure) or number of bytes read */
	int read, write, i;

	read = write = 0;

	for (i = 0; i < bytes; i++) {
		line[write] = buffer[read++];
		line[write+1] = '\0';
		if (line[write] == '\n') { /* eol */
			if ((write == 0) || ((write == 1) && (line[0] == '\r')) )
				return read;                              /* empty line */

			else {                                      /* more than an empty line */
				if (buffer[read] == '\t')  buffer[read] = ' ';  /* conv. TAB to SPC */
				if (buffer[read] == ' ')                  /* continued line? */
					read++;                                 /* yes, so skip first char */
				else
					return read;                            /* no */

			}
		}
		write++;
	}

	return 0;
}



void pollread_body(struct connection *conn, int bytes)
/* read part of the body in the the body-buffer */
/* conn             connection structure */
/* bytes            number of bytes ready to be read */
{
	/* reading the body */

	if (conn->used + bytes > conn->bodysize) bytes = conn->bodysize-conn->used;

	if ((bytes = webjames_readbuffer(conn, conn->body+conn->used, bytes))<0) return;
	conn->used += bytes;
	if (conn->used >= conn->bodysize)  donereading(conn);
}



void pollread_header(struct connection *conn, int bytes)
{
	int i;
	size_t len;
	char upper[HTTPBUFFERSIZE], *newptr;

	/* calculate the number of bytes left in the temp buffer */
	if (bytes + conn->used > HTTPBUFFERSIZE)
		bytes = HTTPBUFFERSIZE - conn->used;

	if (bytes <= 0) {
		read_report(conn);
		report_badrequest(conn, "Request is too big for this server");
		return;
	}

	/* read some bytes from the socket */
	if ((bytes = webjames_readbuffer(conn, conn->buffer+conn->used, bytes))<0) return;
	conn->used += bytes;

	do {
		/* attempt to read a line from the buffer */
		bytes = readline(conn->buffer, conn->used, line);
		if (bytes <= 0)  return;

		/* remove the used bytes from the buffer */
		if (conn->used > bytes)
			memmove(conn->buffer, conn->buffer+bytes, conn->used-bytes);
		conn->used -= bytes;

		/* if we need to keep the header (for a cgi-script) */
		if ((conn->header) && (*line)) {
			int i;

			if (conn->headersize > 1024*configuration.maxrequestsize) {
				read_report(conn);
				report_badrequest(conn, "Request is too big for this server");
				return;
			}
			if (conn->headersize + bytes + 1 > conn->headerallocated) {
				/* attempt to increase the size of the header-buffer; increase by */
				/* more than necessary, so we don't have to realloc() each time */
				conn->headerallocated += bytes + 1024;
				newptr = realloc(conn->header, conn->headerallocated);
				if (!newptr) {
					read_report(conn);
					report_busy(conn, "No room for header");
					return;
				}
				conn->header= newptr;
			}
			i = 0;
			do {
				conn->header[conn->headersize+i] = line[i];
				i++;
			} while (line[i] >= ' ');
			conn->headersize += i;
			conn->header[conn->headersize++] = '\n';
		}

		/* remove CR/LF and make an uppercase copy */
		for (i = 0; i < bytes; i++) {
			if (line[i] < 32)  line[i] = '\0';
			upper[i] = toupper(line[i]);
		}

		/* process the line */
		if (!conn->requestline) {

			char *ptr, *file, *http;
			int query=0;
			size_t len;

			/* save the requestline for the clf-log */
			len=strlen(line)+1;
			conn->requestline = malloc(len);
			if (!conn->requestline) {
				read_report(conn);
				report_busy(conn, "No room for requestline");
				return;
			}
			memcpy(conn->requestline, line, len);

			file = line;
			while ((*file != ' ') && (*file))  file++;  /* skip until SPACE */
			while (*file == ' ')  file++;               /* skip until /filename */
			ptr = file;
			while ((*ptr != ' ') && (*ptr))  ptr++;     /* skip over the filename */
			if (*ptr == ' ')  *ptr++ = '\0';            /* terminate /filename */
			while (*ptr == ' ')  ptr++;                 /* skip until HTTP/ */
			http = ptr;                                 /* make uppercase copy */
			while (*ptr) {                              /* of HTTP version */
				*ptr = toupper(*ptr);
				ptr++;
			}

			conn->httpmajor = 0;                        /* simple request */
			conn->httpminor = 9;
			if (strncmp(http, "HTTP/", 5) == 0) {       /* read version number */
				char *dot;

				conn->httpmajor = conn->httpminor = 0;
				dot = strchr(http+5, '.');
				if (dot) {
					*dot++ = '\0';
					conn->httpmajor = atoi(http+5);
					conn->httpminor = atoi(dot);
				}
				if ((conn->httpmajor < 1) && (conn->httpminor < 9)) {
					read_report(conn);
					report_badrequest(conn, "HTTP version < 0.9");
					return;
				}
			}
			len=strlen(file)+2;
			conn->uri = malloc(len);         /* get buffer for filename */
			if (!conn->uri) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			conn->requesturi = malloc(len);  /* get buffer for filename */
			if (!conn->requesturi) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}

			wjstrncpy(conn->requesturi,file,len);
			/*Copy uri*/
			i=0;
			while (*file!='\0') {
				if (file[0]=='?') query=1; /*Don't convert %xx sequences in the query string part of the uri*/
				if (!query && file[0]=='%' && isxdigit(file[1]) && isxdigit(file[2])) {
					/* Treat next two chars as hex code of character */
					char num[3],*end;
					num[0] = file[1];
					num[1] = file[2];
					num[2] = 0;
					conn->uri[i] = (char)strtol(num,&end,16);
					/* Convert spaces to hard spaces */
					if (conn->uri[i] == 32) conn->uri[i] = 160;
					i++;
					file+=3;
				} else {
					conn->uri[i++]=*(file++);
				}
			}
			conn->uri[i] = '\0';

			if (!configuration.casesensitive) {
				for (i = 0; conn->uri[i] && (conn->uri[i] != '?'); i++) {
					conn->uri[i] = tolower(conn->uri[i]);     /* lower-case until args */
				}
			}

			if (strncmp(upper, "GET ", 4) == 0) {
				conn->method = METHOD_GET;
				conn->methodstr="GET";
			} else if (strncmp(upper, "POST ", 5) == 0) {
				conn->method = METHOD_POST;
				conn->methodstr="POST";
			} else if (strncmp(upper, "HEAD ", 5) == 0) {
				conn->method = METHOD_HEAD;
				conn->methodstr="HEAD";
			} else if (strncmp(upper, "DELETE ", 7) == 0) { /* DELETE request */
				if (!*configuration.delete_script) {
					read_report(conn);
					report_notfound(conn);
					return;
				}
				conn->method = METHOD_DELETE;
				conn->methodstr="DELETE";
				len=strlen(configuration.delete_script)+1;
				ptr = malloc(len);
				if (!ptr) {
					read_report(conn);
					report_busy(conn, "Memory low");
					return;
				}
				if (conn->uri) free(conn->uri);
				conn->uri = ptr;
				memcpy(conn->uri, configuration.delete_script,len);

				if (!configuration.casesensitive) {
					for (i = 0; conn->uri[i]; i++) conn->uri[i] = tolower(conn->uri[i]);  /* must be lowercase */
				}

			} else if (strncmp(upper, "PUT ", 4) == 0) {
				if (!*configuration.put_script) {
					read_report(conn);
					report_notfound(conn);
					return;
				}
				conn->method = METHOD_PUT;
				conn->methodstr="PUT";
				len=strlen(configuration.put_script)+1;
				ptr = malloc(len);
				if (!ptr) {
					read_report(conn);
					report_busy(conn, "Memory low");
					return;
				}
				if (conn->uri) free(conn->uri);
				conn->uri = ptr;
				memcpy(conn->uri, configuration.put_script,len);

				if (!configuration.casesensitive) {
					for (i = 0; conn->uri[i]; i++) conn->uri[i] = tolower(conn->uri[i]);  /* must be lowercase */
				}

			} else { /* OPTIONS TRACE LINK UNLINK PATCH */
				char *space;

				read_report(conn);
				space = strchr(upper, ' ');
				if (space)   *space = '\0';
				report_notimplemented(conn, upper);
				return;
			}

			if (conn->httpmajor < 1) {                  /* HTTP 0.9 */
				donereading(conn);
				return;
			}

			conn->header = malloc(HTTPBUFFERSIZE);    /* allocate header-buffer */
			if (conn->header) {
				snprintf(conn->header, HTTPBUFFERSIZE, "%s\n", conn->requestline);
				conn->headersize = strlen(conn->header);
				conn->headerallocated = HTTPBUFFERSIZE;
			} else {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}

		} else if (strncmp(upper, "ACCEPT: ", 8) == 0) {
			if (conn->accept)  free(conn->accept);
			len=strlen(line+8)+1;
			conn->accept = malloc(len);
			if (conn->accept==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->accept, line+8,len);

		} else if (strncmp(upper, "ACCEPT-LANGUAGE: ", 17) == 0) {
			if (conn->acceptlanguage)  free(conn->acceptlanguage);
			len=strlen(line+17)+1;
			conn->acceptlanguage = malloc(len);
			if (conn->acceptlanguage==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->acceptlanguage, line+17, len);

		} else if (strncmp(upper, "ACCEPT-ENCODING: ", 17) == 0) {
			if (conn->acceptencoding)  free(conn->acceptencoding);
			len=strlen(line+17)+1;
			conn->acceptencoding = malloc(len);
			if (conn->acceptencoding==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->acceptencoding, line+17, len);

		} else if (strncmp(upper, "ACCEPT-CHARSET: ", 17) == 0) {
			if (conn->acceptcharset)  free(conn->acceptcharset);
			len=strlen(line+17)+1;
			conn->acceptcharset = malloc(len);
			if (conn->acceptcharset==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->acceptcharset, line+17, len);

		} else if (strncmp(upper, "CONTENT-LENGTH: ", 16) == 0) {
			conn->bodysize = atoi(upper+16);
			if (conn->bodysize < 0) {
				read_report(conn);
				report_badrequest(conn, "negative request-body size");
				return;
			}
			if (conn->bodysize > 1024*configuration.maxrequestsize) {
				read_report(conn);
				report_badrequest(conn, "Request is too big for this server");
				return;
			}

		} else if (strncmp(upper, "CONTENT-TYPE: ", 14) == 0) {
			if (conn->type)  free(conn->type);
			len=strlen(upper+14)+1;
			conn->type = malloc(len);
			if (conn->type==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->type, line+14, len);

		} else if (strncmp(upper, "COOKIE: ", 8) == 0) {
			if (conn->cookie)  free(conn->cookie);
			len=strlen(line+8)+1;
			conn->cookie = malloc(len);
			if (conn->cookie==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->cookie, line+8,len);

		} else if (strncmp(upper, "REFERER: ", 9) == 0) {
			if (conn->referer)  free(conn->referer);
			len=strlen(line+9)+1;
			conn->referer = malloc(len);
			if (conn->referer==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->referer, line+9,len);
			webjames_writelog(LOGLEVEL_REFERER, "%s", line);

		} else if (strncmp(upper, "FROM: ", 6) == 0) {
			webjames_writelog(LOGLEVEL_FROM, "%s", line);

		} else if (strncmp(upper, "USER-AGENT: ", 12) == 0) {
			if (conn->useragent)  free(conn->useragent);
			len=strlen(line+12)+1;
			conn->useragent = malloc(len);
			if (conn->useragent==NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			memcpy(conn->useragent, line+12,len);
			webjames_writelog(LOGLEVEL_USERAGENT, "%s", line);

		} else if (strncmp(upper, "HOST: ", 6) == 0) {
			if (conn->host)  free(conn->host);
			len=strlen(line+6)+1;
			conn->host = malloc(len);
			if (conn->host == NULL) {
				read_report(conn);
				report_busy(conn, "Memory low");
				return;
			}
			for (i = 0; i < len; i++) conn->host[i] = tolower(line[i+6]);
			webjames_writelog(LOGLEVEL_FROM, "%s", line);

		} else if (strncmp(upper, "AUTHORIZATION: ", 15) == 0) {
			int pos, bytes;
			pos = 15;
			while (upper[pos] == ' ')  pos++;
			if (strncmp(upper+pos, "BASIC ", 6)) {
				read_report(conn);
				report_badrequest(conn, "unsupported authorization scheme");
				return;
			} else {
				pos += 6;
				while (line[pos] == ' ')  pos++;
				if (conn->authorization)  free(conn->authorization);
				conn->authorization = malloc(strlen(line+pos)+1);
				if (conn->authorization) {
					bytes = decode_base64(conn->authorization, line+pos, strlen(line+pos));
					conn->authorization[bytes] = 0;
					if ((bytes > 100) || (!strchr(conn->authorization, ':'))) {
						read_report(conn);
						report_badrequest(conn, "error in authorization");
						return;
					}
				} else {
					read_report(conn);
					report_busy(conn, "No room for Authorization: field");
					return;
				}
			}

		} else if (strncmp(upper, "IF-MODIFIED-SINCE: ", 19) == 0) {
			rfc_to_time(upper+19, &conn->if_modified_since);

		} else if (*line == '\0') {               /* end of header */

			if (conn->bodysize >= 0) {              /* if there is a body... */
				conn->body = malloc(conn->bodysize+4);
				if (!conn->body) {
					/* if malloc failed :-( */
					read_report(conn);
					report_busy(conn, "No room for the body");
					return;
				}
				if (conn->used > conn->bodysize) {
					/* HELP! We've already read more than we should! */
					/* But Oregano and some others send an extra CRLF at the end of the body*/
					/* So pretend that the body size matches the Content-Length: header */
					conn->used = conn->bodysize;
				}
				conn->status = WJ_STATUS_BODY;
				if (conn->used) {
					memcpy(conn->body, conn->buffer, conn->used);
					if (conn->used == conn->bodysize)  donereading(conn);
				}
				return;


			} else {                                /* no body, so we're done!! */

				if ((conn->method == METHOD_POST) || (conn->method == METHOD_PUT)) {
					/* POST/PUT requests must include infoabout the size of the body */
					read_report(conn);
					report_badrequest(conn, "no Content-length: field in the request");
					return;
				}



				donereading(conn);
				return;
			}

		} else {
			/* ignore unrecognised header field */
		}
#ifdef LOG
		for (i=0;i<configuration.logheaders;i++) {
			if (strncmp(upper, configuration.logheader[i], strlen(configuration.logheader[i])) == 0) {
				webjames_writelog(LOGLEVEL_HEADER, "%s", line);
			}
		}
#endif

	} while (conn->used > 0);
}



void pollread(int cn) {
/* called repeatedly until the header (and body) has been read */

	struct connection *conn;
	int bytes;
	os_error *err;

	conn = connections[cn];
	/* check if there's any data ready on the socket */
	bytes = ip_ready(conn->socket,&err);
	if (err) {
		if (CHECK_INET_ERR(err->errnum,socket_EPIPE)) {
			webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
		} else {
			webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s",err->errmess);
		}
		conn->close(conn,1);
		return;
	} else if (bytes < 0) {
		webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
		conn->close(conn,1);
		return;
	} else if (bytes==0) return;

	conn->timeoflastactivity = clock();

	if (conn->status == WJ_STATUS_HEADER)
		pollread_header(conn, bytes);       /* if we're reading the header */
	else if (conn->status == WJ_STATUS_BODY)
		pollread_body(conn, bytes);         /* if we're reading the body */
}
