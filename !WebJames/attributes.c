#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "attributes.h"
#include "stat.h"
#include "cgi.h"

#include "osfscontrol.h"

#define STACKSIZE 10

typedef enum sectiontype {
	section_LOCATION,
	section_DIRECTORY,
	section_FILES,
	section_NONE
} sectiontype;

static struct attributes *uriattributeslist, *dirattributeslist;
static struct attributes **uriattrlist;
static int starturiattr[57];
static int uriattributescount, dirattributescount;

static void insert_attributes(struct attributes *attr, enum sectiontype section) {
// insert an attributes structure at the right position in the linked list
  struct attributes *prev, *this;
  int more;

  switch (section) {
    case section_LOCATION:
      this = uriattributeslist;
      break;
    case section_DIRECTORY:
      this = dirattributeslist;
    default:
      this = NULL;
  }

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
  if (prev) {
    prev->next = attr;
  } else {
    switch (section) {
      case section_LOCATION:
        uriattributeslist = attr;
        break;
      case section_DIRECTORY:
        dirattributeslist = attr;
        break;
      default:
        break;
    }
  }

  switch (section) {
    case section_LOCATION:
      uriattributescount++;
      break;
    case section_DIRECTORY:
      dirattributescount++;
      break;
    default:
      break;
  }

}


static void scan_filetype_list(char *list, struct attributes *attr, int allowed) {

  int count, filetypeslist[256], *oldlist, *newlist, more;

  if (allowed) {
    count = attr->allowedfiletypescount;
    oldlist = attr->allowedfiletypes;
  } else {
    count = attr->forbiddenfiletypescount;
    oldlist = attr->forbiddenfiletypes;
  }

  if ((count > 0) && (oldlist))    memcpy(filetypeslist, oldlist, count*4);

  more = 1;
  do {
    int n, f, ch;

    n = sscanf(list, "%x%n", &f, &ch);
    list += ch;
    if (n == 1) {
      filetypeslist[count++] = f;
    } else {
      more = 0;
    }

    if (strchr(list, ',')) list = strchr(list, ',')+1;
  } while (more);

  if (!count)  return;
  newlist = malloc(4*count);
  if (!newlist)  return;
  if (oldlist)  free(oldlist);

  memcpy(newlist, filetypeslist, 4*count);
  if (allowed) {
    attr->allowedfiletypes = newlist;
    attr->allowedfiletypescount = count;
  } else {
    attr->forbiddenfiletypes = newlist;
    attr->forbiddenfiletypescount = count;
  }
}



