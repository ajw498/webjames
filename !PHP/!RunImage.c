/*
	!PHP
	Copyright © Alex Waugh 2000

	Version 1.01 19/2/00


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

#include "Desk.Window.h"
#include "Desk.Error.h"
#include "Desk.Event.h"
#include "Desk.EventMsg.h"
#include "Desk.Handler.h"
#include "Desk.Icon.h"
#include "Desk.Str.h"
#include "Desk.Resource.h"
#include "Desk.Screen.h"
#include "Desk.Template.h"

#include "AJWLib.File.h"
#include "AJWLib.Icon.h"
#include "AJWLib.Error2.h"

#include <stdio.h>
#include <stdlib.h>


#define icon_STANDALONE 0
#define icon_CGI 1
#define icon_HELP 3
#define icon_CANCEL 5
#define icon_SAVE 4
#define icon_OK 2
#define icon_TASKWINDOW 6

Desk_window_handle window;

Desk_bool Cancel(Desk_event_pollblock *block, void *r)
{
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	Desk_Event_CloseDown();
	return Desk_TRUE;
}

Desk_bool Help(Desk_event_pollblock *block, void *r)
{
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	system("Filer_Run <PHP$Dir>.!Help");
	return Desk_TRUE;
}

Desk_bool OK(Desk_event_pollblock *block, void *r)
{
	char str[256];
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	sprintf(str,"Set PHP$CGI \"%s\"\n",Desk_Icon_GetSelect(window,icon_CGI) ? "Yes" : "No");
	system(str);
	sprintf(str,"Set PHP$TaskWindow \"%s\"\n",Desk_Icon_GetSelect(window,icon_TASKWINDOW) ? "Yes" : "No");
	system(str);
	if (block->data.mouse.button.data.select) Desk_Event_CloseDown();
	return Desk_TRUE;
}

Desk_bool ShadeTaskWindow(Desk_event_pollblock *block, void *r)
{
	Desk_Icon_Shade(window,icon_TASKWINDOW);
	return Desk_TRUE;
}

Desk_bool UnShadeTaskWindow(Desk_event_pollblock *block, void *r)
{
	Desk_Icon_Unshade(window,icon_TASKWINDOW);
	return Desk_TRUE;
}

Desk_bool Save(Desk_event_pollblock *block, void *r)
{
	FILE *config;
	char str[256];
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	config=AJWLib_File_fopen("<PHP$Dir>.Config","w");
	sprintf(str,"Set PHP$CGI \"%s\"\n",Desk_Icon_GetSelect(window,icon_CGI) ? "Yes" : "No");
	system(str);
	fprintf(config,str);
	sprintf(str,"Set PHP$TaskWindow \"%s\"\n",Desk_Icon_GetSelect(window,icon_TASKWINDOW) ? "Yes" : "No");
	system(str);
	fprintf(config,str);
	fclose(config);
	if (block->data.mouse.button.data.select) Desk_Event_CloseDown();
	return Desk_TRUE;
}

int main(void)
{
	Desk_Error2_HandleAllSignals();
	Desk_Error2_SetHandler(AJWLib_Error2_ReportFatal);
	Desk_Resource_Initialise("PHP");
	Desk_Event_Initialise("PHP Config");
	Desk_EventMsg_Initialise();
	Desk_Screen_CacheModeInfo();
	Desk_EventMsg_Claim(Desk_message_MODECHANGE,Desk_event_ANY,Desk_Handler_ModeChange,NULL);
	Desk_Template_Initialise();
	Desk_Template_LoadFile("Templates");
	Desk_Event_Claim(Desk_event_OPEN,Desk_event_ANY,Desk_event_ANY,Desk_Handler_OpenWindow,NULL);
	window=Desk_Window_Create("Main",Desk_template_TITLEMIN);
	Desk_Window_Show(window,Desk_open_CENTERED);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_CANCEL,Cancel,NULL);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_SAVE,Save,NULL);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_HELP,Help,NULL);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_OK,OK,NULL);
	AJWLib_Icon_RegisterCheckAdjust(window,icon_STANDALONE);
	AJWLib_Icon_RegisterCheckAdjust(window,icon_CGI);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_CGI,ShadeTaskWindow,NULL);
	Desk_Event_Claim(Desk_event_CLICK,window,icon_STANDALONE,UnShadeTaskWindow,NULL);
	Desk_Icon_SetRadios(window,icon_STANDALONE,icon_CGI,Desk_stricmp(getenv("PHP$CGI"),"Yes") ? icon_STANDALONE : icon_CGI);
	Desk_Icon_SetSelect(window,icon_TASKWINDOW,Desk_stricmp(getenv("PHP$TaskWindow"),"Yes"));
	if (Desk_Icon_GetSelect(window,icon_CGI)) ShadeTaskWindow(NULL,NULL);
	while (Desk_TRUE) Desk_Event_Poll();
	return 0;
}

