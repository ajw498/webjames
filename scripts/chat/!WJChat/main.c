#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "swis.h"

#define MAXCHANNELS 8
#define MAXUSERS    20
#define NICKLEN     8
#define CHANNELLEN  16
#define HISTORYLEN  32
#define MAXMSGS     400
#define TIMEOUT     (5*60*100)


#define RESPONS_OK   "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
#define HEADREFRESH  "<html><head><title>WJChat</title><meta http-equiv=\"refresh\" content=\"6,/cgi-bin/chat?"
#define HEADNORMAL   "<html><head><title>WJChat</title></head>"
#define HEADEND      "</head>"
#define TOPFRAME     "<frame src=\"/cgi-bin/chat?p=history&id=%d\" name=\"history\">"
#define BOTTOMFRAME  "<frame src=\"/cgi-bin/chat?p=enter&id=%d\" name=\"enter\">"
#define PAGE_LOGIN   "<input type=\"hidden\" name=\"p\" value=\"login\">"
#define FORM_INPUT   "<form name=\"input\" method=\"GET\" action=\"/cgi-bin/chat\">"
#define FORM_LOGIN   "<form name=\"login\" method=\"GET\" action=\"/cgi-bin/chat\">"
#define SUBMIT_LOGIN "<input type=\"submit\" name=\"\" value=\"Login...\">"

// #define FORM_LOGOFF  "<form name=\"logoff\" method=\"GET\" action=\"/cgi-bin/chat\"><input type=\"hidden\" name=\"p\" value=\"logoff\"><input type=\"hidden\" name=\"id\" value=\"%d\"><input type=\"submit\" name=\"\" value=\"Logoff\"></form> &nbsp; &nbsp; "

#define INPUT_NICK   "<p>Nickname &nbsp;<input name=\"nick\" size=\"15\" maxlength=\"8\" value=\"\">"
#define INPUT_CHANNEL "<p>Channel &nbsp;<input name=\"chn\" size=\"15\" maxlength=\"12\" value=\"\">"
#define INPUT_MSG    "<p><input name=\"msg\" size=\"50\" maxlength=\"100\" value=\"\">"
#define INPUT_SEND   "<input type=\"submit\" name=\"\" value=\"Send message\">"
#define INPUT_ID     "<input type=\"hidden\" name=\"id\" value=\"%d\">"
#define INPUT_SENDTO "<input type=\"hidden\" name=\"p\" value=\"input\">"

#define VALUE_ALL    " value=\"*\">everybody on channel "
#define VALUE_NICK   " value=\"%s\">%s"
#define OPTION       "<option "
#define OPTION_S     "<option selected "
#define END_OPT      "</option>"

#define BODYTAG      "<body bgcolor=\"#ffffff\">"

#define PUBLICMSG    "<font color=\"#ff0000\">&lt<i>%s</i>&gt </font>%s<br>"
#define PRIVATEMSG   "<font color=\"#0000ff\">&lt%s&gt </font><i>%s</i><br>"

#define DOCEND       "</body></html>"

#define TOOMANYUSERS "The maximum allowed number of users has been reached - please try again later"
#define TOOMANYCHNS  "You cannot create a new channel - the maximum number of channels has been reached"
#define NICKUSED     "The nickname you've selected is already used"

#define PANIC        "<html><head><title>WJChat</title></head><body bgcolor\"#ffffff\">PANIC!!</body></html>"



#define MESSAGE_WEBJAMES_CMD  0x4f988

#define CGI_HEADER_UNKNOWN    0
#define CGI_HEADER_RMA        1
#define CGI_HEADER_FILE       2
#define CGI_HEADER_DYNAMIC    3

#define CGI_METHOD_GET        0
#define CGI_METHOD_POST       1
#define CGI_METHOD_HEAD       2

#define Socket_Write            0x41214
#define Socket_Close            0x41210


