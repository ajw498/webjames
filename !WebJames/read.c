#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "cgi.h"
#include "attributes.h"
#include "stat.h"
#include "ip.h"
#include "read.h"
#include "report.h"
#include "write.h"
#include "coding.h"


// must be called before calling report_XXXX() during reading
static void read_report(struct connection *conn) {

  fd_clear(select_read, conn->socket);
  readcount--;

  select_writing(conn->index);
}


static void donereading(struct connection *conn) {
// header (and body) has been read, now process the request

  if (conn->httpmajor < 1) {
    read_report(conn);
    report_badrequest(conn, "bad HTTP version");
    return;
  }

  // deselect the socket for reading
  fd_clear(select_read, conn->socket);
  readcount--;

  send_file(conn);
}



int readline(char *buffer, int bytes, char *line) {
// read a header-line from the buffer; the header-line may be split
// over multiple lines and may be terminated by either CR or CRLF
//
// buffer           input buffer
// bytes            size of buffer
// line             output buffer
//
// return either 0 (failure) or number of bytes read
  int read, write, i;

  read = write = 0;

  for (i = 0; i < bytes; i++) {
    line[write] = buffer[read++];
    line[write+1] = '\0';
    if (line[write] == '\n') { // eol
      if ((write == 0) || ((write == 1) && (line[0] == '\r')) )
        return read;                              // empty line

      else {                                      // more than an empty line
        if (buffer[read] == '\t')  buffer[read] = ' ';  // conv. TAB to SPC
        if (buffer[read] == ' ')                  // continued line?
          read++;                                 // yes, so skip first char
        else
          return read;                            // no

      }
    }
    write++;
  }

  return 0;
}



void pollread_body(struct connection *conn, int bytes) {
// read part of the body in the the body-buffer
//
// conn             connection structure
// bytes            number of bytes ready to be read
//
  // reading the body

  if (conn->used + bytes > conn->bodysize) {
    read_report(conn);
    report_badrequest(conn, "body larger than Content-Length indicated");
    return;
  }

  bytes = ip_read(conn->socket, conn->body+conn->used, bytes);
  conn->used += bytes;
  if (conn->used == conn->bodysize)  donereading(conn);
}



