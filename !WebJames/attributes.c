#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "write.h"
#include "attributes.h"
#include "stat.h"
#include "cgi.h"

#include "osfscontrol.h"

#define STACKSIZE 10
#define HASHINCREMENT 20

typedef enum sectiontype {
	section_LOCATION,
	section_DIRECTORY,
	section_FILES,
	section_NONE
} sectiontype;

typedef struct hashentry {
	char *uri;
	struct attributes *attr;
} hashentry;

static struct hashentry *hash=NULL;
static int hashsize, hashentries;

static struct attributes *globalfiles=NULL;

static int generate_key(char *uri, int size) {
/* generate a hash table key for given uri */
	int len, i, mult, key = 0;

	len = strlen(uri);
	mult = size/(len+1);
	for (i=0; i<len; i++) key += mult*i*uri[i];
	return key % size;
}

static void insert_into_hash(struct hashentry *hashptr, int size, char *uri, struct attributes *attr) {
/* insert into hash table */
	int key;

	key = generate_key(uri,size);
	while (hashptr[key].uri) {
		key++;
		if (key>=size) key = 0;
	}

	hashptr[key].uri = uri;
	hashptr[key].attr = attr;
}

static void insert_attributes(struct attributes *attr) {
/* insert an attributes structure into the hash table */
	int i;

	if (hashentries*4/3>hashsize) {
		/* increase the size of the hash table */
		struct hashentry *newhash;

		newhash = malloc((hashsize+HASHINCREMENT)*sizeof(struct hashentry));
		if (!newhash) return;
		for (i=0; i<hashsize+HASHINCREMENT; i++) newhash[i].uri = NULL;
		for (i=0; i<hashsize; i++) {
			if (hash[i].uri) insert_into_hash(newhash,hashsize+HASHINCREMENT,hash[i].uri,hash[i].attr);
		}
		hashsize += HASHINCREMENT;
		free(hash);
		hash = newhash;
	}

	insert_into_hash(hash,hashsize,attr->uri,attr);
	hashentries++;
}


static void scan_filetype_list(char *list, struct attributes *attr, int allowed) {

	int count, filetypeslist[256], *oldlist, *newlist, more;

	if (allowed) {
		attr->defined.allowedfiletypes = 1;
		count = attr->allowedfiletypescount;
		oldlist = attr->allowedfiletypes;
	} else {
		attr->defined.forbiddenfiletypes = 1;
		count = attr->forbiddenfiletypescount;
		oldlist = attr->forbiddenfiletypes;
	}

	if ((count > 0) && (oldlist))    memcpy(filetypeslist, oldlist, count*4);

	if (list) {
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
	}

	if (count) {
		newlist = malloc(4*count);
		if (!newlist)  return;
		memcpy(newlist, filetypeslist, 4*count);
	} else {
		newlist = NULL;
	}
	if (oldlist)  free(oldlist);

	if (allowed) {
		attr->allowedfiletypes = newlist;
		attr->allowedfiletypescount = count;
	} else {
		attr->forbiddenfiletypes = newlist;
		attr->forbiddenfiletypescount = count;
	}
}