int *cgi_writetosocket(char *buffer, int bytes, int *written);
int cgi_init(int argc, char **argv);
void cgi_quit(void);
int cgi_extractparameter(char *in, char **name, int *nlen, char **val, int *vlen);
int cgi_decodeescaped(char *in, char *out, int max);
void cgi_webjamescommand(char *ptr, int release);
void cgi_writestring(char *string);

void chat_start(void);
void chat_url_received(int size, int flags, char *data);
void chat_input(char *id, char *to, char *msg);
void chat_history(char *id);
void chat_login(char *nick, char *chn);
void user_logoff(int user);
int channel_create(char *name);
void channel_remove(int chn);
void join_channel(int user, int chn);
void channel_addmsg(int chn, int msg);
void chat_enter(int id);
void user_addmsg(int user, int msg);
void user_removemsg(int msg);
// void chat_logoff(int id);



int wimp[64];


char _cgi_temp[2048], _cgi_requestline[4100], _cgi_headerfile[256];
char _cgi_host[128], _cgi_username[64];
char *_cgi_parameters, *_cgi_headeraddress;
int _cgi_headerstorage;
int _cgi_headermeminfo, _cgi_releaseheader, _cgi_requestsize;
int _cgi_socket, _cgi_httpversion, _cgi_requesttype;
int _cgi_maxbytespersecond, _cgi_headerdone, _cgi_byteswritten;
int _cgi_polldelay, _cgi_starttime, _cgi_responscode;
int _cgi_requestlinelength, _cgi_parameterssize, _cgi_parametercount;


typedef struct message {
  char *text;
  int users;
} message;

typedef struct user {
  char nickname[NICKLEN+1];
  char upper[NICKLEN+1];
  int channel;
  int logintime, lastactive;
  int used;
  int sendto;
  int history[HISTORYLEN];
  int historylen;
} user;

typedef struct channel {
  char name[CHANNELLEN+1];
  int used;
  int users[MAXUSERS];
} channel;

char output[10000];
struct user users[MAXUSERS];
struct channel channels[MAXCHANNELS];
int usercount, channelcount;
struct message messages[MAXMSGS];




int main(int argc, char *argv[]) {

  int msg[2], quit, action, i;

  quit = 0;

  for (i = 0; i < MAXMSGS; i++) {
    messages[i].text = NULL;
    messages[i].users = 0;
  }
  for (i = 0; i < MAXUSERS; i++) {
    users[i].nickname[0] = '\0';
    users[i].used = 0;
  }
  for (i = 0; i < MAXCHANNELS; i++) {
    channels[i].name[0] = '\0';
    channels[i].used = 0;
  }
  usercount = channelcount = 0;

  msg[0] = 0x4f989;
  msg[1] = 0;
  if (_swix(Wimp_Initialise, _IN(0)|_IN(1)|_IN(2)|_IN(3),
                            310, 0x4b534154, "WJChat", msg))  quit = 1;

  while (!quit) {
    int clk;
    _swix(OS_ReadMonotonicTime, _OUT(0), &clk);
    _swix(Wimp_PollIdle, _IN(0)|_IN(1)|_IN(2)|_OUT(0),
                         0, wimp, clk+100, &action);
    if (action == 0) {
      int usr;
      for (usr = 0; usr < MAXUSERS; usr++)
        if ((users[usr].used) && (users[usr].lastactive+TIMEOUT < clock()))
          user_logoff(usr);
    } else if ((action == 17) || (action == 18)) {
      if (wimp[4] == 0)
        quit = 1;
      else if ((wimp[4] == 0x4f989) && (wimp[6] == 0X54434A57))
        chat_url_received(wimp[0]-28, wimp[5], (char *)(wimp+7));
    }
  }

  _swix(Wimp_CloseDown, _IN(0)|_IN(1), 0, 0);
}



