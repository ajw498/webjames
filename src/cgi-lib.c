/* cgi-lib - C library for WebJames cgi-scripts

   cgi-lib 0.03 17/11/2001
   cgi-lib 0.02 06/10/1999
   cgi-lib 0.01 14/02/1999
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
// oslib
#include "OSlib:os.h"
#include "OSlib:socket.h"
#include "OSlib:osmodule.h"
#include "OSlib:wimp.h"
#include "OSlib:osfile.h"

#include "cgi-lib.h"

#undef NULL
#define NULL 0

char _cgi_temp[2048], _cgi_requestline[4100], _cgi_headerfile[256];
char _cgi_host[128], _cgi_username[64], _cgi_taskname[32];
char *_cgi_parameters, *_cgi_headeraddress;
int _cgi_multitask, _cgi_multitasktime, _cgi_headerstorage;
int _cgi_headermeminfo, _cgi_releaseheader, _cgi_requestsize;
socket_s _cgi_socket;
int _cgi_httpversion, _cgi_requesttype;
int _cgi_maxbytespersecond, _cgi_headerdone, _cgi_byteswritten;
int _cgi_polldelay, _cgi_starttime, _cgi_responscode;
int _cgi_requestlinelength, _cgi_parameterssize, _cgi_parametercount;

int registers[10];
char _cgi_postdata[1000];


int cgi_init(int argc, char **argv) {
// initialises the cgi-library, parses the arguments
// returns NULL (ok) or 1 (error) or 2 (possible attempt to run as non-cgi)
//
// argc             number of arguments
// argv             array of argument-pointers

  int arg, i;
  char *ptr, *end;
  int length,die,nextpost;

  if (argc < 7)  return 2;

  // reset all variables
  _cgi_multitask = 0;
  _cgi_multitasktime = clock() + 5;
  _cgi_headerstorage = CGI_HEADER_UNKNOWN;
  _cgi_headeraddress = NULL;
  _cgi_headermeminfo = 0;
  _cgi_headerfile[0] = '\0';
  _cgi_releaseheader = 0;
  _cgi_requestsize = -1;
  _cgi_socket = (socket_s)-1;
  _cgi_httpversion = 0;
  _cgi_requesttype = CGI_METHOD_GET;
  _cgi_maxbytespersecond = 0;
  _cgi_host[0] = '\0';
  _cgi_username[0] = '\0';
  _cgi_headerdone = 0;
  strcpy(_cgi_taskname, "cgi-script");
  _cgi_byteswritten = 0;
  _cgi_polldelay = 0;
  _cgi_starttime = clock();
  _cgi_responscode = -1;
  _cgi_requestlinelength = -1;
  _cgi_parameters = _cgi_temp;
  _cgi_parameterssize = 0;
  _cgi_parametercount = 0;

  // parse the arguments
  arg = 1;
  while (arg < argc)
  {
   if (strcmp(argv[arg], "-socket") == 0)
   {
    if (arg+1 < argc)  _cgi_socket = (socket_s)atoi(argv[++arg]);
   }
   else if (strcmp(argv[arg], "-http") == 0)
   {
    if (arg+1 < argc)  _cgi_httpversion = atoi(argv[++arg]);
   }
   else if (strcmp(argv[arg], "-head") == 0)
     _cgi_requesttype = CGI_METHOD_HEAD;
   else if (strcmp(argv[arg], "-post") == 0)
     _cgi_requesttype = CGI_METHOD_POST;
   else if (strcmp(argv[arg], "-remove") == 0)
   {
    _cgi_releaseheader = 1;
   }
   else if (strcmp(argv[arg], "-size") == 0)
   {
    if (arg+1 < argc)  _cgi_requestsize = atoi(argv[++arg]);
   }
   else if (strcmp(argv[arg], "-file") == 0)
   {
    if (arg+1 < argc)  strncpy(_cgi_headerfile, argv[++arg], 255);
    _cgi_headerstorage = CGI_HEADER_FILE;
   }
   else if (strcmp(argv[arg], "-rma") == 0)
   {
    if (arg+1 < argc)  _cgi_headeraddress = (char *)atoi(argv[++arg]);
    _cgi_headerstorage = CGI_HEADER_RMA;
   }
   else if (strcmp(argv[arg], "-dynamic") == 0)
   {
    if (arg+1 < argc)  _cgi_headermeminfo = atoi(argv[++arg]);
    _cgi_headerstorage = CGI_HEADER_DYNAMIC;
   }
   else if (strcmp(argv[arg], "-bps") == 0)
   {
    if (arg+1 < argc)
    {
     _cgi_maxbytespersecond = atoi(argv[++arg]);
     if (_cgi_maxbytespersecond < 100)
       _cgi_maxbytespersecond = 100;
     if (_cgi_maxbytespersecond < 1000000)
       _cgi_maxbytespersecond = 1000000;
    }
   }
   else if (strcmp(argv[arg], "-host") == 0)
   {
    if (arg+1 < argc)  strncpy(_cgi_host, argv[++arg], 127);
   }
   else if (strcmp(argv[arg], "-user") == 0)
   {
    if (arg+1 < argc)  strncpy(_cgi_username, argv[++arg], 63);
   }

   arg++;
  }

  // check if the arguments are OK
  if ((_cgi_httpversion < 9) || (_cgi_headerstorage == CGI_HEADER_UNKNOWN))
    return 1;
  if ((_cgi_requestsize < 0) || ((int)_cgi_socket < 0))
    return 1;

  if (_cgi_headerstorage == CGI_HEADER_FILE) {
    xosfile_read_stamped(_cgi_headerfile, NULL, NULL, NULL, &_cgi_requestsize, NULL, NULL);
    xosmodule_alloc(_cgi_requestsize, (void **)&_cgi_headeraddress);
    xosfile_load_stamped(_cgi_headerfile, (byte *)_cgi_headeraddress,
                         NULL, NULL, NULL, NULL, NULL);
    if (_cgi_releaseheader)  remove(_cgi_headerfile);
    _cgi_headerstorage = CGI_HEADER_RMA;
    _cgi_releaseheader = 1;
  } else if (_cgi_headerstorage == CGI_HEADER_RMA) {
  } else if (_cgi_headerstorage == CGI_HEADER_DYNAMIC) {
    xosdynamicarea_read((os_dynamic_area_no)_cgi_headermeminfo,
         NULL, (byte **)&_cgi_headeraddress, NULL, NULL, NULL, NULL, NULL);
  }

  // preprocess the arguments (if any) to the cgi-script
  if (_cgi_requesttype==CGI_METHOD_GET)
  {
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
   if ((*ptr < ' ') || (ptr == end) || (ptr[1] < ' '))  return NULL;

   _cgi_parameters = ++ptr;    // skip the '?'

  }

  /* new additions by mdc - POST support!  (yay!) */
  if (_cgi_requesttype==CGI_METHOD_POST)
  {
   ptr=_cgi_headeraddress;
   end=ptr+1;
   die=0;
   nextpost=0;
   while (ptr!=NULL && end!=NULL && die==0)
   {
    ptr=strstr(ptr,"\n");
    ptr++;
    end=strstr(ptr,"\n");
    if (ptr!=NULL)
    {
     if ( ((((int) _cgi_headeraddress) - (int) end) > _cgi_requestsize) || end==NULL)
     {
      end=(char *) (((int) _cgi_headeraddress) + _cgi_requestsize);
      die=1;
     }
     length=((int) end)-((int) ptr);
     if (length>=1000) length=999;
     strncpy(_cgi_postdata,ptr,length);
     _cgi_postdata[length]=0;
     if (_cgi_postdata[0]==13) nextpost=1;
    }
   }
   if (nextpost==1)
    _cgi_parameters=_cgi_postdata;
   else
    _cgi_parameters=ptr;
  }

  if (_cgi_requesttype==CGI_METHOD_GET || _cgi_requesttype==CGI_METHOD_POST )
  {
   _cgi_parametercount = 1;
   while ((*ptr > ' ') && (ptr < end)) {
     if (*ptr == '&')  _cgi_parametercount++;
     _cgi_parameterssize++;
     ptr++;
   }
  }

  return NULL;
}



