/*
	$Id$
	CGI script handler
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/os.h"
#include "oslib/osfile.h"
#include "oslib/osfscontrol.h"
#include "oslib/osmodule.h"
#include "oslib/wimp.h"

#include "webjames.h"
#include "wjstring.h"
#include "datetime.h"
#include "cgiscript.h"
#include "openclose.h"
#include "report.h"
#include "stat.h"
#include "ip.h"
#include "write.h"
#include "handler.h"

#define MAXHEADERS 100
#define SETVARSBUFSIZE 30

void cgiscript_setvars(struct connection *conn)
/*set system variables for a CGI or SSI script*/
{
	char buffer[SETVARSBUFSIZE];
	if (configuration.server[0])  set_var_val("SERVER_SOFTWARE", configuration.server);
	set_var_val("SERVER_PROTOCOL", conn->protocol);
	if (conn->vhost->domain[0])  set_var_val("SERVER_NAME", conn->vhost->domain);
	set_var_val("SERVER_ADMIN", conn->vhost->serveradmin);

	set_var_val("DOCUMENT_ROOT", conn->vhost->homedir);

	set_var_val("GATEWAY_INTERFACE", "CGI/1.1");
	if (conn->requesturi) set_var_val("REQUEST_URI", conn->requesturi);

	set_var_val("SCRIPT_NAME", conn->uri);
	set_var_val("PATH_TRANSLATED", conn->filename);

	if (conn->args) set_var_val("QUERY_STRING", conn->args);

	snprintf(buffer, SETVARSBUFSIZE, "%d.%d.%d.%d", conn->ipaddr[0], conn->ipaddr[1], conn->ipaddr[2], conn->ipaddr[3]);
	set_var_val("REMOTE_ADDR", buffer);
	if (conn->dnsstatus == DNS_OK)  set_var_val("REMOTE_HOST", conn->remotehost);

	set_var_val("REQUEST_METHOD", conn->methodstr);

	if ((conn->method == METHOD_POST) || (conn->method == METHOD_PUT)) {
		snprintf(buffer, SETVARSBUFSIZE, "%d", conn->bodysize);
		set_var_val("CONTENT_LENGTH", buffer);
		set_var_val("CONTENT_TYPE", conn->type);
	}

	snprintf(buffer, SETVARSBUFSIZE, "%d", conn->port);
	set_var_val("SERVER_PORT", buffer);

	if ((conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE))
		set_var_val("ENTITY_PATH", conn->requesturi);

	if (conn->pwd) {
		set_var_val("AUTH_TYPE", "basic");
		set_var_val("REMOTE_USER", conn->authorization);
	} else {
		set_var_val("AUTH_TYPE", "");
		set_var_val("REMOTE_USER", "");
	}

	if (conn->cookie) set_var_val("HTTP_COOKIE", conn->cookie);
	if (conn->useragent) set_var_val("HTTP_USER_AGENT", conn->useragent);
	if (conn->referer) set_var_val("HTTP_REFERER", conn->referer);
	if (conn->accept) set_var_val("HTTP_ACCEPT", conn->accept);
	if (conn->acceptlanguage) set_var_val("HTTP_ACCEPT_LANGUAGE", conn->acceptlanguage);
	if (conn->acceptcharset) set_var_val("HTTP_ACCEPT_CHARSET", conn->acceptcharset);
	if (conn->acceptencoding) set_var_val("HTTP_ACCEPT_ENCODING", conn->acceptencoding);
}

void cgiscript_removevars(void)
/* remove all variables set by cgiscript_setvars()*/
{
	remove_var("SERVER_SOFTWARE");
	remove_var("SERVER_PORT");
	remove_var("SERVER_PROTOCOL");
	remove_var("SERVER_NAME");
	remove_var("SERVER_ADMIN");
	remove_var("DOCUMENT_ROOT");
	remove_var("GATEWAY_INTERFACE");
	remove_var("REQUEST_URI");
	remove_var("SCRIPT_NAME");
	remove_var("PATH_TRANSLATED");
	remove_var("QUERY_STRING");
	remove_var("REMOTE_ADDR");
	remove_var("REMOTE_HOST");
	remove_var("REQUEST_METHOD");
	remove_var("CONTENT_LENGTH");
	remove_var("CONTENT_TYPE");
	remove_var("SERVER_PORT");
	remove_var("ENTITY_PATH");
	remove_var("AUTH_TYPE");
	remove_var("REMOTE_USER");
	remove_var("HTTP_COOKIE");
	remove_var("HTTP_USER_AGENT");
	remove_var("HTTP_REFERER");
	remove_var("HTTP_ACCEPT");
	remove_var("HTTP_ACCEPT_LANGUAGE");
	remove_var("HTTP_ACCEPT_CHARSET");
	remove_var("HTTP_ACCEPT_ENCODING");
}

