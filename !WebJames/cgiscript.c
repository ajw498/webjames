#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "os.h"
#include "osfile.h"
#include "osmodule.h"
#include "wimp.h"

#include "webjames.h"
#include "cgi.h"
#include "openclose.h"
#include "report.h"
#include "stat.h"
#include "ip.h"
#include "write.h"


int script_start(int scripttype, struct connection *conn, char *script, int pwd, char *args) {
// start a script
//
// scripttype       currently always SCRIPT_CGI
// conn             connection structure
// script           script filename
// pwd              0 if not password-protected, 1 if password-protected
// args             pointer to arguments-part of the URI (eg. 'arg=value')
//
// returns 0 if object should not be treated as a cgi script, 1 if dealt with

  int size;
  fileswitch_object_type objtype;
  bits load, exec, filetype;
  fileswitch_attr attr;

  // check if cgi-script program exists
  xosfile_read_stamped_no_path(script, &objtype, &load, &exec, &size,
                   &attr, &filetype);
  if (objtype != 1) {
    report_notfound(conn);
    return 1;
  }

  if (conn->forbiddenfiletypescount) {
    int i, forbidden = 0;
  	for (i = 0; i < conn->forbiddenfiletypescount; i++) {
    	if (conn->forbiddenfiletypes[i] == filetype) forbidden = 1;
    }
    if (forbidden) return 0;
  }

  if (conn->allowedfiletypescount) {
    int i, allowed = 0;

    for (i = 0; i < conn->allowedfiletypescount; i++) {
    	if (conn->allowedfiletypes[i] == filetype) allowed = 1;
    }
    if (!allowed) return 0;
  }

  if (conn->cgi_api == CGI_API_REDIRECT)
    script_start_redirect(script, conn, args, pwd);
  else if (conn->cgi_api == CGI_API_WEBJAMES)
    script_start_webjames(scripttype, conn, script, pwd);

  return 1;
}



void set_var_val(char *name, char *value) {

  xos_set_var_val(name, (byte *)value, strlen(value), 0, 4, NULL, NULL);
}


void script_start_redirect(char *script, struct connection *conn, char *args, int pwd) {
// start a script with redirection
//
// script           script file nane
// conn             connection structure
// args             pointer to arguments-part of the URI

  char tempfile[256], input[256], cmd[256], temp[HTTPBUFFERSIZE+1];
  int size;
  FILE *file;
  wimp_t task;

  // set up the system variables

  if (server[0])  set_var_val("SERVER_SOFTWARE", server);
  set_var_val("SERVER_PORT", "80");
  set_var_val("SERVER_PROTOCOL", "HTTP/1.0");
  if (serverip[0])  set_var_val("SERVER_NAME", serverip);
  set_var_val("SERVER_ADMIN", webmaster);

  set_var_val("SCRIPT_NAME", conn->uri);
  set_var_val("PATH_TRANSLATED", script);

  if (args)
    set_var_val("QUERY_STRING", args);
  else
    set_var_val("QUERY_STRING", "");

  sprintf(temp, "%d.%d.%d.%d", conn->ipaddr[0], conn->ipaddr[1],
          conn->ipaddr[2], conn->ipaddr[3]);
  set_var_val("REMOTE_ADDR", temp);
  if (conn->dnsstatus == DNS_OK)  set_var_val("REMOTE_HOST", conn->host);

  if (conn->method == METHOD_HEAD)
    strcpy(temp, "HEAD");
  else if (conn->method == METHOD_POST)
    strcpy(temp, "POST");
  else if (conn->method == METHOD_GET)
    strcpy(temp, "GET");
  else if (conn->method == METHOD_PUT)
    strcpy(temp, "PUT");
  else if (conn->method == METHOD_DELETE)
    strcpy(temp, "DELETE");
  set_var_val("REQUEST_METHOD", temp);

  if ((conn->method == METHOD_POST) || (conn->method == METHOD_PUT)) {
    sprintf(temp, "%d", conn->bodysize);
    set_var_val("CONTENT_LENGTH", temp);
    set_var_val("CONTENT_TYPE", conn->type);
  }

  sprintf(temp, "%d", conn->port);
  set_var_val("SERVER_PORT", temp);

  if ((conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE))
    set_var_val("ENTITY_PATH", conn->requesturi);

  if (pwd) {
    set_var_val("AUTH_TYPE", "basic");
    set_var_val("REMOTE_USER", conn->authorization);
  } else {
    set_var_val("AUTH_TYPE", "");
    set_var_val("REMOTE_USER", "");
  }

  if (conn->cookie)
    set_var_val("HTTP_COOKIE", conn->cookie);
  else
    set_var_val("HTTP_COOKIE", "");

  if (conn->useragent)
    set_var_val("HTTP_USER_AGENT", conn->useragent);
  else
    set_var_val("HTTP_USER_AGENT", "");

  if (conn->referer)
    set_var_val("HTTP_REFERER", conn->referer);
  else
    set_var_val("HTTP_REFERER", "");


  input[0] = '\0';                      // clear spool-input filename

  // get unique, temporary file
  if (*cgi_out)
    strcpy(tempfile, cgi_out);
  else
    tmpnam(tempfile);

  if (conn->method == METHOD_POST) {
    if (*cgi_in)
      strcpy(input, cgi_in);
    else
      tmpnam(input);
    // generate input filename
    xosfile_save_stamped(input, 0xfff, (byte *)conn->body, (byte *)conn->body+conn->bodysize);
    // build command with redirection
    sprintf(cmd, "*%s { < %s > %s }", script, input, tempfile);
  } else {
    sprintf(cmd, "*%s { > %s }", script, tempfile);
    input[0] = '\0';                    // no file with input for the cgi-script
  }

  // execute the cgi-script
  if (xwimp_start_task(cmd, &task)) {
    // failed to start cgi-script
    if (input[0])   remove(input);
    report_badrequest(conn, "script couldn't be started");
    return;
  }

#ifdef LOG
  writelog(LOGLEVEL_CGISTART, cmd);
#endif

  // remove cgi-spool-input file (if any)
  if (input[0])   remove(input);

  // disable reading
  if (fd_is_set(select_read, conn->socket)) {
    fd_clear(select_read, conn->socket);
    readcount--;
  }

  // already set up for writing (this is done in write.c)

  // read filesize
  if (get_file_info(tempfile, NULL, NULL, &size) < 0) {
    report_badrequest(conn, "error occured when reading file info");
    return;
  }

  // open file for reading
  file = fopen(tempfile, "rb");
  if (!file) {
    report_notfound(conn);
    return;
  }


  // attempt to get a read-ahead buffer for the file
  // notice: things will still work if malloc fails
  conn->filebuffer = malloc(readaheadbuffer*1024);
  conn->flags.releasefilebuffer = 1;    // ignored if filebuffer == NULL
  conn->leftinbuffer = 0;

  conn->flags.deletefile = 1;           // delete the file when done
  strcpy(conn->filename, tempfile);

  conn->flags.is_cgi = 0;               // make close() output clf-info

  // set the fields in the structure, and that's it!
  conn->file = file;
  conn->fileused = 0;
  conn->filesize = size;

  writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
}



