/*
	$Id: sendasis.c,v 1.7 2001/09/18 20:58:01 AJW Exp $
	send-as-is handler
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

#include "webjames.h"
#include "wjstring.h"
#include "stat.h"
#include "report.h"
#include "datetime.h"
#include "cache.h"
#include "sendasis.h"

#ifdef SENDASIS

#define MAXHEADERS 100

void sendasis_start(struct connection *conn)
{
	char headerbuf[TEMPBUFFERSIZE];
	char *headers[MAXHEADERS];
	char *headerbuffer;
	int headerbufferlength;
	int location=0;
	char *status=0;
	int i,j;

	if (conn->method != METHOD_POST) {
		/* was there a valid If-Modified-Since field in the request?? */
		if (conn->if_modified_since.tm_year > 0) {
			if (compare_time(&conn->fileinfo.date, &conn->if_modified_since) < 0) {
				report_notmodified(conn);
				return;
			}
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

		headerbuffer = headerbuf;
		headerbufferlength = TEMPBUFFERSIZE<conn->fileinfo.size ? TEMPBUFFERSIZE : conn->fileinfo.size;
		memcpy(headerbuffer,conn->filebuffer,headerbufferlength);

	} else {
		FILE *handle;
		
		handle = fopen(conn->filename, "rb");
		if (!handle) {
			report_notfound(conn);
			return;
		}
		/* attempt to get a read-ahead buffer for the file */
		/* notice: things will still work if malloc fails */
		conn->filebuffer = EM(malloc(configuration.readaheadbuffer*1024));
		conn->flags.releasefilebuffer = 1;
		conn->leftinbuffer = 0;
		/* set the fields in the structure, and that's it! */
		conn->file = handle;

		/*read the first chunk of the file*/
		if (conn->filebuffer) {
			headerbuffer=conn->filebuffer;
			headerbufferlength=conn->leftinbuffer = fread(conn->filebuffer, 1, configuration.readaheadbuffer*1024, conn->file);
			conn->positioninbuffer = 0;
		} else {
			headerbuffer=headerbuf;
			headerbufferlength=fread(headerbuffer, 1, TEMPBUFFERSIZE, conn->file);
		}
	}

	for (i=0;i<MAXHEADERS;i++) headers[i]=NULL;

	/*set headers[] to point to start of each header line*/
	for (i=0,j=0;i<MAXHEADERS;i++) {
		headers[i]=headerbuffer+j;
		while (j<headerbufferlength && headerbuffer[j]!='\r' && headerbuffer[j]!='\n') j++; /*find end of line*/
		if (j>=headerbufferlength-1) {
			/*we have reached the end of the buffer before finding the end of the headers, so reset everything and don't bother to parse the headers*/
			for (i=0;i<MAXHEADERS;i++) headers[i]=NULL;
			j=0;
			break;
		}
		if (headerbuffer[j]!=headerbuffer[j+1]) {
			headerbuffer[j++]='\0';
			if (headerbuffer[j]=='\r' || headerbuffer[j]=='\n') j++; /*supports CR,LF,CRLF and LFCR*/
			if (headers[i][0]=='\0') {
				/*end of headers reached*/
				headers[i]=NULL;
				break;
			}
		} else {
			/*either a \r\r or a \n\n so must be the end of the headers*/
			headerbuffer[j++]='\0';
			break;
		}
	}

	/*set so that staticcontent_poll will carry on from the end of the headers*/
	conn->fileused=j;
	conn->positioninbuffer=j;
	conn->leftinbuffer-=j;

	if (conn->flags.outputheaders) {
		time_t now;
		char rfcnow[50];

		for (i=0;i<MAXHEADERS;i++) {
			if (headers[i]) {
				/*overwrite any Date: or Server: headers from the script*/
				if (wjstrnicmp(headers[i],"Date:",5)==0) {
					headers[i]=NULL;
				} else if (wjstrnicmp(headers[i],"Server:",7)==0) {
					headers[i]=NULL;
				} else if (wjstrnicmp(headers[i],"Location:",9)==0) {
					location=1;
				} else if (wjstrnicmp(headers[i],"Status:",7)==0) {
					status=headers[i]+7;
					while (isspace(*status)) status++;
					headers[i]=NULL;
				} else if (wjstrnicmp(headers[i],"HTTP/",5)==0) {
					status=headers[i];
					headers[i]=NULL;
				}
			}
		}

		/*if there was a Location: header, then output the status as 302, unless the script explicitly gave a status code*/
		if (status) {
			if (wjstrnicmp(status,"HTTP/",5)==0) {
				snprintf(temp, TEMPBUFFERSIZE, "%s\r\n",status);
			} else {
				snprintf(temp, TEMPBUFFERSIZE, "HTTP/1.0 %s\r\n",status);
			}
			webjames_writestringr(conn,temp);
		} else if (location) {
			webjames_writestringr(conn, "HTTP/1.0 302 Moved Temporarily\r\n");
		} else {
			webjames_writestringr(conn, "HTTP/1.0 200 OK\r\n");
		}

		time(&now);
		time_to_rfc(localtime(&now),rfcnow);
		snprintf(temp, TEMPBUFFERSIZE, "Date: %s\r\n", rfcnow);
		webjames_writestringr(conn, temp);
		if (conn->vary[0]) {
			snprintf(temp, TEMPBUFFERSIZE, "Vary:%s\r\n", conn->vary);
			webjames_writestringr(conn, temp);
		}
		for (i = 0; i < configuration.xheaders; i++) {
			webjames_writestringr(conn, configuration.xheader[i]);
			webjames_writestringr(conn, "\r\n");
		}
		for (i=0;i<MAXHEADERS;i++) {
			if (headers[i]) {
				snprintf(temp, TEMPBUFFERSIZE, "%s\r\n",headers[i]);
				webjames_writestringr(conn,temp);
			}
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
