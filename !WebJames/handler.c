#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "webjames.h"
#include "handler.h"
#include "report.h"

#include "staticcontent.h"
#include "sendasis.h"
#include "cgiscript.h"
#include "webjamesscript.h"

static struct handlerentry handlers[] = {
	{
		"cgi-script",       /* name of handler */
		0,                  /* should the files be cached */
		NULL,               /* function to call once to initialise handler */
		cgiscript_start,    /* called to start a script */
		staticcontent_poll, /* called every so often to send a bit of the script output */
		NULL                /* function to call just before WebJames quits */
	},
	{
		"webjames-script",
		0,
		NULL,
		webjamesscript_start,
		NULL,
		NULL
	},
	{
		"send-as-is",
		1,
		NULL,
		sendasis_start,
		staticcontent_poll,
		NULL
	},
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

int handler_poll(struct connection *conn,int maxbytes)
{
	if (conn->handler != NULL) if (conn->handler->pollfn != NULL) return conn->handler->pollfn(conn,maxbytes);
	return 0;
}

void add_handler(char *name, char *command)
{
	struct handler *new;
	int ffound;
	char *percent;

	new = malloc(sizeof(struct handler));
	if (new == NULL) return;

	new->name = malloc(strlen(name)+1);
	if (new->name == NULL) return;
	strcpy(new->name,name);
	new->command = malloc(strlen(command)+1);
	if (new->command == NULL) return;
	strcpy(new->command,command);
	new->unix = 0;
	new->cache = 0;
	percent = strchr(new->command,'%');
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
				new->unix = 1;
				break;
			default:
				/* an illegal % sequence, so ignore the handler */
				return;
				break;
		}
		percent = strchr(percent+1,'%');
	}
	new->startfn = cgiscript_start;
	new->pollfn = staticcontent_poll;
	new->next = handlerslist;
	handlerslist = new;
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

void init_handlers(void)
{
	int i=0;

	while (handlers[i].name != NULL) {
		struct handler *new;
	
		new = malloc(sizeof(struct handler));
		if (new == NULL) return;
	
		new->name = handlers[i].name;
		new->command = NULL;
		new->unix = 0;
		new->cache = handlers[i].cache;
		new->startfn = handlers[i].startfn;
		new->pollfn = handlers[i].pollfn;
		new->next = handlerslist;
		handlerslist = new;
		if (handlers[i].initfn) handlers[i].initfn();
		i++;
	}
}

void quit_handlers(void)
{
	int i=0;

	while (handlers[i].name != NULL) {
		if (handlers[i].quitfn) handlers[i].quitfn();
		i++;
	}
}