int cgi_init(int argc, char **argv) {

  int arg, i;
  char *ptr, *end;

  _cgi_headerstorage = CGI_HEADER_UNKNOWN;
  _cgi_headeraddress = NULL;
  _cgi_headermeminfo = 0;
  _cgi_headerfile[0] = '\0';
  _cgi_releaseheader = 0;
  _cgi_requestsize = -1;
  _cgi_socket = -1;
  _cgi_httpversion = 0;
  _cgi_requesttype = CGI_METHOD_GET;
  _cgi_maxbytespersecond = 0;
  _cgi_host[0] = '\0';
  _cgi_username[0] = '\0';
  _cgi_headerdone = 0;
  _cgi_byteswritten = 0;
  _cgi_polldelay = 0;
  _cgi_starttime = clock();
  _cgi_responscode = -1;
  _cgi_requestlinelength = -1;
  _cgi_parameters = _cgi_temp;
  _cgi_parameterssize = 0;
  _cgi_parametercount = 0;


  arg = 0;
  while (arg < argc) {
    if (strcmp(argv[arg], "-socket") == 0) {
      if (arg+1 < argc)  _cgi_socket = atoi(argv[++arg]);
    } else if (strcmp(argv[arg], "-http") == 0) {
      if (arg+1 < argc)  _cgi_httpversion = atoi(argv[++arg]);
    } else if (strcmp(argv[arg], "-head") == 0)
      _cgi_requesttype = CGI_METHOD_HEAD;
    else if (strcmp(argv[arg], "-post") == 0)
      _cgi_requesttype = CGI_METHOD_POST;
    else if (strcmp(argv[arg], "-remove") == 0) {
      _cgi_releaseheader = 1;
    } else if (strcmp(argv[arg], "-size") == 0) {
      if (arg+1 < argc)  _cgi_requestsize = atoi(argv[++arg]);
    } else if (strcmp(argv[arg], "-file") == 0) {
      if (arg+1 < argc)  strncpy(_cgi_headerfile, argv[++arg], 255);
      _cgi_headerstorage = CGI_HEADER_FILE;
    } else if (strcmp(argv[arg], "-rma") == 0) {
      if (arg+1 < argc)  _cgi_headeraddress = (char *)atoi(argv[++arg]);
      _cgi_headerstorage = CGI_HEADER_RMA;
    } else if (strcmp(argv[arg], "-dynamic") == 0) {
      if (arg+1 < argc)  _cgi_headermeminfo = atoi(argv[++arg]);
      _cgi_headerstorage = CGI_HEADER_DYNAMIC;
    } else if (strcmp(argv[arg], "-bps") == 0) {
      if (arg+1 < argc) {
        _cgi_maxbytespersecond = atoi(argv[++arg]);
        if (_cgi_maxbytespersecond < 100)
          _cgi_maxbytespersecond = 100;
        if (_cgi_maxbytespersecond < 1000000)
          _cgi_maxbytespersecond = 1000000;
      }
    } else if (strcmp(argv[arg], "-host") == 0) {
      if (arg+1 < argc)  strncpy(_cgi_host, argv[++arg], 127);
    } else if (strcmp(argv[arg], "-user") == 0) {
      if (arg+1 < argc)  strncpy(_cgi_username, argv[++arg], 63);
    }

    arg++;
  }

  // check if the arguments are OK
  if ((_cgi_httpversion < 9) || (_cgi_headerstorage == CGI_HEADER_UNKNOWN))
    return 1;
  if ((_cgi_requestsize < 0) || (_cgi_socket < 0))
    return 1;

  if (_cgi_headerstorage == CGI_HEADER_FILE) {
    _swix(OS_File, _IN(0)|_IN(5)|_OUT(4),
                   5, _cgi_headerfile, &_cgi_requestsize);
    _swix(OS_Module, _IN(0)|_IN(3)|_OUT(2),
                    6, _cgi_requestsize, &_cgi_headeraddress);
    _swix(OS_File, _IN(0)|_IN(1)|_IN(2),
                   255, _cgi_headerfile, _cgi_headeraddress);
    if (_cgi_releaseheader)  remove(_cgi_headerfile);
    _cgi_headerstorage = CGI_HEADER_RMA;
    _cgi_releaseheader = 1;
  } else if (_cgi_headerstorage == CGI_HEADER_RMA) {
  } else if (_cgi_headerstorage == CGI_HEADER_DYNAMIC) {
    _swix(OS_DynamicArea, _IN(0)|_IN(1)|_OUT(3),
                          2, _cgi_headermeminfo, &_cgi_headeraddress);
  }

  // preprocess the arguments (if any) to the cgi-script
  ptr = _cgi_headeraddress;             // points to 'GET /<uri>[?...&...]'
  end = ptr + _cgi_requestsize;
  i = 0;
  while ((ptr[i] >= ' ') && (i < 4096)) {
    _cgi_requestline[i] = ptr[i];
    i++;
  }

  _cgi_requestline[i] = '\0';
  _cgi_requestlinelength = i;
  // skip method and go to /
  while ((*ptr >= ' ') && (ptr < end) && (*ptr != '/'))  ptr++;
  while ((*ptr >= ' ') && (ptr < end) && (*ptr != '?'))  ptr++;
  if ((*ptr < ' ') || (ptr == end) || (ptr[1] < ' '))  return 0;

  _cgi_parameters = ++ptr;    // skip the '?'
  _cgi_parametercount = 1;
  while ((*ptr > ' ') && (ptr < end)) {
    if (*ptr == '&')  _cgi_parametercount++;
    _cgi_parameterssize++;
    ptr++;
  }

  return 0;
}