void cgi_multitask(char *taskname, int wimpslot) {
// switch to multitasking
//
// taskname         name to use for the task, max 31 chars

  int messages[1];

  if (_cgi_multitask)  return;
  _cgi_multitask = 1;
  strncpy(_cgi_taskname, taskname, 31);

  messages[0] = 0;

  xwimp_initialise(310, _cgi_taskname, (wimp_message_list *)messages, NULL, NULL);

  if (wimpslot > 0) {
  }
}


void cgi_headerdone() {
// indicate that the last line in the header has
// been written, write the last, empty, line
  if (_cgi_httpversion >= 10)  cgi_writeheader("");
  _cgi_byteswritten = 0;      // count only the body
  _cgi_headerdone = 1;
}


void cgi_respons(int code, char *text) {
// output the first line in the header
//
// code             HTTP responscode
// text             text to include in the respomsline

  char temp[256];

  _cgi_responscode = code;
  sprintf(temp, "HTTP/1.0 %d %s", code, text);
  cgi_writeheader(temp);
}


void cgi_writeheader(char *text) {
// write a header line
//
// text             buffer with the headerline

  if (_cgi_httpversion >= 10)  cgi_writestring(text);
}


void cgi_writestring(char *string) {
// write a string to the socket, include CR LF
//
// string           string to write to the socket

// Zap doesn't like CR LF - using LF instead.
  char crlf[2];

  cgi_writebuffer(string, strlen(string));
  //crlf[0] = 13;
  crlf[0] = 10;
  cgi_writebuffer(crlf, 1);
}