static void scan_defaultfiles_list(char *list, struct attributes *attr) {

	char *filelist[256], **newlist;
	int count, more;

	if (attr->defaultfiles) {
		int i;

		for (i=0; i<attr->defaultfilescount; i++) free(attr->defaultfiles[i]);
		free(attr->defaultfiles);
	}

	attr->defaultfilescount = 0;
	attr->defaultfiles = NULL;
	attr->defined.defaultfile = 1;

	count = 0;

	more = 1;
	do {
		int n, ch;
		char buffer[256];

		n = sscanf(list, "%s%n", buffer, &ch);
		list += ch;

		if (n == 1) {
			filelist[count] = malloc(ch+1);
			if (filelist[count] == NULL) return;
			strcpy(filelist[count],buffer);
			count++;
		} else {
			more = 0;
		}

		if (strchr(list, ',')) list = strchr(list, ',')+1;
	} while (more);

	if (!count)  return;
	newlist = malloc(sizeof(char *)*count);
	if (!newlist)  return;

	memcpy(newlist, filelist, sizeof(char *)*count);

	attr->defaultfiles = newlist;
	attr->defaultfilescount = count;
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

	/* reset the structure to default state */
	attr->cacheable = attr->hidden = attr->ignore = attr->stripextensions = 0;
	attr->realm = attr->accessfile = attr->userandpwd = NULL;
	attr->homedir = attr->moved = attr->tempmoved = NULL;
	attr->defaultfiles = NULL;
	attr->defaultfilescount = 0;
	attr->next = NULL;
	attr->forbiddenhosts = attr->allowedhosts = NULL;
	attr->forbiddenhostscount = attr->allowedhostscount = 0;
	attr->forbiddenfiletypes = attr->allowedfiletypes = NULL;
	attr->forbiddenfiletypescount = attr->allowedfiletypescount = 0;
	attr->errordocs = NULL;
	attr->errordocscount = 0;
	attr->methods = (1<<METHOD_GET)|(1<<METHOD_HEAD)|(1<<METHOD_POST);
	/* the flags indicate which attributes are defined for an URI */
	/* this is necessary as attributes may be NULL, so it is not */
	/* enough to simply check if the attributes are != NULL */
	attr->defined.accessfile = attr->defined.userandpwd = attr->defined.realm =
		attr->defined.moved = attr->defined.tempmoved = attr->defined.cacheable =
		attr->defined.homedir = attr->defined.is_cgi = attr->defined.cgi_api =
		attr->defined.methods = attr->defined.port = attr->defined.hidden =
		attr->defined.defaultfile = attr->defined.allowedfiletypes =
		attr->defined.forbiddenfiletypes = attr->defined.stripextensions = attr->defined.setcsd = 0;

	attr->urilen = strlen(uri);
	attr->uri = malloc(attr->urilen+1);
	if (!attr->uri)  return NULL;
	strcpy(attr->uri, uri);

	return attr;
}

static int stack(int value, int push) {
/* if push!=0 then push type onto the stack, else pop a value and return it */
/* reset the stack if push == -1 */
	static int stack[STACKSIZE];
	static int size;

	switch (push) {
		case -1:
			/* reset the stack */
			size = 0;
			break;
		case 0:
			/* pop value from stack */
			if (size > 0) return stack[--size];
			break;
		default:
			/* push value to the stack */
			if (size >= STACKSIZE) break;
			stack[size++] = value;
	}
	return section_NONE;
}

static struct attributes *read_attributes_file(char *filename, char *base) {
/* filename - name of the attributes file */
/* base - base uri */


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
		line[0] = '\0';  /* clear the linebuffer */
		fgets(line, 255, file);

		/* remove all leading whitespace */
		attribute = line;
		while (*attribute && isspace(*attribute)) attribute++;
		ptr = attribute;

		/* remove any trailing whitespace */
		end = ptr;
		while (*end) end++;
		while (end>ptr && isspace(*(end-1))) end--;
		*end='\0';