int *cgi_writetosocket(char *buffer, int bytes, int *written) {
// writes bytes to the socket
// returns error or NULL
//
// buffer           pointer to buffer
// bytes            bytes towrite
//
// on exit
// written          number of bytes written

  int *error, written2;

  error = (int *)_swix(Socket_Write, _IN(0)|_IN(1)|_IN(2)|_IN(3)|_OUT(0),
                               _cgi_socket, buffer, bytes, 0, &written2);

  if (error) {
    if (error[0] != 36)  return error;
  } else
    *written = written2;

  return NULL;
}



void cgi_quit() {

  if (_cgi_releaseheader) {
    if (_cgi_headerstorage == CGI_HEADER_RMA) {
      _swix(OS_Module, _IN(0)|_IN(2), 7, _cgi_headeraddress);
    } else if (_cgi_headerstorage == CGI_HEADER_DYNAMIC) {
      _swix(OS_DynamicArea, _IN(0)|_IN(1), 1, _cgi_headermeminfo);
    } else if (_cgi_headerstorage == CGI_HEADER_FILE) {
      remove(_cgi_headerfile);
    }
    _cgi_headerstorage = CGI_HEADER_UNKNOWN;
  }
  if (_cgi_socket >= 0) {
    _swix(Socket_Close, _IN(0), _cgi_socket);
    _cgi_socket = -1;
  }
  if ((_cgi_requestlinelength > 0) && (_cgi_responscode)) {
    char *rma;

    if (!_swix(OS_Module, _IN(0)|_IN(1)|_OUT(2), 6,
                   100 + strlen(_cgi_host) + _cgi_requestlinelength, &rma)) {
      sprintf(rma, "clf %d %d %s %% %s", _cgi_responscode, _cgi_byteswritten,
                    _cgi_host, _cgi_requestline);
      cgi_webjamescommand(rma, 1);
    }
  }
}


int cgi_decodeescaped(char *in, char *out, int max) {
// decode an escape-sequence
//
// in               escape-sequence
// out              output buffer (may be == in)
// max              size of 'in'

  int read, write, hex0, hex1, chr;

  read = write = 0;
  do {
    chr = in[read];
    if (chr == '+')
      chr = 32;
    else if (chr == '%') {
      hex0 = in[read+1];
      hex1 = in[read+2];
      read += 2;
      if (hex0 >= 'a')  hex0 -= 32;
      if (hex0 <= '9')
        hex0 -= '0';
      else
        hex0 -= 'A'-10;
      if (hex1 >= 'a')  hex1 -= 32;
      if (hex1 <= '9')
        hex1 -= '0';
      else
        hex1 -= 'A'-10;
      chr = 16*hex0 + hex1;
    }
    out[write++] = chr;
    read++;
  } while (read < max);

  return write;
}



