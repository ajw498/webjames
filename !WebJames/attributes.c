#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "attributes.h"
#include "stat.h"
#include "cgi.h"


struct attributes *attributeslist;
struct attributes **attrlist;
int startattr[57];
int attributescount;

struct attributes *create_attribute_structure(char *uri);
void insert_attributes(struct attributes *attr);
void scan_host_list(char *list, struct attributes *attr, int allowed);



void init_attributes(char *filename) {
// reads the attributes file
//
// filename         pointer to the name of the attributes file
//
  struct attributes *attr;
  int i, chr, start;

  attributeslist = NULL;
  attrlist = NULL;
  attributescount = 0;
  read_attributes_file(filename, "");   // read attributes in to a
                                        // sorted linked list
  if (!attributescount)  return;

  attrlist = malloc(attributescount*sizeof(struct attributes *));
  if (!attrlist)  return;

  // start position in attrlist array, indexed by first letter in the URI
  for (i = 0; i < 56; i++)  startattr[i] = -1;

  attr = attributeslist;                // convert the linked list to
                                        // a straight array - easier to
                                        // to binary search that way
  for (i = 0; i < attributescount; i++) {
    attrlist[i] = attr;

    chr = attr->uri[1];                 // first letter after the /
    if (chr < 'A')
      start = ATTR___A;
    else if ((chr >= 'A') && (chr <= 'Z'))
      start = chr - 'A' + ATTR_A_Z;
    else if ((chr > 'Z') && (chr < 'a'))
      start = ATTR_Z_a;
    else if ((chr >= 'a') && (chr <= 'z'))
      start = chr - 'a' + ATTR_a_z;
    else
      start = ATTR_z__;

    if (startattr[start] == -1)
      startattr[start] = i;             // set starting point

    attr = attr->next;                  // next in linked list
  }
}



void insert_attributes(struct attributes *attr) {
// insert an attributes structure at the right position in the linked list
  struct attributes *prev, *this;
  int more;

  this = attributeslist;
  prev = NULL;
  more = 1;
  while ((this) && (more)) {
    if (strcmp(this->uri, attr->uri) > 0)
      more = 0;
    else {
      prev = this;
      this = this->next;
    }
  }
  attr->next = this;                    // insert the structure and
  attr->previous = prev;                // re-do the pointers in the
  if (this)  this->previous = attr;     // previous and next structures
  if (prev)
    prev->next = attr;
  else
    attributeslist = attr;

  attributescount++;
}



void scan_host_list(char *list, struct attributes *attr, int allowed) {

  int count, hostlist[256], *oldlist, *newlist, more;

  if (allowed) {
    count = attr->allowedhostscount;
    oldlist = attr->allowedhosts;
  } else {
    count = attr->forbiddenhostscount;
    oldlist = attr->forbiddenhosts;
  }

  if ((count > 0) && (oldlist))    memcpy(hostlist, oldlist, count*4);

  more = 1;
  do {
    int n, a0, a1, a2, a3;

    n = sscanf(list, "%d.%d.%d.%d", &a0, &a1, &a2, &a3);

    if (n == 4)
      hostlist[count++] = a0 | (a1<<8) | (a2<<16) | (a3<<24);

    if (strchr(list, ','))
      list = strchr(list, ',')+1;
    else
      more = 0;
  } while (more);

  if (!count)  return;
  newlist = malloc(4*count);
  if (!newlist)  return;
  if (oldlist)  free(oldlist);

  memcpy(newlist, hostlist, 4*count);
  if (allowed) {
    attr->allowedhosts = newlist;
    attr->allowedhostscount = count;
  } else {
    attr->forbiddenhosts = newlist;
    attr->forbiddenhostscount = count;
  }
}



struct attributes *create_attribute_structure(char *uri) {

  struct attributes *attr;

  attr = malloc(sizeof(struct attributes));
  if (!attr)  return NULL;

