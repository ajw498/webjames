#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "handler.h"
#include "staticcontent.h"
#include "cgiscript.h"
#include "webjamesscript.h"


static handler *handlerslist=NULL;

void handler_start(struct connection *conn)
{
	if (conn->handler != NULL) if (conn->handler->startfn != NULL) conn->handler->startfn(conn);
}

int handler_poll(struct connection *conn,int maxbytes)
{
	if (conn->handler != NULL) if (conn->handler->pollfn != NULL) return conn->handler->pollfn(conn,maxbytes);
	return 0;
}

void add_handler(char *name, char *command)
{
	struct handler *new;

	new = malloc(sizeof(struct handler));
	if (new == NULL) return;

	new->name = malloc(strlen(name)+1);
	if (new->name == NULL) return;
	new->command = malloc(strlen(command)+1);
	if (new->command == NULL) return;
	new->startfn = NULL;
	new->pollfn = NULL;
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

static struct handlerentry handlers[] = {
	{
		"cgi-script",
		NULL,
		cgiscript_start,
		staticcontent_poll,
		NULL
	},
	{
		"webjames-script",
		NULL,
		webjamesscript_start,
		NULL,
		NULL
	},
	{
		"static-content",
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
		NULL
	}
};

void init_handlers(void)
{
	int i=0;

	while (handlers[i].name != NULL) {
		struct handler *new;
	
		new = malloc(sizeof(struct handler));
		if (new == NULL) return;
	
		new->name = handlers[i].name;
		new->command = NULL;
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