int cgi_extractparameter(char *in, char **name, int *nlen, char **val, int *vlen) {
// read an argument from the url-arguments
// returns 0 or next value of 'in'
//
  int namedone, p;

  p = 0;
  *name = in;
  *nlen = 0;
  *val = NULL;
  *vlen = 0;
  namedone = 0;
  while (in < _cgi_parameters+_cgi_parameterssize) {
    if (*in == '&') {
      *in = 0;
      if (in + 1 < _cgi_parameters+_cgi_parameterssize)  return (int)(in+1);
      return 0;
    } else if ((*in == '=') && (namedone)) {
      *in = '\0';
      return 0;
    } else if (*in == '=') {
      *in = '\0';
      namedone = 1;
    } else if (namedone) {
      if (*val == NULL)  *val = in;
      *vlen = *vlen + 1;
    } else
      *nlen = *nlen + 1;
    in++;
  }

  *in = '\0';
  return 0;
}




void cgi_webjamescommand(char *ptr, int release) {
// send a command to WebJames
//
// ptr              command string
// release          set to 1 to tell WebJames to release the buffer

  int msg[64];

  if (strlen(ptr) > 220) {
    msg[0] = 28;
    if (release)
      msg[5] = 6;
    else
      msg[5] = 2;
    msg[6] = (int)ptr;
  } else {
    msg[0] = (27+strlen(ptr)) &~3;
    msg[5] = 0;
    strcpy((char *)(msg+6), ptr);
    if (release)  _swix(OS_Module, _IN(0)|_IN(2), 7, ptr);
  }
  msg[4] = MESSAGE_WEBJAMES_CMD;
  _swix(Wimp_SendMessage, _IN(0)|_IN(1)|_IN(2), 17, msg, 0);
}



void cgi_writestring(char *string) {
// write a string to the socket, include CR LF
//
// string           string to write to the socket

  int written;

  cgi_writetosocket(string, strlen(string), &written);
}


// -------------------------------------------------------------------


void chat_url_received(int size, int flags, char *data) {

  char *argv[100], *next, *var, *val;
  int argc, varlen, vallen, i;
  int page, nick, chn, id, msg, to;

  argc = 0;
  argv[argc] = strtok(data, " ");
  while (argv[argc]) {
    argv[++argc] = strtok(NULL, " ");
  }

  cgi_init(argc, argv);

  if (_cgi_parametercount == 0) {
    chat_start();
    cgi_quit();
    return;
  }

  argc = 0;
  for (i = 0; i < 100; i++)  argv[i] = NULL;
  page = msg = nick = chn = id = to = -1;
  next = _cgi_parameters;
  do {
    next = (char *)cgi_extractparameter(next, &var, &varlen, &val, &vallen);
    if ((varlen) && (argc < 100)) {
      argv[argc] = val;
      if (strcmp(var, "p") == 0)
        page = argc;
      else if (strcmp(var, "msg") == 0)
        msg = argc;
      else if (strcmp(var, "nick") == 0)
        nick = argc;
      else if (strcmp(var, "chn") == 0)
        chn = argc;
      else if (strcmp(var, "id") == 0)
        id = argc;
      else if (strcmp(var, "to") == 0)
        to = argc;
      argc++;
    }
  } while (next);

  if (page == -1) {
    chat_start();
    cgi_quit();
    return;
  }

  if ((strcmp(argv[page], "login") == 0) && (nick >= 0) && (chn >= 0)) {
    chat_login(argv[nick], argv[chn]);
  } else if ((strcmp(argv[page], "history") == 0) && (id >= 0)) {
    chat_history(argv[id]);
  } else if ((strcmp(argv[page], "input") == 0) && (id >= 0) && (msg >= 0)) {
    if (to >= 0)
      chat_input(argv[id], argv[to], argv[msg]);
    else
      chat_input(argv[id], "*", argv[msg]);
  } else if ((strcmp(argv[page], "enter") == 0) && (id >= 0)) {
    chat_enter(atoi(argv[id]));
  } else {
    cgi_writestring(RESPONS_OK);
    cgi_writestring(PANIC);
  }
  cgi_quit();
}




