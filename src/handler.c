/*
	$Id$
	Management of handlers
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "webjames.h"
#include "handler.h"
#include "report.h"
#include "stat.h"

#include "staticcontent.h"
#include "sendasis.h"
#include "cgiscript.h"
#include "webjamesscript.h"
#include "serverparsed.h"
#include "autoindex.h"

#ifdef PHP
#include "php_webjames.h"
#endif

static struct handlerentry handlers[] = {
#ifdef CGISCRIPT
	{
		"cgi-script",       /* name of handler */
		0,                  /* should the files be cached */
		NULL,               /* function to call once to initialise handler */
		cgiscript_start,    /* called to start a script */
		staticcontent_poll, /* called every so often to send a bit of the script output */
		NULL                /* function to call just before WebJames quits */
	},
#endif
#ifdef WEBJAMESSCRIPT
	{
		"webjames-script",
		0,
		NULL,
		webjamesscript_start,
		NULL,
		NULL
	},
#endif
#ifdef SENDASIS
	{
		"send-as-is",
		1,
		NULL,
		sendasis_start,
		staticcontent_poll,
		NULL
	},
#endif
#ifdef AUTOINDEX
	{
		"autoindex",
		0,
		NULL,
		autoindex_start,
		staticcontent_poll,
		NULL
	},
#endif
#ifdef CONTENT
	{
		"type-map",
		0,
		NULL,
		content_starthandler,
		NULL,
		NULL
	},
#endif
#ifdef SSI
	{
		"server-parsed",
		1,
		NULL,
		serverparsed_start,
		serverparsed_poll,
		NULL
	},
#endif
#ifdef PHP
	{
		"php-script",
		0,
		webjames_php_init,
		webjames_php_request,
		NULL,
		webjames_php_shutdown
	},
#endif
	{
		"static-content",
		1,
		NULL,
		staticcontent_start,
		staticcontent_poll,
		NULL
	},
	{
		NULL,  /* last entry should be all NULLs */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	}
};

static handler *handlerslist=NULL;

void handler_start(struct connection *conn)
{
	if (conn->handler->startfn != NULL) {
		conn->handler->startfn(conn);
	} else {
		if (conn->handler->pollfn == NULL) report_nocontent(conn);
	}

}

void handler_poll(struct connection *conn,int maxbytes)
{
	if (conn->handler && conn->handler->pollfn) conn->handler->pollfn(conn,maxbytes);
}

void add_handler(char *name, char *command)
{
	struct handler *newhandler;
	int ffound;
	char *percent;
	size_t len;

	newhandler = EM(malloc(sizeof(struct handler)));
	if (newhandler == NULL) return;

	len=strlen(name)+1;
	newhandler->name = EM(malloc(len));
	if (newhandler->name == NULL) return;
	memcpy(newhandler->name,name,len);
	len=strlen(command)+1;
	newhandler->command = EM(malloc(len));
	if (newhandler->command == NULL) return;
	memcpy(newhandler->command,command,len);
	newhandler->unix = 0;
	newhandler->cache = 0;
	percent = strchr(newhandler->command,'%');
	ffound = 0;
	while (percent) {
		switch (percent[1]) {
			case '%':
				/* %% is ok */
				break;
			case 'f':
				/* filename */
				percent[1] = 's';
				if (ffound) return; /* can't have two %f in command */
				ffound = 1;
				break;
			case 'u':
				/* unix style redirection should be used */
				/* %u should always be at the end of the line */
				percent[0] = '\0';
				percent[1] = '\0';
				newhandler->unix = 1;
				break;
			default:
				/* an illegal % sequence, so ignore the handler */
				return;
				break;
		}
		percent = strchr(percent+1,'%');
	}
	newhandler->startfn = cgiscript_start;
	newhandler->pollfn = staticcontent_poll;
	newhandler->next = handlerslist;
	handlerslist = newhandler;
}

struct handler *get_handler(char *name)
/* Get a pointer to the handler structure from a handler name */
{
	struct handler *test;

	test = handlerslist;
	while (test) {
		if (strcmp(test->name,name) == 0) return test;
		test = test->next;
	}
	return NULL;
}

int init_handlers(void)
{
	int i;

	for (i=0;handlers[i].name;i++) {
		struct handler *newhandler;
	
		newhandler = EM(malloc(sizeof(struct handler)));
		if (newhandler == NULL) return 0;
	
		newhandler->name = handlers[i].name;
		newhandler->command = NULL;
		newhandler->unix = 0;
		newhandler->cache = handlers[i].cache;
		newhandler->startfn = handlers[i].startfn;
		newhandler->pollfn = handlers[i].pollfn;
		newhandler->next = handlerslist;
		handlerslist = newhandler;
		if (handlers[i].initfn) {
			if (!handlers[i].initfn()) return 0;
		}
	}
	return 1; /*successfully started all handlers*/
}

void quit_handlers(void)
{
	int i;

	for (i=0;handlers[i].name;i++) {
		if (handlers[i].quitfn) handlers[i].quitfn();
	}
}
