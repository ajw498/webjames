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
#include "cgiscript.h"
#include "openclose.h"
#include "report.h"
#include "stat.h"
#include "ip.h"
#include "write.h"
#include "handler.h"

#define remove_var(name) xos_set_var_val(name,NULL,-1,0,os_VARTYPE_STRING,NULL,NULL)
#define set_var_val(name,value) xos_set_var_val(name, (byte *)value, strlen(value), 0, 4, NULL, NULL)

void cgiscript_start(struct connection *conn)
/* start a CGI script with redirection */
{
	char tempfile[256], input[256], cmd[256], cmdformat[256], temp[HTTPBUFFERSIZE+1];
	int size, unix;
	FILE *file;
	wimp_t task;

	/* change currently selected directory if required */
	if (conn->flags.setcsd) {
		char dirname[256], *dot;

		strcpy(dirname,conn->filename);
		dot = strrchr(dirname,'.');
		if (dot != NULL) {
			*dot = '\0';
			xosfscontrol_dir(dirname);
		}
	}

	/* set up the system variables */

	if (configuration.server[0])  set_var_val("SERVER_SOFTWARE", configuration.server);
	set_var_val("SERVER_PORT", "80");
	set_var_val("SERVER_PROTOCOL", "HTTP/1.0");
	if (configuration.serverip[0])  set_var_val("SERVER_NAME", configuration.serverip);
	set_var_val("SERVER_ADMIN", configuration.webmaster);

	set_var_val("DOCUMENT_ROOT", configuration.site);

	set_var_val("GATEWAY_INTERFACE", "CGI/1.1");
	set_var_val("REQUEST_URI", conn->requesturi);

	set_var_val("SCRIPT_NAME", conn->uri);
	set_var_val("PATH_TRANSLATED", conn->filename);

	if (conn->args)
		set_var_val("QUERY_STRING", conn->args);
	else
		set_var_val("QUERY_STRING", "");

	sprintf(temp, "%d.%d.%d.%d", conn->ipaddr[0], conn->ipaddr[1],
					conn->ipaddr[2], conn->ipaddr[3]);
	set_var_val("REMOTE_ADDR", temp);
	if (conn->dnsstatus == DNS_OK)  set_var_val("REMOTE_HOST", conn->host);

	if (conn->method == METHOD_HEAD)
		strcpy(temp, "HEAD");
	else if (conn->method == METHOD_POST)
		strcpy(temp, "POST");
	else if (conn->method == METHOD_GET)
		strcpy(temp, "GET");
	else if (conn->method == METHOD_PUT)
		strcpy(temp, "PUT");
	else if (conn->method == METHOD_DELETE)
		strcpy(temp, "DELETE");
	set_var_val("REQUEST_METHOD", temp);

	if ((conn->method == METHOD_POST) || (conn->method == METHOD_PUT)) {
		sprintf(temp, "%d", conn->bodysize);
		set_var_val("CONTENT_LENGTH", temp);
		set_var_val("CONTENT_TYPE", conn->type);
	}

	sprintf(temp, "%d", conn->port);
	set_var_val("SERVER_PORT", temp);

	if ((conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE))
		set_var_val("ENTITY_PATH", conn->requesturi);

	if (conn->pwd) {
		set_var_val("AUTH_TYPE", "basic");
		set_var_val("REMOTE_USER", conn->authorization);
	} else {
		set_var_val("AUTH_TYPE", "");
		set_var_val("REMOTE_USER", "");
	}

	if (conn->cookie)
		set_var_val("HTTP_COOKIE", conn->cookie);
	else
		set_var_val("HTTP_COOKIE", "");

	if (conn->useragent)
		set_var_val("HTTP_USER_AGENT", conn->useragent);
	else
		set_var_val("HTTP_USER_AGENT", "");

	if (conn->referer)
		set_var_val("HTTP_REFERER", conn->referer);
	else
		set_var_val("HTTP_REFERER", "");


	input[0] = '\0';                      /* clear spool-input filename */

	/* get unique, temporary file */
	if (*configuration.cgi_out) {
		strcpy(tempfile, configuration.cgi_out);
	} else {
		tmpnam(tempfile);
	}

	strcpy(cmdformat,"*%s");
	unix = 0;
	if (conn->handler) {
		if (conn->handler->unix) {
			unix = 1;
		}
		if (conn->handler->command) {
			strcpy(cmdformat,conn->handler->command);
		}
	}

	if (conn->method == METHOD_POST) {
		if (*configuration.cgi_in) {
			strcpy(input, configuration.cgi_in);
		} else {
			tmpnam(input);
		}
		/* generate input filename */
		xosfile_save_stamped(input, 0xfff, (byte *)conn->body, (byte *)conn->body+conn->bodysize);
		/* build command with redirection */
		if (unix) strcat(cmdformat," < %s > %s"); else strcat(cmdformat," { < %s > %s }");
		sprintf(cmd, cmdformat, conn->filename, input, tempfile);
	} else {
		if (unix) strcat(cmdformat," > %s"); else strcat(cmdformat," { > %s }");
		sprintf(cmd, cmdformat, conn->filename, tempfile);
		input[0] = '\0';                    /* no file with input for the cgi-script */
	}

	/* execute the cgi-script */
	if (xwimp_start_task(cmd, &task)) {
		/* failed to start cgi-script */
		if (input[0])   remove(input);
		report_badrequest(conn, "script couldn't be started");
		return;
	}

#ifdef LOG
	writelog(LOGLEVEL_CGISTART, cmd);
#endif

	/* remove cgi-spool-input file (if any) */
	if (input[0])   remove(input);

	/* Remove all system variables that were set */
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

	/* Set the CSD back to what it was if we changed it */
	if (conn->flags.setcsd) xosfscontrol_back();

	/* disable reading */
	if (fd_is_set(select_read, conn->socket)) {
		fd_clear(select_read, conn->socket);
		readcount--;
	}

	/* already set up for writing (this is done in write.c) */

	/* read filesize */
	if (get_file_info(tempfile, NULL, NULL, &size, 0) < 0) {
		report_badrequest(conn, "error occured when reading file info");
		return;
	}

	/* open file for reading */
	file = fopen(tempfile, "rb");
	if (!file) {
		report_notfound(conn);
		return;
	}


	/* attempt to get a read-ahead buffer for the file */
	/* notice: things will still work if malloc fails */
	conn->filebuffer = malloc(configuration.readaheadbuffer*1024);
	conn->flags.releasefilebuffer = 1;    /* ignored if filebuffer == NULL */
	conn->leftinbuffer = 0;

	conn->flags.deletefile = 1;           /* delete the file when done */
	strcpy(conn->filename, tempfile);

	conn->flags.is_cgi = 0;               /* make close() output clf-info */

	/* set the fields in the structure, and that's it! */
	conn->file = file;
	conn->fileused = 0;
	conn->fileinfo.size = size;

	writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
}


