#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "swis.h"

#include "cgi-lib.h"


int main(int argc, char *argv[]) {

  int wjchat, found;
  int taskinfo[16];

  wjchat = found = 0;
  do {
    _swix(TaskManager_EnumerateTasks, _IN(0)|_IN(1)|_IN(2)|_OUT(0),
                                      found, taskinfo, 16, &found);
    if (strcmp((char *)taskinfo[1], "WJChat") == 0)  wjchat = taskinfo[0];
  } while ((found >= 0) && (!wjchat));

  if (wjchat) {
    char cmd[256], *ptr;
    int arg, msg[64];

    ptr = cmd;
    for (arg = 1; arg < argc; arg++) {
      strcpy(ptr, argv[arg]);
      ptr += strlen(ptr);
      *ptr++ = ' ';
    }

    msg[0] = 0;
    _swix(Wimp_Initialise, _IN(0)|_IN(1)|_IN(2)|_IN(3),
                            310, 0x4b534154, "WJChat request", msg);

    msg[0] = 256;
    msg[4] = 0x4f989;
    msg[5] = 0;
    msg[6] = 0X54434A57;
    strcpy((char *)(msg+7), cmd);
    _swix(Wimp_SendMessage, _IN(0)|_IN(1)|_IN(2), 17, msg, wjchat);

    _swix(Wimp_CloseDown, _IN(0)|_IN(1), 0, 0);
    return 0;
  }

  if (cgi_init(argc, argv))  return 0;

  cgi_respons(200, "OK");
  cgi_writeheader("Content-Type: text/html");
  cgi_headerdone();
  cgi_writestring("<html><head><title>WJChat</title></head>");
  cgi_writestring("<body bgcolor=\"#ffffff\"><h1>WJChat</h1>");
  cgi_writestring("<h3>WJChat is not running at the moment - ");
  cgi_writestring("please try again later</h3>");
  cgi_quit();

  return 0;
}
