/*
	!PHP
	Copyright © Alex Waugh 2000

	$Id: PHPFrontEnd.c,v 1.2 2002/02/19 22:45:35 ajw Exp $


    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdlib.h>
#include <stdio.h>

#define TOOLBOX_H
#define WIMP_H
#define WINDOW_H
#define MESSAGETRANS_H
#include "SysTypes.h"

#include "event.h"
#include "oslib/radiobutton.h"
/*
#include "tboxlibs/wimp.h"
#include "tboxlibs/toolbox.h"
#include "tboxlibs/event.h"
#include "tboxlibs/wimplib.h"
#include "tboxlibs/gadgets.h"
*/

#define event_SAVE 0x100
#define event_CANCEL 0x101
#define event_HELP 0x102

#define icon_STANDALONE 0
#define icon_MULTITASK  1
#define icon_ANT        2
#define icon_NAVAHO     3
#define icon_NETPLEX    4
#define icon_WEBJAMES   5

#define UNUSED(x) (x=x)

#define E(x) { \
	_kernel_oserror *err=x;\
	if (err!=NULL) {\
		xwimp_report_error(err,0,"PHP Config",NULL);\
		exit(EXIT_FAILURE);\
	}\
}

static ObjectId windowid;

static int Help(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
	UNUSED(event_code);
	UNUSED(event);
	UNUSED(id_block);
	UNUSED(handle);

	system("Filer_Run <PHP$Dir>.!Help");
	return 1;
}

static int AutoCreated(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
	UNUSED(event_code);
	UNUSED(event);
	UNUSED(handle);

	windowid=id_block->self_id;
	
	return 1;
}

#define ALIAS "Set Alias$@RunType_18A "
#define RUN   "Run <PHP$Dir>.RunType."

static int Save(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
	ComponentId selected;
	FILE *file;

	UNUSED(event_code);
	UNUSED(event);
	UNUSED(id_block);
	UNUSED(handle);

	E(xradiobutton_get_state(0,windowid,icon_STANDALONE,NULL,&selected));
	file=fopen("<PHP$Dir>.SetRunType","w");
	if (file!=NULL) {
		switch (selected) {
			case icon_STANDALONE:
				fprintf(file,ALIAS RUN "StandAlone %%%%*0\n");
				break;
			case icon_MULTITASK:
				fprintf(file,ALIAS "TaskWindow \"" RUN "StandAlone" " %%%%*0\" -name \"PHP script\" -quit -wimpslot 1800k");
				break;
			case icon_ANT:
				fprintf(file,ALIAS RUN "ANT %%%%*0\n");
				break;
			case icon_NAVAHO:
				fprintf(file,ALIAS RUN "Navaho %%%%*0\n");
				break;
			case icon_NETPLEX:
				fprintf(file,ALIAS RUN "Netplex %%%%*0\n");
				break;
			case icon_WEBJAMES:
				fprintf(file,ALIAS "WimpSlot -Min 1800k -Max 1800k |M Run <PHP$Dir>.php %%%%*0\n");
				break;
		}
	}
	fclose(file);

	system("Obey <PHP$Dir>.SetRunType");

	exit(EXIT_SUCCESS);
	return 1;
}

static int Quit(int event_code, ToolboxEvent *event, IdBlock *id_block,void *handle)
{
	UNUSED(event_code);
	UNUSED(event);
	UNUSED(id_block);
	UNUSED(handle);

	exit(EXIT_SUCCESS);
	return 1;
}

static int Message_Quit(WimpMessage *message,void *handle)
{
	UNUSED(message);
	UNUSED(handle);

	exit(EXIT_SUCCESS);
	return 1;
}


int main(void)
{
	int toolbox_events[] = {0}, wimp_messages[] = {0}, event_code;
	WimpPollBlock poll_block;
	MessagesFD messages;
	IdBlock id_block;

	if (xtoolbox_initialise(0, 310, (wimp_message_list*)wimp_messages, (toolbox_action_list*)toolbox_events, "<PHP$Dir>", &messages, &id_block, 0, 0, 0)!=NULL) exit(EXIT_FAILURE);
	event_initialise(&id_block);
	event_set_mask(1+256);

	event_register_toolbox_handler(event_ANY,event_CANCEL,(event_toolbox_handler*)Quit,NULL);
	event_register_toolbox_handler(event_ANY,event_SAVE,(event_toolbox_handler*)Save,NULL);
	event_register_toolbox_handler(event_ANY,event_HELP,(event_toolbox_handler*)Help,NULL);
	event_register_toolbox_handler(event_ANY,action_OBJECT_AUTO_CREATED/*Toolbox_ObjectAutoCreated*/,(event_toolbox_handler*)AutoCreated,NULL);
	event_register_message_handler(message_QUIT,Message_Quit,NULL);

	while (TRUE) event_poll(&event_code, &poll_block, 0);

}