		/* **** lines are either start-of-section */
		if ((ptr[0] == '[') || (ptr[0] == '<')) {
			/* start of a new section */
			char *type = NULL;
			int len;

			if ((ptr[0] == '[') || (ptr[0] == '<' && ptr[1] == '/')) {
				sectiontype oldsection;
				/* end of a section */
				oldsection = section;
				if (section != section_FILES && attr) insert_attributes(attr);
				section = (sectiontype)stack(section_NONE,0);
				if (oldsection == section_FILES) attr = (struct attributes*)stack(section_NONE,0);
				if (ptr[0] == '<') continue; /* end of section, but not the start of a new one */
			}

			if (ptr[0] == '<') {
				ptr++;
				/* skip whitespace */
				while (*ptr != '\0' && isspace(*ptr)) ptr++;
				type = ptr;
				/* find end of type, and start of URI/filename */
				while (*ptr!='\0' && !isspace(*ptr)) ptr++;
				*ptr = '\0';
			}

			ptr++;
			len = strlen(ptr);

			if (type == NULL || strcmp(type,"location") == 0) {
				if (ptr[0] != '/')  continue;        /* MUST start with a / */
				if (section != section_NONE && section != section_LOCATION) continue; /* cannot have a location inside anything else */

				stack(section,1);
				section = section_LOCATION;
				ptr[len-1] = '\0';
				len--;

				if (!configuration.casesensitive) {
					/* URIs are case-insensitive */
					for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
				}

				sprintf(uri, "%s%s", base, ptr);
				attr = create_attribute_structure(uri);
				if (!attr) {
					fclose(file);
					return NULL;
				}
			} else if (strcmp(type,"directory") == 0) {
				char buffer[256];
				int spare;

				if (section != section_NONE) continue; /* cannot have a directory inside anything else */

				stack(section,1);
				section = section_DIRECTORY;
				/* Remove the trailing > */
				ptr[len-1] = '\0';
				len--;

				if (!configuration.casesensitive) {
					/* filenames are case-insensitive */
					for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
				}

				sprintf(uri, "%s%s", base, ptr);

				if (xosfscontrol_canonicalise_path(uri,buffer,NULL,NULL,255,&spare)) strcpy(buffer,uri);

				attr = create_attribute_structure(buffer);
				if (!attr) {
					fclose(file);
					return NULL;
				}
			} else if (strcmp(type,"files") == 0) {
				struct attributes **fileslist;

				if (section != section_NONE && section != section_DIRECTORY) continue; /* cannot have a files inside a location or another files section */

				if (attr) {
					fileslist = &(attr->next);
				} else {
					fileslist = &globalfiles;
				}
				stack((int)attr,1);
				stack(section,1);
				section = section_FILES;
				/* Remove the trailing > */
				ptr[len-1] = '\0';
				len--;

				if (!configuration.casesensitive) for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);

				attr = create_attribute_structure(ptr);
				if (!attr) {
					fclose(file);
					return NULL;
				}
				attr->next = *fileslist;
				*fileslist = attr;
			} else {
				/* an unknown section type */
			}