void cgi_reporterror(char *error) {
// report an error, write it to WebJames' log and quit
//
// error            error string

  char temp[512];

  sprintf(temp, "log CGI %s ERR %s", _cgi_taskname, error);
  cgi_webjamescommand(temp, 0);
  cgi_quit();
}


void cgi_mimetype(char *filename, char *mimetype) {
}


void cgi_sendhtml(char *buffer, int length) {
// send a HTML document
//
// buffer           pointer to HTML code
// length           size of HTML code

  char temp[100];

  cgi_respons(200, "OK");
  cgi_writeheader("Content-Type: text/html");
  sprintf(temp, "Content-Length: %d", length);
  cgi_writeheader(temp);
  cgi_headerdone();
  cgi_writebuffer(buffer, length);
}


int cgi_sendfile(char *filename) {
// send a file
//
// filename         name of the file

  char temp[200];
  FILE *file;
  int filesize;

  file = fopen(filename, "rb");
  if (!file)  return 0;

  cgi_respons(200, "OK");
  strcpy(temp, "Content-Type: ");
  cgi_mimetype(filename, temp+strlen(temp));
  cgi_writeheader(temp);

  fseek(file, 0, SEEK_END);
  filesize = (int)ftell(file);
  fseek(file, 0, SEEK_SET);
  sprintf(temp, "Content-Length: %d", filesize);
  cgi_writeheader(temp);
  cgi_headerdone();
  cgi_writefromfile(file, filesize);
  fclose(file);

  return 200;
}


void cgi_writefromfile(FILE *file, int bytes) {
// send a number of bytes from an open file
//
// file             filehandle, set to the read-position
// bytes            number of bytes to read and write

  int thistime, missed, pos;

  if ((_cgi_headerdone) && (_cgi_requesttype == CGI_METHOD_HEAD)) {
    _cgi_byteswritten += bytes;
    return;
  }

  do {
    pos = (int)ftell(file);
    thistime = bytes;
    if (thistime > 2048)  thistime = 2048;
    missed = fread(_cgi_temp, 1, thistime, file);
    if (missed)  fseek(file, (long)(pos + thistime - missed), SEEK_SET);
    cgi_writebuffer(_cgi_temp, thistime - missed);
    bytes -=  thistime - missed;
  } while (bytes > 0);
}