  // reset the structure to default state
  attr->cacheable = attr->hidden = attr->ignore = 0;
  attr->realm = attr->accessfile = attr->userandpwd = NULL;
  attr->homedir = attr->moved = attr->tempmoved = NULL;
  attr->defaultfile = NULL;
  attr->next = attr->previous = NULL;
  attr->forbiddenhosts = attr->allowedhosts = NULL;
  attr->forbiddenhostscount = attr->allowedhostscount = 0;
  attr->methods = (1<<METHOD_GET)|(1<<METHOD_HEAD)|(1<<METHOD_POST);
  // the flags indicate which attributes are defined for an URI
  // this is necessary as attributes may be NULL, so it is not
  // enough to simply check if the attributes are != NULL
  attr->defined.accessfile = attr->defined.userandpwd = attr->defined.realm =
    attr->defined.moved = attr->defined.tempmoved = attr->defined.cacheable =
    attr->defined.homedir = attr->defined.is_cgi = attr->defined.cgi_api =
    attr->defined.methods = attr->defined.port = attr->defined.hidden =
    attr->defined.defaultfile = 0;

  attr->urilen = strlen(uri);
  attr->uri = malloc(attr->urilen+1);
  if (!attr->uri)  return NULL;
  strcpy(attr->uri, uri);

  return attr;
}