		/* **** or an attribute and value */
		} else {
			char *value = NULL;

			ptr = attribute;
			/* find end of attribute */
			while (*ptr!='\0' && !isspace(*ptr) && *ptr!='=') ptr++;

			if (*ptr) {
				*(ptr++) = '\0';

				/* remove any whitespace and '=' at start of attribute value */
				while (*ptr!='\0' && (isspace(*ptr) || *ptr=='=')) ptr++;

				if (*ptr) {
					value = malloc(strlen(ptr)+1);
					if (!value) {                  /* malloc failed */
						fclose(file);
						return NULL;
					}
					strcpy(value, ptr);            /* make copy of value */
				}
			}

			/* **** either inside a section */
			if (attr) {

				if (strcmp(attribute, "defaultfile") == 0) {
					if (section == section_LOCATION && attr->uri[attr->urilen-1] != '/') continue;
					scan_defaultfiles_list(value,attr);
					if (value) free(value);

				} else if ((strcmp(attribute, "homedir") == 0)) {
					if (section == section_LOCATION && attr->uri[attr->urilen-1] == '/') continue;
					/* define where on the harddisc the directory is stored */
					if (attr->homedir)  free(attr->homedir);
					attr->homedir = value;
					attr->defined.homedir = 1;
					if (attr->homedir)
						/* calc how many chars at the start of the URI will be */
						/* replaced by the home-directory-path */
						attr->ignore = attr->urilen-1;
					else
						attr->ignore = 0;

				} else if (strcmp(attribute, "moved") == 0) {
					/* URI has been changed */
					if (attr->moved)  free(attr->moved);
					attr->defined.moved = 1;
					attr->moved = value;
				} else if (strcmp(attribute, "tempmoved") == 0) {
					if (attr->tempmoved)  free(attr->tempmoved);
					attr->defined.tempmoved = 1;
					attr->tempmoved = value;

				} else if (strcmp(attribute, "realm") == 0) {
					/* define realm or password-protected data */
					if (attr->realm)  free(attr->realm);
					attr->defined.realm = 1;
					attr->realm = value;
				} else if (strcmp(attribute, "accessfile") == 0) {
					/* name of file holding all login:password pairs */
					if (attr->accessfile)  free(attr->accessfile);
					attr->defined.accessfile = 1;
					attr->accessfile = value;
				} else if (strcmp(attribute, "userandpwd") == 0) {
					/* login:password pair */
					if (attr->userandpwd)  free(attr->userandpwd);
					attr->defined.userandpwd = 1;
					attr->userandpwd = value;

				} else if (strcmp(attribute, "port") == 0) {
					/* allowed port */
					if (value) {
						attr->port = atoi(value);
						free(value);
						attr->defined.port = 1;
					}

				} else if (strcmp(attribute, "stripextensions") == 0) {
					/* Strip filename extensions */
					attr->defined.stripextensions = 1;
					attr->stripextensions = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "setcsd") == 0) {
					/* Set CSD to dir containing CGI script */
					attr->defined.setcsd = 1;
					attr->setcsd = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "is-cgi") == 0) {
					/* URI is cgi */
					attr->defined.is_cgi = 1;
					attr->is_cgi = 1;
					if (value) free(value);
				} else if (strcmp(attribute, "isnt-cgi") == 0) {
					/* URI isn't cgi */
					attr->defined.is_cgi = 1;
					attr->is_cgi = 0;
					if (value) free(value);
				} else if (strcmp(attribute, "cgi-api") == 0) {
					/* which type of cgi */
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
					/* list of hosts that may not access the data */
					if (value) {
						scan_host_list(value, attr, 0);
						free(value);
					}
				} else if (strcmp(attribute, "allowed-hosts") == 0) {
					/* list of hosts that may access the data */
					if (value) {
						scan_host_list(value, attr, 1);
						free(value);
					}

				} else if (strcmp(attribute, "forbidden-filetypes") == 0) {
					/* list of filetypes that may not be treated as cgi scripts */
					scan_filetype_list(value, attr, 0);
					if (value) free(value);

				} else if (strcmp(attribute, "allowed-filetypes") == 0) {
					/* list of filetypes that may be treated as cgi scripts */
					scan_filetype_list(value, attr, 1);
					if (value) free(value);

				} else if (strcmp(attribute, "methods") == 0) {
					/* allowed request-methods */
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
					/* priority/bandwidth - NOT SUPPORTED */
					if (value)  free(value);

				} else if (strcmp(attribute, "ram-faster") == 0) {
					/* allow WebJames to move the file to a faster media - NOT SUPPORTED */
					if (value) free(value);

				} else if (strcmp(attribute, "notcacheable") == 0) {
					/* data is not cacheable */
					attr->defined.cacheable = 1;
					attr->cacheable = 0;
					if (value) free(value);
				} else if (strcmp(attribute, "cacheable") == 0) {
					/* data is cacheable */
					attr->defined.cacheable = 1;
					attr->cacheable = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "more-attributes") == 0) {
					if (value) {
						if (attr->uri[attr->urilen-1] == '/') {
							strcpy(uri, attr->uri);
							uri[attr->urilen-1] = '\0';         /* remove the terminating / */
							read_attributes_file(value, uri);
						}
						free(value);
					}

				} else if (strcmp(attribute, "hidden") == 0) {
					/* file does not exist */
					attr->defined.hidden = 1;
					attr->hidden = 1;
					if (value) free(value);
				} else if (strcmp(attribute, "nothidden") == 0) {
					/* file _does_ exist */
					attr->defined.hidden = 1;
					attr->hidden = 0;
					if (value) free(value);

				} else if (strcmp(attribute, "errordocument") == 0) {
					/* a custom error report document */
					struct errordoc *newlist;
					int status,n;

					newlist = realloc(attr->errordocs,(attr->errordocscount+1)*sizeof(struct errordoc));
					if (newlist) {
						attr->errordocs = newlist;
						attr->errordocscount++;
						/* read the status code */
						sscanf(value,"%d%n",&status,&n);
						attr->errordocs[attr->errordocscount-1].status = status;
						value+=n;
						/* skip whitespace after status code */
						while (isspace(*value)) value++;
						attr->errordocs[attr->errordocscount-1].report = value;
					}

				} else {
					if (value)  free(value);
				}

			/* **** or outside all sections */
			} else {
				if (strcmp(attribute, "more-attributes") == 0) {
					if (value) read_attributes_file(value, base);
				}
				if (value) free(value);
			}
		}
	}
	if (attr) {
		insert_attributes(attr);
	}

	fclose(file);
	return attr;
}

