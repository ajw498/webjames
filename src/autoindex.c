/*
	$Id: $
	autoindex handler
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include "oslib/osgbpb.h"

#include "webjames.h"
#include "wjstring.h"
#include "stat.h"
#include "report.h"
#include "datetime.h"
#include "autoindex.h"

#ifdef AUTOINDEX

#define BUFINCR 5*1024
#define LEAFNAME_MAX 1024
#define TRUNCATE_LEN 30


/* Add a string to the buffer, increasing the buffer size if necessary */
static int add_to_buffer(char *str, int *buflen, struct connection *conn)
{
	int len = strlen(str);

	if (len == 0 || len > BUFINCR) return 0;

	if (conn->leftinbuffer + len > *buflen) {
		char *newbuf;
		
		newbuf = EM(realloc(conn->filebuffer, *buflen + BUFINCR));
		if (newbuf == NULL) {
			free(conn->filebuffer);
			conn->filebuffer = NULL;
			return 1;
		}
		conn->filebuffer = newbuf;
		*buflen += BUFINCR;
	}

	memcpy(conn->filebuffer + conn->leftinbuffer, str, len);
	conn->leftinbuffer += len;

	return 0;
}

/* Swap / to . and possibly uri encode a filename */
/* If display is set it won't encode, and will truncate */
/* Returns a pointer to a static buffer */
static char *uri_encode(char *filename, int display, int *len)
{
	static char buffer[LEAFNAME_MAX * 3];
	char *uri = buffer;

	while (*filename && (uri + 4 < buffer + sizeof(buffer))) {
		if (*filename == '/') {
			*uri++ = '.';
		} else if (display || isalnum(*filename)) {
			*uri++ = *filename;
		} else {
			int ch = *filename & 0xFF;
			*uri++ = '%';
			snprintf(uri, 3, "%.2x", ch);
			uri += 2;
		}
		filename++;
		if (display && uri - buffer > TRUNCATE_LEN) break;
	}

	*uri = '\0';

	if (len) *len = uri - buffer;

	return buffer;
}

/* Formats a filesize into a human readable format */
/* Returns a pointer to a static buffer */
static char *format_size(unsigned int size)
{
	static char buffer[6];
	char suffix;

	if (size & 0xC0000000) {
		size >>= 30;
		suffix = 'G';
	} else if (size & 0xFFF00000) {
		size >>= 20;
		suffix = 'M';
	} else {
		size >>= 10;
		if (size < 1) size = 1;
		suffix = 'k';
	}
	snprintf(buffer, sizeof(buffer), "%4d%c", size, suffix);

	return buffer;
}

/* Start handler. Generate the listing into a buffer */
void autoindex_start(struct connection *conn)
{
	int buflen = 0;

	conn->fileused = 0;
	conn->file = NULL;
	conn->filebuffer = NULL;
	conn->positioninbuffer = 0;
	conn->flags.releasefilebuffer = 1;
	conn->leftinbuffer = 0;

	if (conn->method != METHOD_HEAD) {
		char buffer[LEAFNAME_MAX];
		osgbpb_info_list *entry = (osgbpb_info_list *)buffer;
		int read;
		int start = 0;

		add_to_buffer("<html><head><title>Index of ", &buflen, conn);
		add_to_buffer(conn->uri, &buflen, conn);
		add_to_buffer("</title></head><body bgcolor=\"#ffffff\" text=\"#000000\"><b>Index of ", &buflen, conn);
		add_to_buffer(conn->uri, &buflen, conn);
		add_to_buffer("</b><p><pre>Name                            Last modified       Size\n<hr>\n", &buflen, conn);

		if (strcmp(conn->uri, "/") != 0) add_to_buffer("<a href=\"../\">Parent Directory</a>\n", &buflen, conn);

		do {
			if (E(xosgbpb_dir_entries_info(conn->filename, entry, 1, start, sizeof(buffer), NULL, &read, &start))) {
				/* Terminate reading of the directory on an error */
				read = 0;
				start = -1;
			}

			if (read) {
				int len;
				int i;
				os_date_and_time utc;
				struct tm localtime;
				char timebuf[20];

				add_to_buffer("<a href=\"", &buflen, conn);
				add_to_buffer(uri_encode(entry->info[0].name, 0, NULL), &buflen, conn);
				if (entry->info[0].obj_type == fileswitch_IS_DIR) {
					add_to_buffer("/", &buflen, conn);
				}
				add_to_buffer("\">", &buflen, conn);
				add_to_buffer(uri_encode(entry->info[0].name, 1, &len), &buflen, conn);
				if (entry->info[0].obj_type == fileswitch_IS_DIR) {
					add_to_buffer("/", &buflen, conn);
					len++;
				}
				add_to_buffer("</a>", &buflen, conn);
				for (i = len; i < TRUNCATE_LEN + 2; i++) add_to_buffer(" ", &buflen, conn);
				memcpy(&utc, &(entry->info[0].exec_addr), 4);
				utc[4] = entry->info[0].load_addr & 0xFF;
				utc_to_localtime(&utc, &localtime);
				strftime(timebuf, sizeof(timebuf), "%d-%b-%Y %H:%M  ", &localtime);
				add_to_buffer(timebuf, &buflen, conn);
				if (entry->info[0].obj_type != fileswitch_IS_DIR) {
					add_to_buffer(format_size(entry->info[0].size), &buflen, conn);
				}
				add_to_buffer("\n", &buflen, conn);
			}
		} while (start != -1);

                add_to_buffer("</pre><hr>" WEBJAMES_SERVER_SOFTWARE "</body></html>", &buflen, conn);
	}

	conn->fileinfo.size = conn->leftinbuffer;

	if (conn->flags.outputheaders) {
		time_t now;
		char rfcnow[50];
		int i;

		webjames_writestringr(conn, "HTTP/1.0 200 OK\r\n");

		time(&now);
		time_to_rfc(localtime(&now),rfcnow);
		snprintf(temp, TEMPBUFFERSIZE, "Date: %s\r\n", rfcnow);
		webjames_writestringr(conn, temp);
		for (i = 0; i < configuration.xheaders; i++) {
			webjames_writestringr(conn, configuration.xheader[i]);
			webjames_writestringr(conn, "\r\n");
		}
		if (configuration.server[0]) {
			snprintf(temp, TEMPBUFFERSIZE, "Server: %s\r\n", configuration.server);
			webjames_writestringr(conn, temp);
		}
		webjames_writestringr(conn, "\r\n");
	}
	if (conn->method==METHOD_HEAD) conn->close(conn,0);

}

#endif