void cgiscript_start(struct connection *conn)
/* start a CGI script with redirection */
{
	char cmd[MAX_FILENAME], cmdformat[MAX_FILENAME];
	int size, unix;
	FILE *file;
	wimp_t task;
	char headerbuf[TEMPBUFFERSIZE];
	char *headers[MAXHEADERS];
	char *headerbuffer;
	int headerbufferlength;
	int location=0;
	char *status=0;
	int i,j;
	char cgiin[MAX_FILENAME]="", cgiout[MAX_FILENAME];
	static unsigned int cgiserial = 0;

	/* change currently selected directory if required */
	if (conn->flags.setcsd) {
		char dirname[MAX_FILENAME], *dot;

		wjstrncpy(dirname,conn->filename,MAX_FILENAME);
		dot = strrchr(dirname,'.');
		if (dot != NULL) {
			*dot = '\0';
			EV(xosfscontrol_dir(dirname));
		}
	}

	/* set up the system variables */
	cgiscript_setvars(conn);

	wjstrncpy(cmdformat,"*%s",MAX_FILENAME);
	unix = 0;
	if (conn->handler) {
		if (conn->handler->unix) {
			unix = 1;
		}
		if (conn->handler->command) {
			wjstrncpy(cmdformat,conn->handler->command,MAX_FILENAME);
		}
	}

	/*Get filename to use for output redirection.*/
	/*Should be unique, to allow a script to start whilst the previous one's output is still being served*/
	if (*configuration.cgi_out != '\0') {
		wjstrncpy(cgiout, configuration.cgi_out, MAX_FILENAME);
	} else {
		snprintf(cgiout, MAX_FILENAME, "%s.%u", configuration.cgi_dir, cgiserial++);
	}

	if (conn->method == METHOD_POST) {
		/* generate input filename */
		if (*configuration.cgi_in != '\0') {
			wjstrncpy(cgiin, configuration.cgi_in, MAX_FILENAME);
		} else {
			snprintf(cgiin, MAX_FILENAME, "%s.%u", configuration.cgi_dir, cgiserial++);
		}
		EV(xosfile_save_stamped(cgiin, 0xfff, (byte *)conn->body, (byte *)conn->body+conn->bodysize));

		/* build command with redirection */
		if (unix) wjstrncat(cmdformat," < %s > %s",MAX_FILENAME); else wjstrncat(cmdformat," { < %s > %s }",MAX_FILENAME);
		snprintf(cmd, MAX_FILENAME, cmdformat, conn->filename, cgiin, cgiout);
	} else {
		if (strchr(cmdformat,'%')) {
			if (unix) wjstrncat(cmdformat," > %s",MAX_FILENAME); else wjstrncat(cmdformat," { > %s }",MAX_FILENAME);
			snprintf(cmd, MAX_FILENAME, cmdformat, conn->filename, cgiout);
		} else {
			if (unix) wjstrncat(cmdformat," > %s",MAX_FILENAME); else wjstrncat(cmdformat," { > %s }",MAX_FILENAME);
			snprintf(cmd, MAX_FILENAME, cmdformat, cgiout);
		}
	}

	/* execute the cgi-script */
	if (E(xwimp_start_task(cmd, &task))) {
		/* failed to start cgi-script */
		if (*cgiin) remove(cgiin);
		report_servererr(conn, "script couldn't be started");
		return;
	}

	webjames_writelog(LOGLEVEL_CGISTART, "%s", cmd);

	/* remove cgi-spool-input file (if any) */
	if (*cgiin) remove(cgiin);

	/* Remove all system variables that were set */
	cgiscript_removevars();

	/* Set the CSD back to what it was if we changed it */
	if (conn->flags.setcsd) EV(xosfscontrol_back());

	/* disable reading */
	if (ip_fd_is_set(serverinfo.select_read, conn->socket)) {
		ip_fd_clear(serverinfo.select_read, conn->socket);
		serverinfo.readcount--;
	}

	/* already set up for writing (this is done in write.c) */

	/* read filesize */
	if (get_file_info(cgiout, NULL, NULL, NULL, &size, 0) < 0) {
		report_servererr(conn, "error occured when reading file info");
		return;
	}

	/* open file for reading */
	file = fopen(cgiout, "rb");
	if (!file) {
		report_notfound(conn);
		return;
	}


	/* attempt to get a read-ahead buffer for the file */
	/* notice: things will still work if malloc fails */
	conn->filebuffer = EM(malloc(configuration.readaheadbuffer*1024));
	conn->flags.releasefilebuffer = 1;    /* ignored if filebuffer == NULL */
	conn->leftinbuffer = 0;

	conn->flags.deletefile = 1;           /* delete the file when done */
	wjstrncpy(conn->filename, cgiout, MAX_FILENAME);

	conn->flags.is_cgi = 0;               /* make close() output clf-info */

	/* set the fields in the structure */
	conn->file = file;
	conn->fileused = 0;
	conn->fileinfo.size = size;

	/*read the first chunk of the file*/
	if (conn->filebuffer) {
		headerbuffer=conn->filebuffer;
		headerbufferlength=conn->leftinbuffer = fread(conn->filebuffer, 1, configuration.readaheadbuffer*1024, conn->file);
		conn->positioninbuffer = 0;
	} else {
		headerbuffer=headerbuf;
		headerbufferlength=fread(headerbuffer, 1, TEMPBUFFERSIZE, conn->file);
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
		if (conn->contentlocation) {
			snprintf(temp, TEMPBUFFERSIZE, "Content-Location: %s\r\n", conn->contentlocation);
			webjames_writestringr(conn, temp);
		}
		if (conn->contentlanguage) {
			snprintf(temp, TEMPBUFFERSIZE, "Content-Language: %s\r\n", conn->contentlanguage);
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

