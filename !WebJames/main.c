#include "MemCheck:MemCheck.h"

#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "oslib/os.h"
#include "oslib/wimp.h"

#include "main.h"
#include "webjames.h"
#include "global.h"

/* global variables */
int quit;

/* local variables */
static int wimp[64];
static int polldelay;



void init_task() {

	int wimpmsg[4], dummy;
	wimp_t task;

	quit = 0;
	polldelay = 5;

	/* select which messages to receive */
	wimpmsg[0] = 0x4f988;
	wimpmsg[1] = 0;

	/* initialise the wimp */
	if (xwimp_initialise(310, "WebJames", (wimp_message_list *)wimpmsg, (wimp_version_no *)&dummy, &task))   quit = 1;
}



void poll() {

	int clk, flags, action;

	xos_read_monotonic_time(&clk);

	xwimp_poll_idle(0, (wimp_block *)wimp, (os_t)clk+polldelay, NULL, (wimp_event_no *)&action);
	switch (action) {
	case 0:
		polldelay = webjames_poll();
		if (polldelay < 0)  quit = 1;
		break;
	case 17:
	case 18:
	case 19:
		switch (wimp[4]) {
		case 0:                             /* WIMP_MESSAGE_QUIT */
			quit = 1;
			break;                            /* WIMP_MESSAGE_QUIT */
		case 0x4f988:                       /* MESSAGE_WEBJAMES_CMD */
			flags = wimp[5];
			if (flags & 0xfffffff9)  break;   /* reply or reserved bit set */
			if (flags &2) {                   /* 'long' message */
				char *ptr;
				ptr = (char *)wimp[6];
				MemCheck_RegisterMiscBlock((void *)wimp[6],256); /* The size of the block may not actually be 256 bytes, but we have no way of telling */
				while (*ptr >= ' ')  ptr++;
				*ptr = '\0';
				webjames_command((char *)wimp[6], flags &4);
				MemCheck_UnRegisterMiscBlock((void *)wimp[6]);
			} else {                          /* normal message */
				char cmd[200], *ptr1, *ptr2;
				ptr1 = (char *)&wimp[6];
				ptr2 = cmd;
				while (*ptr1 >= ' ')  *ptr2++ = *ptr1++;
				*ptr2 = '\0';
				webjames_command(cmd, 0);
			}
			break;                            /* MESSAGE_WEBJAMES_CMD */
		}
		break;
	}
}



void closedown() {
	/* quit */
	xwimp_close_down(0);
}



int main(int argc, char *argv[]) {
/* first argument should be the configuration file */

	MemCheck_Init();
	MemCheck_RegisterArgs(argc,argv);
	MemCheck_InterceptSCLStringFunctions();
	MemCheck_SetStoreMallocFunctions(1);
	MemCheck_SetAutoOutputBlocksInfo(0);
	MemCheck_SetWriteQuitting(0);

	if (argc != 2)  return 0;

	init_task();
	if (!quit)
		if (!webjames_init(argv[1]))  quit = 1;
	while (!quit)   poll();
	webjames_kill();
	closedown();
}
