/*
	$Id: main.c,v 1.3 2002/10/20 21:29:54 ajw Exp $
	main() function, wimp polling loop
*/

#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(__GNUC__) && !defined(__TARGET_SCL__)
#include <unixlib/features.h>
#endif
 
#include "kernel.h"
#include "swis.h"

#include "oslib/os.h"
#include "oslib/wimp.h"

#include "webjames.h"
#include "main.h"
#include "stat.h"
#include "ip.h"

#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#define SocketWatch_Register 0x52280
#define SocketWatch_Deregister 0x52281
#define SocketWatch_AtomicReset 0x52282
#define SocketWatch_AllocPW 0x52283
#define SocketWatch_DeallocPW 0x52284
#define MESSAGE_WEBJAMES_CMD 0x4f988

#ifdef PHP
const char *__dynamic_da_name="WebJames and PHP Heap";
#endif

/* local variables */
static int wimp[64];
static int quit;
static os_t polldelay;
static int *pollword=NULL;

void init_task() {

	int wimpmsg[4];

	quit = 0;
	polldelay = 5;

	/* select which messages to receive */
	wimpmsg[0] = MESSAGE_WEBJAMES_CMD;
	wimpmsg[1] = 0;

	/* initialise the wimp */
	if (E(xwimp_initialise(310, "WebJames", (wimp_message_list *)wimpmsg, NULL, NULL))) quit = 1;
}



void poll(void)
{
	os_t clk;
	int flags;
	wimp_event_no action;
	wimp_poll_flags pollmask;
	
	EV(xos_read_monotonic_time(&clk));
	if (pollword) {
		pollmask=wimp_GIVEN_POLLWORD;
	} else {
		pollmask=0;
	}
	EV(xwimp_poll_idle(pollmask, (wimp_block *)&wimp, clk+polldelay, pollword, &action));
	switch (action) {
	case wimp_POLLWORD_NON_ZERO:
		*pollword=0;
		/*fall through*/
	case wimp_NULL_REASON_CODE:
		polldelay = webjames_poll(pollword ? 100 : 10);
		if (polldelay < 0) quit = 1;
		break;
	case wimp_USER_MESSAGE_ACKNOWLEDGE:
		break;
	case wimp_USER_MESSAGE:
	case wimp_USER_MESSAGE_RECORDED:
		switch (wimp[4]) {
		case message_QUIT:
			quit = 1;
			break;
		case MESSAGE_WEBJAMES_CMD:
			flags = wimp[5];
			if (flags & 0xfffffff9)  break;   /* reply or reserved bit set */
			if (flags &2) {                   /* 'long' message */
				char *ptr;
				ptr = (char *)wimp[6];
#ifdef MemCheck_MEMCHECK
				MemCheck_RegisterMiscBlock((void *)wimp[6],256); /* The size of the block may not actually be 256 bytes, but we have no way of telling */
#endif
				while (*ptr >= ' ')  ptr++;
				*ptr = '\0';
				webjames_command((char *)wimp[6], flags &4);
#ifdef MemCheck_MEMCHECK
				MemCheck_UnRegisterMiscBlock((void *)wimp[6]);
#endif
			} else {                          /* normal message */
				char cmd[200], *ptr1, *ptr2;
				ptr1 = (char *)&wimp[6];
				ptr2 = cmd;
				while (*ptr1 >= ' ' && ptr2<cmd+199)  *ptr2++ = *ptr1++;
				*ptr2 = '\0';
				webjames_command(cmd, 0);
			}
			break;                            /* MESSAGE_WEBJAMES_CMD */
		}
		break;
	}
}



void closedown(void)
/* quit */
{
	xwimp_close_down(0);
}



int main(int argc, char *argv[])
{
	char *configfile;
	int i;
#ifdef MemCheck_MEMCHECK
	MemCheck_Init();
	MemCheck_RegisterArgs(argc,argv);
	MemCheck_InterceptSCLStringFunctions();
	MemCheck_SetStoreMallocFunctions(1);
	MemCheck_SetAutoOutputBlocksInfo(0);
	MemCheck_SetWriteQuitting(0);
#endif

#if defined(__GNUC__) && !defined(__TARGET_SCL__)
	__feature_imagefs_is_file = 1;
#endif

	/* first argument should be the configuration file */
	if (argc >= 2) configfile=argv[1]; else configfile="<WebJames$Dir>.config";

	init_task();
	if (!quit)
		if (!webjames_init(configfile))  quit = 1;

	if (!quit) {
		if (_swix(SocketWatch_AllocPW,_OUT(0),&pollword)) {
			/* if this failed, then socketwatch is probably not loaded */
			pollword=NULL;
		} else {
#ifdef MemCheck_MEMCHECK
			MemCheck_RegisterMiscBlock(pollword,4);
#endif
			for (i = 0; i < serverinfo.serverscount; i++) {
				if (serverinfo.servers[i].socket != socket_CLOSED) EV((os_error*)_swix(SocketWatch_Register,_INR(0,2),pollword,1,serverinfo.servers[i].socket));
			}
		}
	
		while (!quit)   poll();
	
		if (pollword) {
			for (i = 0; i < serverinfo.serverscount; i++) {
				if (serverinfo.servers[i].socket != socket_CLOSED) EV((os_error*)_swix(SocketWatch_Deregister,_INR(0,1),serverinfo.servers[i].socket,pollword));
			}
#ifdef MemCheck_MEMCHECK
			MemCheck_UnRegisterMiscBlock(pollword);
#endif
			EV((os_error*)_swix(SocketWatch_DeallocPW,_IN(0),pollword));
		}
	}

	webjames_kill();
	closedown();
	return EXIT_SUCCESS;
}