void pollread_header(struct connection *conn, int bytes) {

  int i;
  char upper[HTTPBUFFERSIZE], *newptr;

  // calculate the number of bytes left in the temp buffer
  if (bytes + conn->used > HTTPBUFFERSIZE)
    bytes = HTTPBUFFERSIZE - conn->used;

  // read some bytes from the socket
  bytes = ip_read(conn->socket, conn->buffer+conn->used, bytes);
  conn->used += bytes;

  do {
    // attempt to read a line from the buffer
    bytes = readline(conn->buffer, conn->used, line);
    if (bytes <= 0)  return;

    // remove the used bytes from the buffer
    if (conn->used > bytes)
      memmove(conn->buffer, conn->buffer+bytes, conn->used-bytes);
    conn->used -= bytes;

    // if we need to keep the header (for a cgi-script)
    if ((conn->header) && (*line)) {
      int i;
      if (conn->headersize > 1024*maxrequestsize) {
        read_report(conn);
        report_badrequest(conn, "Request is too big for this server");
        return;
      }
      if (conn->headersize + bytes + 1 > conn->headerallocated) {
        // attempt to increase the size of the header-buffer; increase by
        // more than necessary, so we don't have to realloc() each time
        conn->headerallocated += bytes + 1024;
        newptr = realloc(conn->header, conn->headerallocated);
        if (!newptr) {
          read_report(conn);
          report_busy(conn, "No room for header");
          return;
        }
        conn->header= newptr;
      }
      i = 0;
      do {
        conn->header[conn->headersize+i] = line[i];
        i++;
      } while (line[i] >= ' ');
      conn->headersize += i;
      conn->header[conn->headersize++] = '\n';
    }

    // remove CR/LF and make an uppercase copy
    for (i = 0; i < bytes; i++) {
      if (line[i] < 32)  line[i] = '\0';
      upper[i] = toupper(line[i]);
    }

    // process the line
    if (!conn->requestline) {

      char *ptr, *file, *http;

      // save the requestline for the clf-log
      conn->requestline = malloc(strlen(line)+1);
      if (!conn->requestline) {
        read_report(conn);
        report_busy(conn, "No room for requestline");
        return;
      }
      strcpy(conn->requestline, line);

      file = line;
      while ((*file != ' ') && (*file))  file++;  // skip until SPACE
      while (*file == ' ')  file++;               // skip until /filename
      ptr = file;
      while ((*ptr != ' ') && (*ptr))  ptr++;     // skip over the filename
      if (*ptr == ' ')  *ptr++ = '\0';            // terminate /filename
      while (*ptr == ' ')  ptr++;                 // skip until HTTP/
      http = ptr;                                 // make uppercase copy
      while (*ptr) {                              // of HTTP version
        *ptr = toupper(*ptr);
        ptr++;
      }

      conn->httpmajor = 0;                        // simple request
      conn->httpminor = 9;
      if (strncmp(http, "HTTP/", 5) == 0) {       // read version number
        char *dot;

        conn->httpmajor = conn->httpminor = 0;
        dot = strchr(http+5, '.');
        if (dot) {
          *dot++ = '\0';
          conn->httpmajor = atoi(http+5);
          conn->httpminor = atoi(dot);
        }
        if ((conn->httpmajor < 1) && (conn->httpminor < 9)) {
          read_report(conn);
          report_badrequest(conn, "HTTP version < 0.9");
          return;
        }
      }
      conn->uri = malloc(strlen(file)+2);         // get buffer for filename
      if (!conn->uri)  return;                    // failed to get buffer
      strcpy(conn->uri, file);                    // fill it with filename

#ifndef CASESENSITIVE
      for (i = 0; conn->uri[i] && (conn->uri[i] != '?'); i++)
        conn->uri[i] = tolower(conn->uri[i]);     // lower-case until args
#endif

      if (strncmp(upper, "GET ", 4) == 0)
        conn->method = METHOD_GET;
      else if (strncmp(upper, "POST ", 5) == 0)
        conn->method = METHOD_POST;
      else if (strncmp(upper, "HEAD ", 5) == 0)
        conn->method = METHOD_HEAD;
      else if (strncmp(upper, "DELETE ", 7) == 0) { // DELETE request
        if (!*configuration.delete_script) {
          read_report(conn);
          report_notfound(conn);
          return;
        }
        conn->method = METHOD_DELETE;
        conn->requesturi = conn->uri;
        ptr = malloc(strlen(configuration.delete_script)+1);
        if (!ptr) {
          read_report(conn);
          report_busy(conn, "Memory low");
          return;
        }
        conn->uri = ptr;
        strcpy(conn->uri, configuration.delete_script);
#ifndef CASESENSITIVE
        for (i = 0; conn->uri[i]; i++)
           conn->uri[i] = tolower(conn->uri[i]);  // must be lowercase
#endif

      } else if (strncmp(upper, "PUT ", 4) == 0) {
        if (!*configuration.put_script) {
          read_report(conn);
          report_notfound(conn);
          return;
        }
        conn->method = METHOD_PUT;
        conn->requesturi = conn->uri;
        ptr = malloc(strlen(configuration.put_script)+1);
        if (!ptr) {
          read_report(conn);
          report_busy(conn, "Memory low");
          return;
        }
        conn->uri = ptr;
        strcpy(conn->uri, configuration.put_script);
#ifndef CASESENSITIVE
        for (i = 0; conn->uri[i]; i++)
           conn->uri[i] = tolower(conn->uri[i]);  // must be lowercase
#endif

      } else { // OPTIONS TRACE LINK UNLINK PATCH
        char *space;

        read_report(conn);
        space = strchr(upper, ' ');
        if (space)   *space = '\0';
        report_notimplemented(conn, upper);
        return;
      }

      if (conn->httpmajor < 1) {                  // HTTP 0.9
        donereading(conn);
        return;
      }

      conn->header = malloc(HTTPBUFFERSIZE);    // allocate header-buffer
      if (conn->header) {
        sprintf(conn->header, "%s\n", conn->requestline);
        conn->headersize = strlen(conn->header);
        conn->headerallocated = HTTPBUFFERSIZE;
      }

    } else if (strncmp(upper, "ACCEPT: ", 8) == 0) {
      if (conn->accept)  free(conn->accept);
      conn->accept = malloc(strlen(upper+8)+1);
      strcpy(conn->accept, upper+8);

    } else if (strncmp(upper, "CONTENT-LENGTH: ", 16) == 0) {
      conn->bodysize = atoi(upper+16);
      if (conn->bodysize < 0) {
        read_report(conn);
        report_badrequest(conn, "negative request-body size");
        return;
      }
      if (conn->bodysize > 1024*maxrequestsize) {
        read_report(conn);
        report_badrequest(conn, "Request is too big for this server");
        return;
      }

    } else if (strncmp(upper, "CONTENT-TYPE: ", 14) == 0) {
      if (conn->type)  free(conn->type);
      conn->type = malloc(strlen(upper+14)+1);
      if (conn->type)  strcpy(conn->type, upper+14);

    } else if (strncmp(upper, "COOKIE: ", 8) == 0) {
      if (conn->cookie)  free(conn->cookie);
      conn->cookie = malloc(strlen(line+8)+1);
      strcpy(conn->cookie, line+8);

#ifdef LOG
    } else if (strncmp(upper, "REFERER: ", 9) == 0) {
      if (conn->referer)  free(conn->referer);
      conn->referer = malloc(strlen(line+9)+1);
      if (conn->referer)  strcpy(conn->referer, line+9);
      writelog(LOGLEVEL_REFERER, line);

    } else if (strncmp(upper, "FROM: ", 6) == 0) {
      writelog(LOGLEVEL_FROM, line);

    } else if (strncmp(upper, "USER-AGENT: ", 12) == 0) {
      if (conn->useragent)  free(conn->useragent);
      conn->useragent = malloc(strlen(line+12)+1);
      if (conn->useragent)  strcpy(conn->useragent, line+12);
      writelog(LOGLEVEL_USERAGENT, line);

    } else if (strncmp(upper, "HOST: ", 6) == 0) {
      writelog(LOGLEVEL_FROM, line);
#endif

    } else if (strncmp(upper, "AUTHORIZATION: ", 15) == 0) {
      int pos, bytes;
      pos = 15;
      while (upper[pos] == ' ')  pos++;
      if (strncmp(upper+pos, "BASIC ", 6)) {
        read_report(conn);
        report_badrequest(conn, "unsupported authorization scheme");
        return;
      } else {
        pos += 6;
        while (line[pos] == ' ')  pos++;
        if (conn->authorization)  free(conn->authorization);
        conn->authorization = malloc(strlen(line+pos)+1);
        if (conn->authorization) {
          bytes = decode_base64(conn->authorization, line+pos, strlen(line+pos));
          conn->authorization[bytes] = 0;
          if ((bytes > 100) || (!strchr(conn->authorization, ':'))) {
            read_report(conn);
            report_badrequest(conn, "error in authorization");
            return;
          }
        } else {
          read_report(conn);
          report_busy(conn, "No room for Authorization: field");
          return;
        }
      }

    } else if (strncmp(upper, "IF-MODIFIED-SINCE: ", 19) == 0) {
      rfc_to_time(upper+19, &conn->if_modified_since);

    } else if (*line == '\0') {               // end of header

      if (conn->bodysize >= 0) {              // if there is a body...
        conn->body = malloc(conn->bodysize+4);
        if (!conn->body) {
          // if malloc failed :-(
          read_report(conn);
          report_busy(conn, "No room for the body");
          return;
        }
        if (conn->used > conn->bodysize) {
          // HELP! We've already read more than we should!
          read_report(conn);
          report_badrequest(conn, "request body overflow");
          return;
        }
        conn->status = WJ_STATUS_BODY;
        if (conn->used) {
          memcpy(conn->body, conn->buffer, conn->used);
          if (conn->used == conn->bodysize)  donereading(conn);
        }


      } else {                                // no body, so we're done!!

        if ((conn->method == METHOD_POST) || (conn->method == METHOD_PUT)) {
          // POST/PUT requests must include infoabout the size of the body
          read_report(conn);
          report_badrequest(conn, "no Content-length: field in the request");
          return;
        }



        donereading(conn);
        return;
      }

    } else {
      // ignore unrecognised header field
    }

  } while (conn->used > 0);
}



void pollread(int cn) {
// called repeatedly until the header (and body) has been read
//
  struct connection *conn;
  int bytes;

  conn = connections[cn];
  // check if there's any data ready on the socket
  bytes = ip_ready(conn->socket);
  if (bytes <= 0)  return;
  conn->timeoflastactivity = clock();

  if (conn->status == WJ_STATUS_HEADER)
    pollread_header(conn, bytes);       // if we're reading the header
  else if (conn->status == WJ_STATUS_BODY)
    pollread_body(conn, bytes);         // if we're reading the body
}