void read_attributes_file(char *filename, char *baseuri) {

  FILE *file;
  char line[256], *ptr, *temp, uri[256];
  int read, len, i, eq;
  struct attributes *attr;

  attr = NULL;
  file = fopen(filename, "r");
  if (!file)  return;

  while (!feof(file)) {
    line[0] = '\0';                               // clear the linebuffer
    fgets(line, 255, file);

    // remove all SPACES and ctrl-chars
    read = len = 0;
    eq = 0;
    while (line[read]) {                          // remove all spaces up to '='
      if ( (line[read] > ' ') || ((eq) && (line[read] == ' ')) )
        line[len++] = line[read];
      if (line[read] == '=')  eq = 1;
      read++;
    }
    line[len] = '\0';
    ptr = line;

    // **** lines are either start-of-section
    if ((ptr[0] == '[') && (ptr[len-1] == ']') && (len >= 3)) {
      // start of a new section
      if (ptr[1] != '/')  break;        // MUST start with a /
      ptr[len-1] = '\0';
      ptr++;
      len -= 2;
#ifndef CASESENSITIVE
      // URIs are case-insensitive
      for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
#endif
      if (attr)  insert_attributes(attr);

      sprintf(uri, "%s%s", baseuri, ptr);
      attr = create_attribute_structure(uri);
      if (!attr) {
        fclose(file);
        return;
      }

    // **** or inside a section
    } else if (attr) {

      temp = NULL;

      ptr = strchr(line, '=');          // split 'variable=value' at '='
      if (ptr) {
        *ptr++ = '\0';
        while (*ptr == ' ')  ptr++;     // strip leading spaces
        if (*ptr) {
          temp = malloc(strlen(ptr)+1);
          if (!temp) {                  // malloc failed
            fclose(file);
            return;
          }
          strcpy(temp, ptr);            // make copy of value
        }
      }

      if ((strcmp(line, "defaultfile") == 0) && (attr->uri[attr->urilen-1] == '/')) {
        if (attr->defaultfile)  free(attr->defaultfile);
        attr->defined.defaultfile = 1;
        attr->defaultfile = temp;

      } else if ((strcmp(line, "homedir") == 0) && (attr->uri[attr->urilen-1] == '/')) {
        // define where on the harddisc the directory is stored
        if (attr->homedir)  free(attr->homedir);
        attr->defined.homedir = 1;
        attr->homedir = temp;
        if (temp)
          // calc how many chars at the start of the URI will be
          // replaced by the home-directory-path
          attr->ignore = attr->urilen-1;
        else
          attr->ignore = 0;

      } else if (strcmp(line, "moved") == 0) {
        // URI has been changed
        if (attr->moved)  free(attr->moved);
        attr->defined.moved = 1;
        attr->moved = temp;
      } else if (strcmp(line, "tempmoved") == 0) {
        if (attr->tempmoved)  free(attr->tempmoved);
        attr->defined.tempmoved = 1;
        attr->tempmoved = temp;

      } else if (strcmp(line, "realm") == 0) {
        // define realm or password-protected data
        if (attr->realm)  free(attr->realm);
        attr->defined.realm = 1;
        attr->realm = temp;
      } else if (strcmp(line, "accessfile") == 0) {
        // name of file holding all login:password pairs
        if (attr->accessfile)  free(attr->accessfile);
        attr->defined.accessfile = 1;
        attr->accessfile = temp;
      } else if (strcmp(line, "userandpwd") == 0) {
        // login:password pair
        if (attr->userandpwd)  free(attr->userandpwd);
        attr->defined.userandpwd = 1;
        attr->userandpwd = temp;

      } else if (strcmp(line, "port") == 0) {
        // allowed port
        if (temp) {
          attr->port = atoi(temp);
          free(temp);
          attr->defined.port = 1;
        }

      } else if (strcmp(line, "is-cgi") == 0) {
        // URI is cgi
        attr->defined.is_cgi = 1;
        attr->is_cgi = 1;
      } else if (strcmp(line, "isnt-cgi") == 0) {
        // URI isn't cgi
        attr->defined.is_cgi = 1;
        attr->is_cgi = 0;
      } else if (strcmp(line, "cgi-api") == 0) {
        // which type of cgi
        attr->defined.cgi_api = 1;
        if (strcmp(temp, "redirect") == 0) {
          attr->cgi_api = CGI_API_REDIRECT;
          attr->defined.cgi_api = 1;
        } else if (strcmp(temp, "webjames") == 0) {
          attr->cgi_api = CGI_API_WEBJAMES;
          attr->defined.cgi_api = 1;
        }
        if (temp)  free(temp);

      } else if (strcmp(line, "forbidden-hosts") == 0) {
        // list of hosts that may not access the data
        if (temp) {
          scan_host_list(temp, attr, 0);
          free(temp);
        }
      } else if (strcmp(line, "allowed-hosts") == 0) {
        // list of hosts that may access the data
        if (temp) {
          scan_host_list(temp, attr, 1);
          free(temp);
        }

      } else if (strcmp(line, "methods") == 0) {
        // allowed request-methods
        if (temp) {
          for (i = 0; i < strlen(temp); i++)  temp[i] = toupper(temp[i]);
          attr->defined.methods = 1;
          attr->methods = 0;
          if (strstr(temp, "\"GET\""))     attr->methods |= 1<<METHOD_GET;
          if (strstr(temp, "\"POST\""))    attr->methods |= 1<<METHOD_POST;
          if (strstr(temp, "\"HEAD\""))    attr->methods |= 1<<METHOD_HEAD;
          if (strstr(temp, "\"DELETE\""))  attr->methods |= 1<<METHOD_DELETE;
          if (strstr(temp, "\"PUT\""))     attr->methods |= 1<<METHOD_PUT;
          free(temp);
        }

      } else if (strcmp(line, "priority") == 0) {
        // priority/bandwidth - NOT SUPPORTED
        if (temp)  free(temp);

      } else if (strcmp(line, "ram-faster") == 0) {
        // allow WebJames to move the file to a faster media - NOT SUPPORTED

      } else if (strcmp(line, "notcacheable") == 0) {
        // data is not cacheable
        attr->defined.cacheable = 1;
        attr->cacheable = 0;
      } else if (strcmp(line, "cacheable") == 0) {
        // data is cacheable
        attr->defined.cacheable = 1;
        attr->cacheable = 1;

      } else if (strcmp(line, "more-attributes") == 0) {
        if (temp) {
          if (attr->uri[attr->urilen-1] == '/') {
            strcpy(uri, attr->uri);
            uri[attr->urilen-1] = '\0';         // remove the terminating /
            read_attributes_file(temp, uri);
          }
          free(temp);
        }

      } else if (strcmp(line, "hidden") == 0) {
        // file does not exist
        attr->defined.hidden = 1;
        attr->hidden = 1;
      } else if (strcmp(line, "nothidden") == 0) {
        // file _does_ exist
        attr->defined.hidden = 1;
        attr->hidden = 0;

      } else
        if (temp)  free(temp);

    // **** or outside all sections
    } else if (strncmp(line, "more-attributes=", 16) == 0) {
      read_attributes_file(line+16, baseuri);

    }

  }
  if (attr)  insert_attributes(attr);

  fclose(file);
}



