#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "ip.h"
#include "openclose.h"
#include "stat.h"
#include "report.h"
#include "cache.h"
#include "staticcontent.h"

void staticcontent_start(struct connection *conn)
{
	if ((conn->method == METHOD_GET) || (conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE)) {
		/* was there a valid If-Modified-Since field in the request?? */
		if (conn->if_modified_since.tm_year > 0) {
			if (compare_time(&conn->fileinfo.date, &conn->if_modified_since) < 0) {
				report_notmodified(conn);
				return;
			}
		}
		/* if everything OK until now, prepare to send file */
		conn->fileused = 0;
		if (conn->cache) {
			conn->filebuffer = conn->cache->buffer;
			conn->fileinfo.size = conn->cache->size;
			conn->flags.releasefilebuffer = 0;
			conn->file = NULL;
			conn->cache->accesses++;

		} else {
			FILE *handle;
			
			handle = fopen(conn->filename, "rb");
			if (!handle) {
				report_notfound(conn);
				return;
			}
			/* attempt to get a read-ahead buffer for the file */
			/* notice: things will still work if malloc fails */
			conn->filebuffer = malloc(readaheadbuffer*1024);
			conn->flags.releasefilebuffer = 1;
			conn->leftinbuffer = 0;
			/* set the fields in the structure, and that's it! */
			conn->file = handle;
		}
	}

	if (conn->httpmajor >= 1) {
		int i;
		/* write header */
		writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
		sprintf(temp, "Content-Length: %d\r\n", conn->fileinfo.size);
		writestring(conn->socket, temp);
		sprintf(temp, "Content-Type: %s\r\n", conn->fileinfo.mimetype);
		writestring(conn->socket, temp);
		for (i = 0; i < configuration.xheaders; i++) {
			writestring(conn->socket, configuration.xheader[i]);
			writestring(conn->socket, "\r\n");
		}
		sprintf(temp, "Server: %s\r\n\r\n", configuration.server);
		writestring(conn->socket, temp);
	}
}

int staticcontent_poll(struct connection *conn,int maxbytes) {
/* attempt to write a chunk of data from the file (bandwidth-limited) */
/* close the connection if EOF is reached */
/* return the number of bytes written */
	int bytes = 0;
	char temp[HTTPBUFFERSIZE];

	if (conn->fileused < conn->fileinfo.size) {
		/* send a bit more of the file or buffered data */

		/* find max no. of bytes to send */
		bytes = conn->fileinfo.size - conn->fileused;
		if (conn->file) {
			if (conn->filebuffer) {
				if (bytes > readaheadbuffer*1024)  bytes = readaheadbuffer*1024;
			} else {
				if (bytes > HTTPBUFFERSIZE)  bytes = HTTPBUFFERSIZE;
			}
		}

		if (maxbytes > 0 && bytes > maxbytes)  bytes = maxbytes;

		/* read from buffer or from file */
		if (conn->file) {   /* read from file (possibly through temp buffer) */
			if (conn->filebuffer) {
				/*  IDEA: if there's less than 512 bytes left in the buffer, */
				/* move those to the start of the buffer, and refill; that may */
				/* be faster than sending the 512 bytes on their own */
				if (conn->leftinbuffer == 0) {
					conn->leftinbuffer = fread(conn->filebuffer, 1,readaheadbuffer*1024, conn->file);
					conn->positioninbuffer = 0;
				}
				if (bytes > conn->leftinbuffer)  bytes = conn->leftinbuffer;
				bytes = ip_write(conn->socket, conn->filebuffer + conn->positioninbuffer,bytes);
				conn->positioninbuffer += bytes;
				conn->leftinbuffer -= bytes;
			} else {
				fseek(conn->file, conn->fileused, SEEK_SET);
				bytes = fread(temp, 1, bytes, conn->file);
				bytes = ip_write(conn->socket, temp, bytes);
			}
		} else {            /* read from buffer */
			bytes = ip_write(conn->socket, conn->filebuffer+conn->fileused, bytes);
		}

		conn->timeoflastactivity = clock();

		if (bytes < 0) {
			/* errorcode = -bytes; */
			*temp = '\0';
			switch (-bytes) {
			case IPERR_BROKENPIPE:
#ifdef LOG
				writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
#endif
				break;
			}
			close(conn->index, 1);           /* close the connection, with force */
			return 0;
		} else {
			conn->fileused += bytes;
		}
	}


	/* if EOF reached, close */
	if (conn->fileused >= conn->fileinfo.size)   close(conn->index, 0);

	return bytes;
}