void chat_input(char *idt, char *to, char *text) {

  int chn, usr, written, i, msg, id;
  char temp[200];

  id = atoi(idt);
  chn = (id-1)>>8;
  usr = (id-1)&255;

  if ((!text) || (!idt) || (!to)) {
    chat_enter(1+usr+256*chn);
    return;
  }
  if ((chn > MAXCHANNELS) || (usr > MAXUSERS)) {
    chat_enter(1+usr+256*chn);
    return;
  }
  if ((!channels[chn].used) || (!users[usr].used)) {
    chat_enter(1+usr+256*chn);
    return;
  }

  users[usr].lastactive = clock();

  written = cgi_decodeescaped(text, text, strlen(text));
  text[written] = '\0';
  if (strlen(text) > 100)  text[100] = '\0';

  if (*to == '*')
    sprintf(temp, PUBLICMSG, users[usr].nickname, text);
  else
    sprintf(temp, PRIVATEMSG, users[usr].nickname, text);

  text = malloc(strlen(temp)+1);
  if (!text) {
    chat_enter(id);
    return;
  }
  strcpy(text, temp);

  msg = 0;
  while ((messages[msg].text) && (msg < MAXMSGS))  msg++;
  if (msg >= MAXMSGS) {
    free(text);
    return;
  }
  messages[msg].text = text;

  if (*to == '*') {
    channel_addmsg(chn, msg);
    users[usr].sendto = -1;
  } else {
    for (i = 0; i < strlen(to); i++)  to[i] = toupper(to[i]);
    for (i = 0; i < MAXUSERS; i++)
      if (strcmp(to, users[i].upper) == 0) {
        users[usr].sendto = i;
        user_addmsg(i, msg);
        i = MAXUSERS;
      }
  }

  chat_enter(id);
}



void chat_history(char *idt) {

  int chn, usr, i, id;
  struct user *user;

  id = atoi(idt);
  chn = (id-1)>>8;
  usr = (id-1)&255;
  user = &users[usr];

  if ((chn > MAXCHANNELS) || (usr > MAXUSERS)) {
    cgi_writestring(RESPONS_OK);
    sprintf(output, HEADREFRESH "p=history&id=%d\">" HEADEND BODYTAG DOCEND, id);
    cgi_writestring(output);
    return;
  }
  if ((!channels[chn].used) || (!user->used)) {
    cgi_writestring(RESPONS_OK);
    sprintf(output, HEADREFRESH "p=history&id=%d\">" HEADEND BODYTAG DOCEND, id);
    cgi_writestring(output);
    return;
  }

  cgi_writestring(RESPONS_OK);
  sprintf(output, HEADREFRESH "p=history&id=%d\">" HEADEND BODYTAG, id);
  cgi_writestring(output);

  for (i = user->historylen-1; i >= 0; i--)
    cgi_writestring(messages[user->history[i]].text);

  cgi_writestring(DOCEND);
}