static void scan_host_list(char *list, struct attributes *attr, int allowed) {

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
    int n, a0, a1, a2, a3, ch;

    n = sscanf(list, "%d.%d.%d.%d%n", &a0, &a1, &a2, &a3, &ch);
    list += ch;

    if (n == 4) {
      hostlist[count++] = a0 | (a1<<8) | (a2<<16) | (a3<<24);
    } else {
      more = 0;
    }

    if (strchr(list, ',')) list = strchr(list, ',')+1;
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



static struct attributes *create_attribute_structure(char *uri) {

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
  attr->forbiddenfiletypes = attr->allowedfiletypes = NULL;
  attr->forbiddenfiletypescount = attr->allowedfiletypescount = 0;
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

static enum sectiontype stack(enum sectiontype type, int push) {
// if push!=0 then push type onto the stack, else pop a value and return it
// reset the stack if push == -1
  static enum sectiontype stack[STACKSIZE];
  static int size;

  switch (push) {
    case -1:
      // reset the stack
      size = 0;
      break;
    case 0:
      // pop value from stack
      if (size > 0) return stack[--size];
      break;
    default:
      // push value to the stack
      if (size >= STACKSIZE) break;
      stack[size++] = type;
  }
  return section_NONE;
}

static struct attributes *read_attributes_file(char *filename, char *base) {
// filename - name of the attributes file
// base - base uri
//

  FILE *file;
  char line[256], *attribute, *ptr, *end, uri[256];
  int i;
  struct attributes *attr;
  enum sectiontype section;

  attr = NULL;

  switch (base[0]) {
    case '\0':
      section = section_NONE;
      break;
    case '/':
      section = section_LOCATION;
      break;
    default:
      section = section_DIRECTORY;
      attr = create_attribute_structure(base);
      if (!attr) return NULL;
  }

  file = fopen(filename, "r");
  if (!file)  return NULL;

  while (!feof(file)) {
    line[0] = '\0';  // clear the linebuffer
    fgets(line, 255, file);

    // remove all leading whitespace
    attribute = line;
    while (*attribute && isspace(*attribute)) attribute++;
    ptr = attribute;

    // remove any trailing whitespace
    end = ptr;
    while (*end) end++;
    while (end>ptr && isspace(*(end-1))) end--;
    *end='\0';

    // **** lines are either start-of-section
    if ((ptr[0] == '[') || (ptr[0] == '<')) {
      // start of a new section
      char *type = NULL;
      int len;

      if ((ptr[0] == '[') || (ptr[0] == '<' && ptr[1] == '/')) {
        // end of a section
        if (attr) {
          insert_attributes(attr,section);
          section = stack(section_NONE,0);
        }
        if (ptr[0] == '<') continue; // end of section, but not the start of a new one
      }

      if (ptr[0] == '<') {
        ptr++;
        // skip whitespace
        while (*ptr != '\0' && isspace(*ptr)) ptr++;
        type = ptr;
        // find end of type, and start of URI/filename
        while (*ptr!='\0' && !isspace(*ptr)) ptr++;
        *ptr = '\0';
      }

      ptr++;
      len = strlen(ptr);

      if (type == NULL || strcmp(type,"location") == 0) {
        if (ptr[0] != '/')  continue;        // MUST start with a /
        if (section != section_NONE && section != section_LOCATION) continue; // cannot have a location inside anything else

        stack(section,1);
        section = section_LOCATION;
        ptr[len-1] = '\0';
        len--;
#ifndef CASESENSITIVE
        // URIs are case-insensitive
        for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
#endif

        sprintf(uri, "%s%s", base, ptr);
        attr = create_attribute_structure(uri);
        if (!attr) {
          fclose(file);
          return NULL;
        }
      } else if (strcmp(type,"directory") == 0) {
        char buffer[256];
        int spare;

        if (section != section_NONE) continue; // cannot have a directory inside anything else

        stack(section,1);
        section = section_DIRECTORY;
        ptr[len-1] = '\0';
        len--;

        sprintf(uri, "%s%s", base, ptr);

        if (xosfscontrol_canonicalise_path(uri,buffer,NULL,NULL,255,&spare)) strcpy(buffer,uri);

        attr = create_attribute_structure(buffer);
        if (!attr) {
          fclose(file);
          return NULL;
        }
      } else {
        // must be a <files> (not supported yet)
      }

    // **** or an attribute and value
    } else {
      char *value = NULL;

      ptr = attribute;
      // find end of attribute
      while (*ptr!='\0' && !isspace(*ptr) && *ptr!='=') ptr++;

      if (*ptr) {
        *(ptr++) = '\0';

      	// remove any whitespace and '=' at start of attribute value
      	while (*ptr!='\0' && (isspace(*ptr) || *ptr=='=')) ptr++;

      	if (*ptr) {
          value = malloc(strlen(ptr)+1);
          if (!value) {                  // malloc failed
            fclose(file);
            return NULL;
          }
          strcpy(value, ptr);            // make copy of value
      	}
      }

      // **** either inside a section
      if (attr) {

        if (strcmp(attribute, "defaultfile") == 0) {
          if (section == section_LOCATION && attr->uri[attr->urilen-1] != '/') continue;
          if (attr->defaultfile)  free(attr->defaultfile);
          attr->defined.defaultfile = 1;
          attr->defaultfile = value;

        } else if ((strcmp(attribute, "homedir") == 0)) {
          if (section == section_LOCATION && attr->uri[attr->urilen-1] == '/') continue;
          // define where on the harddisc the directory is stored
          if (attr->homedir)  free(attr->homedir);
          attr->defined.homedir = 1;
          attr->homedir = value;
          if (value)
            // calc how many chars at the start of the URI will be
            // replaced by the home-directory-path
            attr->ignore = attr->urilen-1;
          else
            attr->ignore = 0;

        } else if (strcmp(attribute, "moved") == 0) {
          // URI has been changed
          if (attr->moved)  free(attr->moved);
          attr->defined.moved = 1;
          attr->moved = value;
        } else if (strcmp(attribute, "tempmoved") == 0) {
          if (attr->tempmoved)  free(attr->tempmoved);
          attr->defined.tempmoved = 1;
          attr->tempmoved = value;

        } else if (strcmp(attribute, "realm") == 0) {
          // define realm or password-protected data
          if (attr->realm)  free(attr->realm);
          attr->defined.realm = 1;
          attr->realm = value;
        } else if (strcmp(attribute, "accessfile") == 0) {
          // name of file holding all login:password pairs
          if (attr->accessfile)  free(attr->accessfile);
          attr->defined.accessfile = 1;
          attr->accessfile = value;
        } else if (strcmp(attribute, "userandpwd") == 0) {
          // login:password pair
          if (attr->userandpwd)  free(attr->userandpwd);
          attr->defined.userandpwd = 1;
          attr->userandpwd = value;

        } else if (strcmp(attribute, "port") == 0) {
          // allowed port
          if (value) {
            attr->port = atoi(value);
            free(value);
            attr->defined.port = 1;
          }

        } else if (strcmp(attribute, "is-cgi") == 0) {
          // URI is cgi
          attr->defined.is_cgi = 1;
          attr->is_cgi = 1;
        } else if (strcmp(attribute, "isnt-cgi") == 0) {
          // URI isn't cgi
          attr->defined.is_cgi = 1;
          attr->is_cgi = 0;
        } else if (strcmp(attribute, "cgi-api") == 0) {
          // which type of cgi
          attr->defined.cgi_api = 1;
          if (strcmp(value, "redirect") == 0) {
            attr->cgi_api = CGI_API_REDIRECT;
            attr->defined.cgi_api = 1;
          } else if (strcmp(value, "webjames") == 0) {
            attr->cgi_api = CGI_API_WEBJAMES;
            attr->defined.cgi_api = 1;
          }
          if (value)  free(value);

        } else if (strcmp(attribute, "forbidden-hosts") == 0) {
          // list of hosts that may not access the data
          if (value) {
            scan_host_list(value, attr, 0);
            free(value);
          }
        } else if (strcmp(attribute, "allowed-hosts") == 0) {
          // list of hosts that may access the data
          if (value) {
            scan_host_list(value, attr, 1);
            free(value);
          }

        } else if (strcmp(attribute, "forbidden-filetypes") == 0) {
          // list of filetypes that may not be treated as cgi scripts
          if (value) {
            scan_filetype_list(value, attr, 0);
            free(value);
          }
        } else if (strcmp(attribute, "allowed-filetypes") == 0) {
          // list of filetypes that may be treated as cgi scripts
          if (value) {
            scan_filetype_list(value, attr, 1);
            free(value);
          }

        } else if (strcmp(attribute, "methods") == 0) {
          // allowed request-methods
          if (value) {
            for (i = 0; i < strlen(value); i++)  value[i] = toupper(value[i]);
            attr->defined.methods = 1;
            attr->methods = 0;
            if (strstr(value, "\"GET\""))     attr->methods |= 1<<METHOD_GET;
            if (strstr(value, "\"POST\""))    attr->methods |= 1<<METHOD_POST;
            if (strstr(value, "\"HEAD\""))    attr->methods |= 1<<METHOD_HEAD;
            if (strstr(value, "\"DELETE\""))  attr->methods |= 1<<METHOD_DELETE;
            if (strstr(value, "\"PUT\""))     attr->methods |= 1<<METHOD_PUT;
            free(value);
          }

        } else if (strcmp(attribute, "priority") == 0) {
          // priority/bandwidth - NOT SUPPORTED
          if (value)  free(value);

        } else if (strcmp(attribute, "ram-faster") == 0) {
          // allow WebJames to move the file to a faster media - NOT SUPPORTED

        } else if (strcmp(attribute, "notcacheable") == 0) {
          // data is not cacheable
          attr->defined.cacheable = 1;
          attr->cacheable = 0;
        } else if (strcmp(attribute, "cacheable") == 0) {
          // data is cacheable
          attr->defined.cacheable = 1;
          attr->cacheable = 1;

        } else if (strcmp(attribute, "more-attributes") == 0) {
          if (value) {
            if (attr->uri[attr->urilen-1] == '/') {
              strcpy(uri, attr->uri);
              uri[attr->urilen-1] = '\0';         // remove the terminating /
              read_attributes_file(value, uri);
            }
            free(value);
          }

        } else if (strcmp(attribute, "hidden") == 0) {
          // file does not exist
          attr->defined.hidden = 1;
          attr->hidden = 1;
        } else if (strcmp(attribute, "nothidden") == 0) {
          // file _does_ exist
          attr->defined.hidden = 1;
          attr->hidden = 0;

        } else {
          if (value)  free(value);
        }

      // **** or outside all sections
      } else {
        if (strcmp(attribute, "more-attributes") == 0) {
          if (value) read_attributes_file(value, base);
        }
        if (value) free(value);
      }
    }
  }
  if (attr) {
    insert_attributes(attr,section);
  }

  fclose(file);
  return attr;
}


static void merge_attributes2(struct connection *conn, struct attributes *attr) {
// merge all attributes from attr into conn

  if (attr->defined.realm)        conn->realm           = attr->realm;
  if (attr->defined.accessfile)   conn->accessfile      = attr->accessfile;
  if (attr->defined.userandpwd)   conn->userandpwd      = attr->userandpwd;
  if (attr->defined.cgi_api)      conn->cgi_api         = attr->cgi_api;
  if (attr->defined.is_cgi)       conn->flags.is_cgi    = attr->is_cgi;
  if (attr->allowedfiletypescount) {
    conn->allowedfiletypes = attr->allowedfiletypes;
    conn->allowedfiletypescount = attr->allowedfiletypescount;
  }
  if (attr->forbiddenfiletypescount) {
    conn->forbiddenfiletypes = attr->forbiddenfiletypes;
    conn->forbiddenfiletypescount = attr->forbiddenfiletypescount;
  }
  if (attr->defined.methods)
    if (!(attr->methods & (1<<conn->method)))
      conn->attrflags.accessallowed = 0;
  if (attr->defined.port)
    if (attr->port != conn->port)
      conn->attrflags.accessallowed = 0;
  if (attr->defined.hidden)   conn->attrflags.hidden = attr->hidden;
  if (attr->allowedhostscount) {
    int h, ok, ip;
    ok = 0;
    ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) |
         (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
    for (h = 0; (h < attr->allowedhostscount) && (ok == 0); h++)
      if (ip == attr->allowedhosts[h])  ok = 1;
    if (!ok)  conn->attrflags.accessallowed = 0;
  }
  if (attr->forbiddenhostscount) {
    int h, ok, ip;
    ok = 1;
    ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) |
         (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
    for (h = 0; (h < attr->forbiddenhostscount) && (ok == 1); h++)
      if (ip == attr->forbiddenhosts[h])  ok = 0;
    if (!ok)  conn->attrflags.accessallowed = 0;
  }
}

static void merge_attributes1(struct connection *conn, struct attributes *attr) {
// merge all attributes from attr into conn

  if (attr->defined.homedir) {
    conn->homedir       = attr->homedir;
    conn->homedirignore = attr->ignore;
  }
  if (attr->defined.defaultfile)  conn->defaultfile     = attr->defaultfile;
  if (attr->defined.cacheable)    conn->flags.cacheable = attr->cacheable;
  if (attr->defined.moved)        conn->moved           = attr->moved;
  if (attr->defined.tempmoved)    conn->tempmoved       = attr->tempmoved;
}

static void merge_attributes(struct connection *conn, struct attributes *attr) {
// merge all attributes from attr into conn

  merge_attributes1(conn,attr);
  merge_attributes2(conn,attr);
}

void get_uri_attributes(char *uri, struct connection *conn) {
// merge all attributes-structures that matches the URI
//
// uri              pointer to the URI to match
// conn             structure to fill in
  int i, m, start, ok;
  struct attributes *test;

  if (*uri != '/')  return;

  conn->attrflags.hidden = 0;

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

  start = starturiattr[start];
  if (start == -1)  start = 0;

  for (i = start; i < uriattributescount; i++) {
    test = uriattrlist[i];

    m = 0;
    while ((uri[m] == test->uri[m]) && (m < test->urilen))  m++;
    ok = 0;
    // only OK if the first (test->urilen) chars are identical
    if (m == test->urilen)  ok = 1;
    // if test->uri is a file (not a dir) then only OK if uri==test->uri
    if ((test->uri[m-1] != '/') && ((uri[m] != '\0') && (uri[m] != '?')))
      ok = 0;

    if (ok) merge_attributes(conn,test);

  }

  if (conn->attrflags.hidden)  conn->attrflags.accessallowed = 0;
}

void get_dir_attributes(char *dir, struct connection *conn) {
// merge all attributes-structures that matches the directory
//
// dir              pointer to the directory to match
// conn             structure to fill in
  int found;
  struct attributes *test;
  char buffer[256], path[256], *ptr;
  int len, last;

  // remove any terminating '.'
  strcpy(path,dir);
  len = strlen(path);
  if (path[len-1] == '.') path[len-1] = '\0';

  if (xosfscontrol_canonicalise_path(path,buffer,NULL,NULL,255,&len)) strcpy(buffer,path);

// do something a bit more efficient here to find the correct attributes structure for each directory
// some sort of binary search?

  ptr = buffer;
  last = 0;

  do {
    ptr = strchr(ptr+1,'.');
    if (ptr == NULL) {
      ptr = buffer+strlen(buffer);
      last = 1;
    }
    // decend through each directory till the one needed
    strncpy(path, buffer, ptr - buffer);  // copy path upto the '.' just found
    path[ptr - buffer] = '\0';

    found = 0;

    for (test = dirattributeslist; test != NULL; test = test->next) {
      // traverse through the linked list till found

      if (strcmp(path, test->uri) == 0) {
        merge_attributes(conn, test);
        found = 1;
        break;
      }
    }

    if (!found) {
      struct attributes *newattr;
      char htaccessfile[256];

      sprintf(htaccessfile,"%s.%s",path,configuration.htaccessfile);
      newattr=read_attributes_file(htaccessfile, path);
      if (newattr) merge_attributes(conn, newattr);
    }

  } while (!last);

  if (conn->attrflags.hidden)  conn->attrflags.accessallowed = 0;

}

void init_attributes(char *filename) {
// reads the attributes file
//
// filename         pointer to the name of the attributes file
//
  struct attributes *attr;
  int i, chr, start;

  uriattributeslist = NULL;
  uriattrlist = NULL;
  uriattributescount = 0;
  dirattributeslist = NULL;
  dirattributescount = 0;
  stack(section_NONE,-1);   // reset stack
  read_attributes_file(filename, "");   // read attributes in to a
                                        // sorted linked list
  if (!uriattributescount)  return;

  uriattrlist = malloc(uriattributescount*sizeof(struct attributes *));
  if (!uriattrlist)  return;

  // start position in attrlist array, indexed by first letter in the URI
  for (i = 0; i < 56; i++)  starturiattr[i] = -1;

  attr = uriattributeslist;                // convert the linked list to
                                        // a straight array - easier to
                                        // to binary search that way
  for (i = 0; i < uriattributescount; i++) {
    uriattrlist[i] = attr;

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

    if (starturiattr[start] == -1)
      starturiattr[start] = i;             // set starting point

    attr = attr->next;                  // next in linked list
  }
}