void get_attributes(char *uri, struct connection *conn) {
// merge all attributes-structures that matches the URI
//
// uri              pointer to the URI to match
// conn             structure to fill in
  int i, m, start, ok, hidden;
  struct attributes *test;

  if (*uri != '/')  return;

  start = uri[1];                       // find starting point in attribute database
  if (start < 'A')
    start = ATTR___A;
  else if ((start >= 'A') && (start <= 'Z'))
    start = start - 'A' + ATTR_A_Z;
  else if ((start > 'Z') && (start < 'a'))
    start = ATTR_Z_a;
  else if ((start >= 'a') && (start <= 'z'))
    start = start - 'a' + ATTR_a_z;
  else
    start = ATTR_z__;

  start = startattr[start];
  if (start == -1)  start = 0;

  hidden = 0;

  for (i = start; i < attributescount; i++) {
    test = attrlist[i];

    m = 0;
    while ((uri[m] == test->uri[m]) && (m < test->urilen))  m++;
    ok = 0;
    // only OK if the first (test->urilen) chars are identical
    if (m == test->urilen)  ok = 1;
    // if test->uri is a file (not a dir) then only OK if uri==test->uri
    if ((test->uri[m-1] != '/') && ((uri[m] != '\0') && (uri[m] != '?')))
      ok = 0;

    if (ok) {
      if (test->defined.homedir) {
        conn->homedir       = test->homedir;
        conn->homedirignore = test->ignore;
      }
      if (test->defined.defaultfile)  conn->defaultfile     = test->defaultfile;
      if (test->defined.cacheable)    conn->flags.cacheable = test->cacheable;
      if (test->defined.moved)        conn->moved           = test->moved;
      if (test->defined.tempmoved)    conn->tempmoved       = test->tempmoved;
      if (test->defined.realm)        conn->realm           = test->realm;
      if (test->defined.accessfile)   conn->accessfile      = test->accessfile;
      if (test->defined.userandpwd)   conn->userandpwd      = test->userandpwd;
      if (test->defined.cgi_api)      conn->cgi_api         = test->cgi_api;
      if (test->defined.is_cgi)       conn->flags.is_cgi    = test->is_cgi;
      if (test->defined.methods)
        if (!(test->methods & (1<<conn->method)))
          conn->attrflags.accessallowed = 0;
      if (test->defined.port)
        if (test->port != conn->port)
          conn->attrflags.accessallowed = 0;
      if (test->defined.hidden)   hidden = test->hidden;
      if (test->allowedhostscount) {
        int h, ok, ip;
        ok = 0;
        ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) |
             (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
        for (h = 0; (h < test->allowedhostscount) && (ok == 0); h++)
          if (ip == test->allowedhosts[h])  ok = 1;
        if (!ok)  conn->attrflags.accessallowed = 0;
      }
      if (test->forbiddenhostscount) {
        int h, ok, ip;
        ok = 1;
        ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) |
             (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
        for (h = 0; (h < test->forbiddenhostscount) && (ok == 1); h++)
          if (ip == test->forbiddenhosts[h])  ok = 0;
        if (!ok)  conn->attrflags.accessallowed = 0;
      }

    } else if (m < 0)
      i = attributescount;              // abort
  }

  if (hidden)  conn->attrflags.accessallowed =0;
}