void chat_enter(int id) {

  int chn, usr, i;

  chn = (id-1)>>8;
  usr = (id-1)&255;

  cgi_writestring(RESPONS_OK);
  sprintf(output, HEADNORMAL "<h5>Channel %s</h5> " FORM_INPUT INPUT_SENDTO "Send message to: <select name=\"to\">", channels[chn].name);
  cgi_writestring(output);
  if (users[usr].sendto == -1)
    sprintf(output, OPTION_S VALUE_ALL "%s" END_OPT, channels[chn].name);
  else
    sprintf(output, OPTION VALUE_ALL "%s" END_OPT, channels[chn].name);
  cgi_writestring(output);
  for (i = 0; i < MAXUSERS; i++) {
    if (users[i].used) {
      if (users[usr].sendto == i)
        sprintf(output, OPTION_S VALUE_NICK END_OPT, users[i].nickname, users[i].nickname);
      else
        sprintf(output, OPTION VALUE_NICK END_OPT, users[i].nickname, users[i].nickname);
      cgi_writestring(output);
    }
  }
  sprintf(output, "</select><br>" INPUT_ID, id);
  cgi_writestring(output);
  cgi_writestring(INPUT_MSG INPUT_SEND "</form>" DOCEND);
}



void chat_logoff(int id) {

  int usr;

  usr = (id-1)&255;
  if (usr >= MAXUSERS)  return;
  if (users[usr].used)  user_logoff(usr);
  cgi_writestring(RESPONS_OK);
  cgi_writestring(HEADNORMAL BODYTAG "Thanks for using WJChat" DOCEND);
}



void chat_login(char *nick, char *channel) {

  int chn, i, usr, chr, written, id;
  char nick1[NICKLEN+1], nick2[NICKLEN+1], chan[16];
  struct user *user;

  if (*channel == '\0') {
    channel = chan;
    strcpy(chan, "chatroom");
  }

  // check that there's room for a new user
  if (usercount == MAXUSERS) {
    cgi_writestring(RESPONS_OK);
    cgi_writestring(HEADNORMAL BODYTAG TOOMANYUSERS DOCEND);
    return;
  }

  // check that the nickname hasn't been used before
  if (strlen(nick) > NICKLEN)  nick[NICKLEN] = '\0';
  strcpy(nick1, nick);
  for (chr = 0; chr < strlen(nick1); chr++)
    nick1[chr] = toupper(nick1[chr]);

  for (i = 0; i < MAXUSERS; i++) {
    if (users[i].used) {
      strcpy(nick2, users[i].nickname);
      for (chr = 0; chr < strlen(nick2); chr++)
        nick2[chr] = toupper(nick2[chr]);
      if (strcmp(nick1, nick2) == 0) {
        cgi_writestring(RESPONS_OK);
        cgi_writestring(HEADNORMAL BODYTAG NICKUSED DOCEND);
        return;
      }
    }
  }

  written = cgi_decodeescaped(channel, channel, strlen(channel));
  channel[written] = '\0';
  // channel names are uppercase
  for (chr = 0; chr < strlen(channel); chr++)
    channel[chr] = toupper(channel[chr]);

  // check if the channel exists
  chn = -1;
  for (i = 0; i < MAXCHANNELS; i++)
    if (strcmp(channel, channels[i].name) == 0)  chn = i;

  // if the channel doesn't exists, and it cannot be created, give error
  if ((chn == -1) && (channelcount == MAXCHANNELS)) {
    cgi_writestring(RESPONS_OK);
    cgi_writestring(HEADNORMAL BODYTAG TOOMANYCHNS DOCEND);
    return;
  }
  // if it doesn't exists, and it can be created, create it!
  if (chn == -1)    chn = channel_create(channel);

  // find an unused user-slot
  for (i = 0; i < MAXUSERS; i++)
    if (!users[i].used)  usr = i;

  // fill in the user-slot
  user = &users[usr];
  strcpy(user->nickname, nick);
  strcpy(user->upper, nick1);
  user->logintime = user->lastactive = clock();
  user->used = 1;
  user->sendto = -1;
  for (i = 0; i < HISTORYLEN; i++)  user->history[i] = NULL;
  user->historylen = 0;
  usercount++;
  id = 1+usr+256*chn;

  join_channel(usr, chn);

  cgi_writestring(RESPONS_OK);
  sprintf(output, HEADNORMAL BODYTAG "<frameset rows=\"75%%,24%%\">" TOPFRAME BOTTOMFRAME "</frameset>" DOCEND, id, id);
  cgi_writestring(output);
}