void script_start_webjames(int scripttype, struct connection *conn, char *script, int pwd) {
// start a 'webjames-tyle' cgi-script
//
// scripttype       cgi-, DELETE- or PUT-
// conn             connection structure
// script           script filename
// pwd              0 if not password-protected, 1 if password-protected

  int size;
  char *ptr;
  wimp_t handle;

  size = conn->headersize ;
  if (conn->bodysize > 0)  size += conn->bodysize;
  if (xosmodule_alloc(size+16, (void **)&ptr)) {
    // failed to allocate rma-block
    report_badrequest(conn, "cannot allocate memory");
    return;
  }

  memcpy(ptr, conn->header, conn->headersize);
  if (conn->bodysize > 0)
    memcpy(ptr + conn->headersize, conn->body, conn->bodysize);

  // start cgi-script
  sprintf(temp,
       "*%s -http %d -socket %d -remove -size %d -rma %d -bps %d -port %d -host %d.%d.%d.%d",
       script, conn->httpmajor*10+conn->httpminor, conn->socket, size,
       (int)ptr, bandwidth/100, conn->port, conn->ipaddr[0], conn->ipaddr[1],
       conn->ipaddr[2], conn->ipaddr[3]);
  if (conn->method == METHOD_HEAD)
    strcat(temp, " -head");
  else if (conn->method == METHOD_POST)
    strcat(temp, " -post");
  else if (conn->method == METHOD_DELETE)
    strcat(temp, " -delete");
  else if (conn->method == METHOD_PUT)
    strcat(temp, " -put");

  if (pwd) {
    *strchr(conn->authorization, ':') = '\0';
    strcat(temp, " -user ");
    strcat(temp, conn->authorization);
  }

#ifdef LOG
  writelog(LOGLEVEL_CGISTART, temp);
#endif

  if (xwimp_start_task(temp, &handle)) {
    // failed to start cgi-script
    xosmodule_free(ptr);
    report_badrequest(conn, "script couldn't be started");
    return;
  }

  // cgi-script started ok, so let the cgi-script close the socket
  if (fd_is_set(select_read, conn->socket)) {
    fd_clear(select_read, conn->socket);
    readcount--;
  }
  if (fd_is_set(select_write, conn->socket)) {
    fd_clear(select_write, conn->socket);
    writecount--;
  }
  conn->socket = -1;
  close(conn->index, 0);
}