static void merge_attributes3(struct connection *conn, struct attributes *attr) {
/* merge all attributes from attr into conn */
	int i;
	if (attr->defined.realm)           conn->realm                 = attr->realm;
	if (attr->defined.accessfile)      conn->accessfile            = attr->accessfile;
	if (attr->defined.userandpwd)      conn->userandpwd            = attr->userandpwd;
	if (attr->defined.cgi_api)         conn->cgi_api               = attr->cgi_api;
	if (attr->defined.is_cgi)          conn->flags.is_cgi          = attr->is_cgi;
	if (attr->defined.stripextensions) conn->flags.stripextensions = attr->stripextensions;
	for (i=0; i<attr->errordocscount; i++) {
		/* add to top of linked list */
		struct errordoc *error;

		error = malloc(sizeof(struct errordoc));
		if (!error) return;
		error->status = attr->errordocs[i].status;
		error->report = attr->errordocs[i].report;
		error->next = conn->errordocs;
		conn->errordocs = error;
	}
	if (attr->defined.allowedfiletypes) {
		conn->allowedfiletypes = attr->allowedfiletypes;
		conn->allowedfiletypescount = attr->allowedfiletypescount;
	}
	if (attr->defined.forbiddenfiletypes) {
		conn->forbiddenfiletypes = attr->forbiddenfiletypes;
		conn->forbiddenfiletypescount = attr->forbiddenfiletypescount;
	}
}

static void merge_attributes2(struct connection *conn, struct attributes *attr) {
/* merge all attributes from attr into conn */
	
	if (attr->defined.setcsd)          conn->flags.setcsd          = attr->setcsd;
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
		ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) | (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
		for (h = 0; (h < attr->allowedhostscount) && (ok == 0); h++)
			if (ip == attr->allowedhosts[h])  ok = 1;
		if (!ok)  conn->attrflags.accessallowed = 0;
	}
	if (attr->forbiddenhostscount) {
		int h, ok, ip;
		ok = 1;
		ip = (conn->ipaddr[3]<<24) | (conn->ipaddr[2]<<16) | (conn->ipaddr[1]<<8)  | (conn->ipaddr[0]);
		for (h = 0; (h < attr->forbiddenhostscount) && (ok == 1); h++)
			if (ip == attr->forbiddenhosts[h])  ok = 0;
		if (!ok)  conn->attrflags.accessallowed = 0;
	}
}

static void merge_attributes1(struct connection *conn, struct attributes *attr) {
/* merge all attributes from attr into conn */

	if (attr->defined.homedir) {
		conn->homedir       = attr->homedir;
		conn->homedirignore = attr->ignore;
	}
	if (attr->defined.defaultfile) {
		conn->defaultfiles     = attr->defaultfiles;
		conn->defaultfilescount     = attr->defaultfilescount;
	}
	if (attr->defined.cacheable)    conn->flags.cacheable = attr->cacheable;
	if (attr->defined.moved)        conn->moved           = attr->moved;
	if (attr->defined.tempmoved)    conn->tempmoved       = attr->tempmoved;
}

static void merge_attributes(struct connection *conn, struct attributes *attr) {
/* merge all attributes from attr into conn */
/* split into 3 separate functions, as otherwise cc gives an internal inconsistency when memcheck is enabled */

	merge_attributes1(conn,attr);
	merge_attributes2(conn,attr);
	merge_attributes3(conn,attr);
}