void chat_start() {

  int i;

  cgi_writestring(RESPONS_OK);
  cgi_writestring(HEADNORMAL BODYTAG "<h3><center>Welcome to WJChat</center></h3>");

  if (usercount == MAXUSERS) {
    cgi_writestring("Unfortunately the maximum number of users has been reached - please try again later...");

  } else {
    cgi_writestring("Please enter your nickname and the name of the channel you want to join");

    if (channelcount == 0)
      cgi_writestring("<p>There are no active channels.");
    else {
      cgi_writestring("<p>Current active channels are:<br><font color=\"#800000\">");
      for (i = 0; i < MAXCHANNELS; i++)
        if (channels[i].used) {
          sprintf(output, "'%s' &nbsp", channels[i].name);
          cgi_writestring(output);
        }
      cgi_writestring("</font>");
    }

    sprintf(output, "<p>Maximum allowed number of channels is %d.",MAXCHANNELS);
    cgi_writestring(output);
    if (usercount == 1)
      sprintf(output, "<p>There is 1 activ user - maximum allowed number of users is %d.", MAXUSERS);
    else
      sprintf(output, "<p>There are %d active users - maximum allowed number of users is %d.", usercount, MAXUSERS);
    cgi_writestring(output);

    cgi_writestring("<p>" FORM_LOGIN PAGE_LOGIN INPUT_NICK INPUT_CHANNEL SUBMIT_LOGIN "</form>");
  }
  cgi_writestring(DOCEND);
}


// -----------------------------------------------------------------

void join_channel(int usr, int chn) {

  int i;

  users[usr].channel = chn;
  i = 0;
  while (channels[chn].users[i] != -1)  i++;
  channels[chn].users[i] = usr;
}



void user_logoff(int usr) {

  int i, n;
  struct user *user;
  struct channel *channel;

  user = &users[usr];
  channel = &channels[user->channel];

  n = 0;
  for (i = 0; i < MAXUSERS; i++) {
    if (channel->users[i] == usr)
      channel->users[i] = -1;
    else
      n++;
  }
  if (n == 0)  channel_remove(user->channel);

  user->used = 0;
  for (i = 0; i < user->historylen; i++)
    if (user->history[i] >= 0)  user_removemsg(user->history[i]);

  usercount--;
}



void channel_remove(int chn) {

  channels[chn].used = 0;
  channelcount--;
}



int channel_create(char *name) {

  int chn, i;

  chn = 0;
  while (channels[chn].used)  chn++;

  strcpy(channels[chn].name, name);
  channels[chn].used = 1;
  for (i = 0; i < MAXUSERS; i++)     channels[chn].users[i] = -1;
  channelcount++;

  return chn;
}



void channel_addmsg(int chn, int msg) {

  int i;
  struct channel *channel;

  channel = &channels[chn];
  for (i = 0; i < MAXUSERS; i++) {
    if (channel->users[i] >= 0)  user_addmsg(channel->users[i], msg);
  }
}



void user_addmsg(int usr, int msg) {

  int i;
  struct user *user;

  user = &users[usr];

  if (user->historylen == HISTORYLEN) {
    user_removemsg(user->history[0]);
    for (i = 0; i < HISTORYLEN-1; i++)
      user->history[i] = user->history[i+1];
    user->history[HISTORYLEN-1] = msg;
  } else {
    user->history[user->historylen] = msg;
    user->historylen++;
  }

  messages[msg].users++;
}



void user_removemsg(int msg) {

  messages[msg].users--;
  if (messages[msg].users == 0) {
    free(messages[msg].text);
    messages[msg].text = NULL;
  }
}