void cgi_writebuffer(char *buffer, int length) {
// write the contents of a buffer
//
// buffer           pointer to data
// length           bytes to write

  int written, thistime, clk;
  int *error;

  if (length <= 0)  return;

  if ((_cgi_headerdone) && (_cgi_requesttype == CGI_METHOD_HEAD)) {
    _cgi_byteswritten += length;
    return;
  }

  written = 0;
  do {
    clk = clock();
    if (_cgi_multitask)  cgi_poll();
    error = cgi_writetosocket(buffer+written, length-written, &thistime);
    if (error) {
      cgi_reporterror((char *)(error+1));
    } else {
      written += thistime;
      _cgi_byteswritten += thistime;
      if ((clk > _cgi_starttime) && (_cgi_maxbytespersecond > 0)) {
        int bps;
        bps = _cgi_byteswritten*100/(clk-_cgi_starttime);
        if (bps > _cgi_maxbytespersecond) {
          _cgi_polldelay += 10;
          if (_cgi_polldelay > 200)  _cgi_polldelay = 200;
        } else {
          _cgi_polldelay -= 20;
          if (_cgi_polldelay < 0)  _cgi_polldelay = 0;
        }
      }
    }
  } while (written != length);
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

  int *error;

  error = (int *)xsocket_write(_cgi_socket, (byte *)buffer, bytes, written);
  if (error) {
    if ((error[0] & 0xFF != 36) && (error[0] & 0xFF != 35))  return error;
  }

  return NULL;
}


void cgi_quit() {

  if (_cgi_releaseheader) {
    if (_cgi_headerstorage == CGI_HEADER_RMA) {
      xosmodule_free(_cgi_headeraddress);
    } else if (_cgi_headerstorage == CGI_HEADER_DYNAMIC) {
      xosdynamicarea_delete((os_dynamic_area_no)_cgi_headermeminfo);
    } else if (_cgi_headerstorage == CGI_HEADER_FILE) {
      remove(_cgi_headerfile);
    }
    _cgi_headerstorage = CGI_HEADER_UNKNOWN;
  }
  if ((int)_cgi_socket >= 0) {
    xsocket_close(_cgi_socket);
    _cgi_socket = (socket_s)-1;
  }
  if ((_cgi_requestlinelength > 0) && (_cgi_responscode)) {
    char *rma;

    if (!xosmodule_alloc(100+strlen(_cgi_host)+_cgi_requestlinelength, (void **)&rma)) {
      sprintf(rma, "clf %d %d %s %% %s", _cgi_responscode, _cgi_byteswritten,
                    _cgi_host, _cgi_requestline);
      cgi_webjamescommand(rma, 1);
    }
  }
  if (_cgi_multitask)  xwimp_close_down(NULL);
  exit(-1);
}


void cgi_poll() {
// (should be) called frequently

  int clk;
  wimp_block block;
  wimp_event_no action;

  if (clock() < _cgi_multitasktime)  return;

  if (_cgi_multitask) {
    if (_cgi_polldelay) {
      xos_read_monotonic_time((os_t *)&clk);
      xwimp_poll_idle(0, &block, (os_t)clk+_cgi_polldelay, NULL, &action);
    } else {
      xwimp_poll(0, &block, NULL, &action);
    }
    switch (action) {
    case wimp_USER_MESSAGE:
    case wimp_USER_MESSAGE_RECORDED:
    case wimp_USER_MESSAGE_ACKNOWLEDGE:
      if (block.message.action == 0)  cgi_quit();
      break;
    }

  } else {
    int clk;

    clk = clock();
    while (clock() < clk + _cgi_polldelay);
  }

  _cgi_multitasktime = clock() + 2;     // don't poll too often
}


void cgi_webjamescommand(char *ptr, int release) {
// send a command to WebJames
//
// ptr              command string
// release          set to 1 to tell WebJames to release the buffer


  wimp_message msg;
  webjames_message_body *body;

  body = (webjames_message_body *)&msg.data;

  if (!_cgi_multitask)  cgi_multitask("cgi-script", -1);
  if (strlen(ptr) > 220) {
    msg.size = 28;
    if (release)
      body->flags = 6;
    else
      body->flags = 2;
    body->cmd.pointer = ptr;
  } else {
    msg.size = (27+strlen(ptr)) &~3;
    body->flags = 0;
    strcpy(body->cmd.embedded, ptr);
  }
  msg.action = MESSAGE_WEBJAMES_CMD;
  xwimp_send_message(wimp_USER_MESSAGE, &msg, 0);
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