void get_attributes(char *uri, struct connection *conn) {
/* merge all attributes-structures that matches the directory */
/* dir              pointer to the directory to match */
/* conn             structure to fill in */
	int found;
	char buffer[256], path[256], *ptr, splitchar, *leafname, leafnamebuffer[256];
	int len, last, first, key;

	if (uri[0] != '/') {
		/* must be a directory */

		/* remove any terminating '.' */
		strcpy(path,uri);
		len = strlen(path);
		if (path[len-1] == '.') {
			path[--len] = '\0';
			leafname = (char *)1;
		} else {
			leafname = NULL;
		}

		if (!configuration.casesensitive) {
			int i;

			for (i = 0; i < len; i++)  path[i] = tolower(path[i]);
		}

		/* make sure we can't have two different pathnames refering to the same directory */
		if (xosfscontrol_canonicalise_path(path,buffer,NULL,NULL,255,&len)) strcpy(buffer,path);
		splitchar = '.';
		if (leafname) {
			leafname = NULL;
		} else {
			leafname = strrchr(buffer,splitchar) + 1;
			if (leafname) {
				if (uri_to_filename(leafname,leafnamebuffer,0)) {
					leafname = NULL;
				} else {
					leafname = leafnamebuffer;
				}
			}
		}
	} else {
		strcpy(buffer,uri);
		splitchar = '/';
		leafname = NULL;
	}

	ptr = buffer;
	last = 0;
	first = 1;

	do {
		ptr = strchr(first ? ptr : ptr+1,splitchar);
		first = 0;
		if (ptr == NULL) {
			ptr = buffer+strlen(buffer);
			last = 1;
		}
		/* decend through each directory till the one needed */
		strncpy(path, buffer, ptr - buffer);  /* copy path upto the '.' just found */
		if (uri[0] == '/' && *ptr == '/') {
			path[ptr - buffer] = '/';
			path[ptr - buffer + 1] = '\0';
		} else {
			path[ptr - buffer] = '\0';
		}

		found = 0;

		key = generate_key(path,hashsize);
		while (hash[key].uri && !found) {
			if (strcmp(hash[key].uri,path) == 0) {
				found = 1;
				merge_attributes(conn,hash[key].attr);
				if (leafname) {
					/* Check to see if the leafname matches any <files> sections within this directory */
					struct attributes *filesattr;
					filesattr = hash[key].attr->next;
					while (filesattr != NULL) {
						if (strcmp(filesattr->uri,leafname) == 0) merge_attributes(conn,filesattr);
						filesattr = filesattr->next;
					}
				}
			}
			key++;
			if (key>=hashsize) key = 0;
		}

		if (!found && uri[0] != '/' && *configuration.htaccessfile) {
			struct attributes *newattr;
			char htaccessfile[256];

			sprintf(htaccessfile,"%s.%s",path,configuration.htaccessfile);
			newattr=read_attributes_file(htaccessfile, path);
			if (newattr) {
				merge_attributes(conn, newattr);
			} else {
				/* an error occoured (probably couldn't find the file), so create a blank structure for this dir */
				newattr=create_attribute_structure(path);
				if (newattr) insert_attributes(newattr);
			}
		}

	} while (!last);

	if (leafname) {
		/* Check to see if the leafname matches any global <files> sections */
		struct attributes *filesattr;

		filesattr = globalfiles;
		while (filesattr != NULL) {
			if (strcmp(filesattr->uri,leafname) == 0) merge_attributes(conn,filesattr);
			filesattr = filesattr->next;
		}
	}

	if (conn->attrflags.hidden)  conn->attrflags.accessallowed = 0;

}

void init_attributes(char *filename) {
/* reads the attributes file */

/* filename         pointer to the name of the attributes file */

	int i;

	stack(section_NONE,-1);   /* reset stack */

	hashsize = HASHINCREMENT; /* init hash table */
	hashentries = 0;
	hash = malloc(hashsize*sizeof(struct hashentry));
	if (!hash) return;
	for (i=0; i<hashsize; i++) hash[i].uri = NULL;

	read_attributes_file(filename, "");   /* read attributes in to the hash table */
}

