/*
	!PHP
	© Alex Waugh 2000

	Version 1.00 30/1/00

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

Desk_window_handle window;

char *GetStr(void)
{
	static char str[256];
	sprintf(str,"Set PHP$CGI \"%s\"\n",Desk_Icon_GetSelect(window,icon_CGI) ? "Yes" : "No");
	return str;
}

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
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	system(GetStr());
	if (block->data.mouse.button.data.select) Desk_Event_CloseDown();
	return Desk_TRUE;
}

Desk_bool Save(Desk_event_pollblock *block, void *r)
{
	FILE *config;
	if (block->data.mouse.button.data.menu) return Desk_FALSE;
	config=AJWLib_File_fopen("<PHP$Dir>.Config","w");
	fprintf(config,GetStr());
	fclose(config);
	system(GetStr());
	if (block->data.mouse.button.data.select) Desk_Event_CloseDown();
	return Desk_TRUE;
}

int main(void)
{
	Desk_Error2_HandleAllSignals();
	Desk_Error2_SetHandler(AJWLib_Error2_ReportFatal);
	Desk_Resource_Initialise("PHP");
	Desk_Event_Initialise("PHP");
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
	Desk_Icon_SetRadios(window,icon_STANDALONE,icon_CGI,Desk_stricmp(getenv("PHP$CGI"),"Yes") ? icon_STANDALONE : icon_CGI);
	while (Desk_TRUE) Desk_Event_Poll();
	return 0;
}

