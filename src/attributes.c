/*
	$Id$
	Reading and using attributes files
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "webjames.h"
#include "wjstring.h"
#include "write.h"
#include "attributes.h"
#include "stat.h"
#include "handler.h"
#include "content.h"

#include "oslib/osfscontrol.h"

#define STACKSIZE 50
#define HASHINCREMENT 20

typedef enum sectiontype {
	section_NONE,
	section_LOCATION,
	section_DIRECTORY,
	section_FILES
} sectiontype;

typedef struct hashentry {
	char *uri;
	struct attributes *attr;
} hashentry;

typedef struct handlerlist {
	char *extension;
	int filetype;
	struct handler *handler;
	struct handlerlist *attrnext; /* next entry in attributes linked list */
	struct handlerlist *connnext; /* next entry in connection linked list */
} handlerlist;

static struct vhostdetails defaultvhost;

static struct vhostdetails *vhostlist = NULL;

static struct vhostdetails *init_vhost(struct vhostdetails *vhost)
{
	int i;
	vhost->domain = configuration.serverip;
	vhost->homedir = configuration.site;
	vhost->serveradmin = configuration.webmaster;
	vhost->globalfiles = NULL;
	vhost->globallocations = NULL;
	vhost->globaldirectories = NULL;
	vhost->globalhandlers = NULL;
	vhost->next = NULL;
	vhost->hashsize = HASHINCREMENT; /* Init hash table */
	vhost->hashentries = 0;
	vhost->hash = EM(malloc(vhost->hashsize*sizeof(struct hashentry)));
	if (vhost->hash == NULL) return NULL;
	for (i=0; i<vhost->hashsize; i++) vhost->hash[i].uri = NULL;
	return vhost;
}

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

static void insert_attributes(struct attributes *attr, struct vhostdetails *vhost) {
/* insert an attributes structure into the hash table */
	int i;

	if (vhost->hashentries*4/3>vhost->hashsize) {
		/* increase the size of the hash table */
		struct hashentry *newhash;

		newhash = EM(malloc((vhost->hashsize+HASHINCREMENT)*sizeof(struct hashentry)));
		if (newhash==NULL) return;
		for (i=0; i<vhost->hashsize+HASHINCREMENT; i++) newhash[i].uri = NULL;
		for (i=0; i<vhost->hashsize; i++) {
			if (vhost->hash[i].uri) insert_into_hash(newhash,vhost->hashsize+HASHINCREMENT,vhost->hash[i].uri,vhost->hash[i].attr);
		}
		vhost->hashsize += HASHINCREMENT;
		free(vhost->hash);
		vhost->hash = newhash;
	}

	insert_into_hash(vhost->hash,vhost->hashsize,attr->uri,attr);
	vhost->hashentries++;
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
		newlist = EM(malloc(4*count));
		if (newlist==NULL)  return;
		memcpy(newlist, filetypeslist, 4*count);
	} else {
		newlist = NULL;
	}
	if (oldlist) free(oldlist);

	if (allowed) {
		attr->allowedfiletypes = newlist;
		attr->allowedfiletypescount = count;
	} else {
		attr->forbiddenfiletypes = newlist;
		attr->forbiddenfiletypescount = count;
	}
}


static void scan_defaultfiles_list(char *list, struct attributes *attr) {

	char *filelist[MAX_FILENAME], **newlist;
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
		char buffer[MAX_FILENAME];

		n = sscanf(list, "%s%n", buffer, &ch);
		list += ch;

		if (n == 1) {
			filelist[count] = EM(malloc(ch+1));
			if (filelist[count] == NULL) return;
			wjstrncpy(filelist[count],buffer,ch+1);
			count++;
		} else {
			more = 0;
		}

		if (strchr(list, ',')) list = strchr(list, ',')+1;
	} while (more && count<MAX_FILENAME);

	if (!count)  return;
	newlist = EM(malloc(sizeof(char *)*count));
	if (newlist==NULL)  return;

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
	newlist = EM(malloc(4*count));
	if (newlist==NULL)  return;
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

	attr = EM(malloc(sizeof(struct attributes)));
	if (attr==NULL)  return NULL;

	/* reset the structure to default state */
	attr->cacheable = attr->hidden = attr->ignore = attr->stripextensions = attr->multiviews = 0;
	attr->realm = attr->accessfile = attr->userandpwd = NULL;
	attr->homedir = attr->moved = attr->tempmoved = NULL;
	attr->regex = NULL;
	attr->defaultfiles = NULL;
	attr->defaultfilescount = 0;
	attr->attrnext = NULL;
	attr->forbiddenhosts = attr->allowedhosts = NULL;
	attr->forbiddenhostscount = attr->allowedhostscount = 0;
	attr->forbiddenfiletypes = attr->allowedfiletypes = NULL;
	attr->forbiddenfiletypescount = attr->allowedfiletypescount = 0;
	attr->errordocs = NULL;
	attr->errordocscount = 0;
	attr->handlers = NULL;
	attr->overridefilename = NULL;
	attr->methods = (1<<METHOD_GET)|(1<<METHOD_HEAD)|(1<<METHOD_POST);
	/* the flags indicate which attributes are defined for an URI */
	/* this is necessary as attributes may be NULL, so it is not */
	/* enough to simply check if the attributes are != NULL */
	attr->defined.accessfile = attr->defined.userandpwd = attr->defined.realm =
		attr->defined.moved = attr->defined.tempmoved = attr->defined.cacheable =
		attr->defined.homedir = attr->defined.is_cgi = attr->defined.cgi_api =
		attr->defined.methods = attr->defined.port = attr->defined.hidden =
		attr->defined.defaultfile = attr->defined.allowedfiletypes =
		attr->defined.forbiddenfiletypes = attr->defined.overridefilename =
		attr->defined.stripextensions = attr->defined.multiviews =
		attr->defined.setcsd = attr->defined.autoindex =
		attr->defined.mimeuseext = 0;

	attr->urilen = strlen(uri);
	attr->uri = EM(malloc(attr->urilen+1));
	if (attr->uri==NULL)  return NULL;
	wjstrncpy(attr->uri, uri,attr->urilen+1);

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

void lower_case(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

static struct attributes *read_attributes_file(char *filename, char *base, struct vhostdetails *vhost) {
/* filename - name of the attributes file */
/* base - base uri */


	FILE *file;
	char line[256], *attribute, *ptr, *end, uri[MAX_FILENAME];
	int i;
	struct attributes *attr;
	enum sectiontype section;
	int active = 1; /* Is the file active at this point, ie not in a failed <ifhandler> */

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
			char *type = "location";
			int len;

			if ((ptr[0] == '[') || (ptr[0] == '<' && ptr[1] == '/')) {
				sectiontype oldsection;
				/* end of a section */
				oldsection = section;
				if (section != section_FILES && attr) {
					if (attr->regex == NULL) insert_attributes(attr, vhost);
				}
				attr = NULL;

				lower_case(ptr);

				/* Only pop if we're active or it's the end of an if section */
				if (!active && strncmp(ptr,"</ifhandler",10) != 0) continue;

				active = stack(section_NONE,0);
				vhost = (struct vhostdetails*)stack(section_NONE,0);
				if (vhost == NULL) vhost = vhostlist;
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
				while (*ptr!='\0' && !isspace(*ptr) && *ptr != '>') ptr++;
				*ptr = '\0';
				lower_case(type);
			}

			ptr++;
			len = strlen(ptr);

			if (strcmp(type,"ifhandler") == 0) {
				int sense;

				/* Remove the trailing > */
				ptr[--len] = '\0';

				/* Remove any trailling spaces */
				while (isspace(ptr[len-1]) && len > 0) len--;

				if (ptr[0] == '!') {
					sense = 1;
					ptr++;
				} else {
					sense = 0;
				}
				stack(section,1);
				stack((int)vhost,1);
				stack(active,1);
				if (active) active = sense ^ (get_handler(ptr) != NULL);
				continue;
			}

			if (!active) continue;

			if (strcmp(type,"virtualhost") == 0) {
				struct vhostdetails *newvhost;

				if (section != section_NONE) {
					/* cannot have a virtualhost section inside anything else */
					webjames_writelog(LOGLEVEL_ATTRIBUTES,"Error in attributes file: virtualhost section inside another section");
					continue;
				}

				stack(section,1);
				stack((int)vhost,1);
				stack(active,1);

				/* Find end of vhostlist */
				vhost = vhostlist;
				while (vhost->next) vhost = vhost->next;
				newvhost = EM(malloc(sizeof(struct vhostdetails)));
				if (newvhost == NULL) {
					fclose(file);
					return NULL;
				}

				if (init_vhost(newvhost) == NULL) {
					fclose(file);
					return NULL;
				}
				vhost->next = newvhost;
				vhost = newvhost;


			} else if (strcmp(type,"location") == 0 || strcmp(type,"locationmatch") == 0) {
				int match = 0;

				if (section != section_NONE && section != section_LOCATION) continue; /* cannot have a location inside anything else */

				if (type[8] != '\0') match = 1;
				if (!match && ptr[0] != '/')  continue;        /* MUST start with a / */

				stack(section,1);
				stack((int)vhost,1);
				stack(active,1);
				section = section_LOCATION;
				/* Remove the trailing > */
				ptr[len-1] = '\0';
				len--;
				/* Remove the quotes around the regex (if there are any) */
				if (match && ptr[0] == '"' && ptr[len-1] == '"') {
					ptr[len-1] = '\0';
					ptr++;
					len--;
				}


				if (!match && !configuration.casesensitive) {
					/* URIs are case-insensitive */
					for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
				}

				if (match) wjstrncpy(uri,ptr, MAX_FILENAME); else snprintf(uri, MAX_FILENAME, "%s%s", base, ptr);
				attr = create_attribute_structure(uri);
				if (!attr) {
					fclose(file);
					return NULL;
				}

				if (match) {
					int ret;

					/* Add to linked list */
					attr->attrnext = vhost->globallocations;
					vhost->globallocations = attr;

					attr->regex = EM(malloc(sizeof(regex_t)));
					if (attr->regex == NULL) {
						fclose(file);
						return NULL;
					}

					if ((ret=regcomp(attr->regex,ptr,REG_EXTENDED | (configuration.casesensitive ? 0 : REG_ICASE)))!=0) {
						char temp[256] = "Error in attributes file: ";

						regerror(ret, attr->regex, temp+26, 256-26);
						webjames_writelog(LOGLEVEL_ATTRIBUTES, "%s", temp);
						free(attr->regex);
						attr->regex = NULL;
					}
				}
			} else if (strcmp(type,"directory") == 0 || strcmp(type,"directorymatch") == 0) {
				int match = 0;
				char buffer[MAX_FILENAME];
				int spare;

				if (section != section_NONE) continue; /* cannot have a directory inside anything else */

				if (type[9] != '\0') match = 1;

				stack(section,1);
				stack((int)vhost,1);
				stack(active,1);
				section = section_DIRECTORY;
				/* Remove the trailing > */
				ptr[len-1] = '\0';
				len--;
				/* Remove the quotes around the regex (if there are any) */
				if (match && ptr[0] == '"' && ptr[len-1] == '"') {
					ptr[len-1] = '\0';
					ptr++;
					len--;
				}

				if (!match && !configuration.casesensitive) {
					/* filenames are case-insensitive */
					for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);
				}

				if (match) {
					wjstrncpy(buffer,ptr,MAX_FILENAME);
				} else {
					snprintf(uri, MAX_FILENAME, "%s%s", base, ptr);
					if (E(xosfscontrol_canonicalise_path(uri,buffer,NULL,NULL,MAX_FILENAME,&spare))) wjstrncpy(buffer,uri,MAX_FILENAME);
				}

				attr = create_attribute_structure(buffer);
				if (!attr) {
					fclose(file);
					return NULL;
				}

				if (match) {
					int ret;

					/* Add to linked list */
					attr->attrnext = vhost->globaldirectories;
					vhost->globaldirectories = attr;

					attr->regex = EM(malloc(sizeof(regex_t)));
					if (attr->regex == NULL) {
						fclose(file);
						return NULL;
					}

					if ((ret=regcomp(attr->regex,ptr,REG_EXTENDED))!=0) {
						char temp[256] = "Error in attributes file: ";

						regerror(ret, attr->regex, temp+26, 256-26);
						webjames_writelog(LOGLEVEL_ATTRIBUTES, "%s", temp);
						free(attr->regex);
						attr->regex = NULL;
					}
				}
			} else if (strcmp(type,"files") == 0 || strcmp(type,"filesmatch") == 0) {
				struct attributes **fileslist;
				int match = 0;

				if (type[5] != '\0') match = 1;
				if (section != section_NONE && section != section_DIRECTORY) continue; /* cannot have a files inside a location or another files section */

				if (attr) {
					fileslist = &(attr->attrnext);
				} else {
					fileslist = &(vhost->globalfiles);
				}
				stack((int)attr,1);
				stack(section,1);
				stack((int)vhost,1);
				stack(active,1);
				section = section_FILES;
				/* Remove the trailing > */
				ptr[len-1] = '\0';
				len--;
				/* Remove the quotes around the regex (if there are any) */
				if (match && ptr[0] == '"' && ptr[len-1] == '"') {
					ptr[len-1] = '\0';
					ptr++;
					len--;
				}

				if (!match && !configuration.casesensitive) for (i = 0; i < len; i++)  ptr[i] = tolower(ptr[i]);

				attr = create_attribute_structure(ptr);
				if (!attr) {
					fclose(file);
					return NULL;
				}
				attr->attrnext = *fileslist;
				*fileslist = attr;

				if (match) {
					int ret;

					attr->regex = EM(malloc(sizeof(regex_t)));
					if (attr->regex == NULL) {
						fclose(file);
						return NULL;
					}
					if ((ret=regcomp(attr->regex,ptr,REG_EXTENDED))!=0) {
						char temp[256] = "Error in attributes file: ";

						regerror(ret, attr->regex, temp+26, 256-26);
						webjames_writelog(LOGLEVEL_ATTRIBUTES, "%s", temp);
						free(attr->regex);
						attr->regex = NULL;
					}
				}
			}

		/* **** or an attribute and value */
		} else {
			char *value = NULL;

			if (!active) continue;

			ptr = attribute;
			/* find end of attribute */
			while (*ptr!='\0' && !isspace(*ptr) && *ptr!='=') ptr++;

			if (*ptr) {
				*(ptr++) = '\0';

				/* remove any whitespace and '=' at start of attribute value */
				while (*ptr!='\0' && (isspace(*ptr) || *ptr=='=')) ptr++;

				if (*ptr) {
					size_t len=strlen(ptr)+1;
					value = EM(malloc(len));
					if (!value) {                  /* malloc failed */
						fclose(file);
						return NULL;
					}
					memcpy(value, ptr, len);            /* make copy of value */
				}
			}
			/* make attribute matching case insensitive */
			lower_case(attribute);

			/* Sort out attributes that may or may not be inside a section */
			if (strcmp(attribute, "action") == 0) {
				/* define a *command as a handler */
				char *command = value, *ptr = value;
				
				/* skip the action name */
				while (!isspace(*command) && *command != '\0') command++;
				if (*command != '\0') *command++ = '\0';
				/* skip whitespace */
				while (isspace(*command) && *command != '\0') command++;

				/* change handler name to lowercase */
				while (*ptr) {
					*ptr = tolower(*ptr);
					ptr++;
				}

				add_handler(value,command);
				free(value);

			} else if (strcmp(attribute, "addhandler") == 0 || strcmp(attribute, "addfiletypehandler") == 0) {
				char *filetypetext;
				int filetype = filetype_NONE, filetypehandler = 0, notfound = 0;
				struct handlerlist *newhandler;

				if (attribute[10] != '\0') filetypehandler = 1;

				filetypetext = value;
				/* skip the action name */
				while (!isspace(*filetypetext) && *filetypetext != '\0') filetypetext++;
				if (*filetypetext != '\0') *filetypetext++ = '\0';
				/* skip whitespace */
				while (isspace(*filetypetext) && *filetypetext != '\0') filetypetext++;

				/* change handler name to lowercase */
				lower_case(value);

				if (strchr(filetypetext, ' ')) webjames_writelog(LOGLEVEL_ATTRIBUTES, "Malformed handler attribute '%s' contains a space",filetypetext);

				/*check for type-map - need to record extension for content negotiation*/
				if (strcmp(value,"type-map")==0) content_recordextension(filetypetext);

				if (filetypehandler) {
					if (filetypetext[0] == '\0') {
						filetype = filetype_NONE;
					} else if (strcmp(filetypetext,"ALL") == 0) {
						filetype = filetype_ALL;
					} else {
						if (xosfscontrol_file_type_from_string(filetypetext,(bits*)&filetype)) notfound = 1;
					}
				} else if (!configuration.casesensitive) {
					/* change file extension to lowercase */
					lower_case(filetypetext);
				}

				if (notfound) {
					webjames_writelog(LOGLEVEL_ATTRIBUTES, "Filetype '%s' unknown, ignoring handler for this filetype",filetypetext);
				} else {
					newhandler = EM(malloc(sizeof(struct handlerlist)));
					if (newhandler == NULL) {
						fclose(file);
						return NULL;
					}
	
					/* fill in details */
					newhandler->filetype = filetype;
					if (filetypetext[0] == '\0') filetypetext = NULL;
					if (filetypehandler) newhandler->extension = NULL; else newhandler->extension = filetypetext;
					newhandler->handler = get_handler(value);
	
					/* add to linked list */
					if (attr) {
						newhandler->attrnext = attr->handlers;
						attr->handlers = newhandler;
					} else {
						newhandler->attrnext = vhost->globalhandlers;
						vhost->globalhandlers = newhandler;
					}
				}
				

			} else if (strcmp(attribute, "servername") == 0) {
				if (value) {
					lower_case(value);

					vhost->domain = value;
				} else {
					vhost->domain = "";
				}
				
			} else if (strcmp(attribute, "serveradmin") == 0) {

				vhost->serveradmin = value;
				
			} else if (attribute[0] == '#' || attribute[0] == '\0') {
				/* Ignore comments and blank lines */
				if (value) free(value);
				
			} else if ((strcmp(attribute, "homedir") == 0 || strcmp(attribute, "documentroot") == 0)) {
				/* define where on the harddisc the directory is stored */
				int size;
				char *buffer = value;

				/* Don't free it as it is probably pointing to the configuration structure */

				if (!configuration.casesensitive) lower_case(value);
				/* canonicalise the path, so <WebJames$Dir> etc are expanded, otherwise they can cause problems */
				if (E(xosfscontrol_canonicalise_path(value,NULL,NULL,NULL,0,&size)) == NULL) {
					buffer = EM(malloc(1-size));
					if (buffer == NULL) {
						buffer = value;
					} else if (E(xosfscontrol_canonicalise_path(value,buffer,NULL,NULL,1-size,&size)) != NULL) {
						free(buffer);
						buffer = value;
					}
				}
				if (attr) {
					attr->homedir = buffer;
					attr->defined.homedir = 1;
					if (attr->homedir) {
						/* calc how many chars at the start of the URI will be */
						/* replaced by the home-directory-path */
						attr->ignore = attr->urilen-1;
					} else {
						attr->ignore = 0;
					}
				} else {
					vhost->homedir = buffer;
				}

			} else if (attr) {
				/* **** either inside a section */

				if (strcmp(attribute, "defaultfile") == 0) {
					if (section == section_LOCATION && attr->uri[attr->urilen-1] != '/') continue;
					scan_defaultfiles_list(value,attr);
					if (value) free(value);

				} else if (strcmp(attribute, "overridename") == 0) {
					if (section != section_LOCATION || attr->regex == NULL) continue;

					if (attr->overridefilename)  free(attr->overridefilename);
					attr->defined.overridefilename = 1;
					attr->overridefilename = value;

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

				} else if (strcmp(attribute, "multiviews") == 0) {
					/* Content negotiation */
					attr->defined.multiviews = 1;
					attr->multiviews = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "setcsd") == 0) {
					/* Set CSD to dir containing CGI script */
					attr->defined.setcsd = 1;
					attr->setcsd = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "autoindex") == 0) {
					/* Autogenerate indexes for directories */
					attr->defined.autoindex = 1;
					attr->autoindex = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "noautoindex") == 0) {
					/* No autogeneration of indexes for directories */
					attr->defined.autoindex = 1;
					attr->autoindex = 0;
					if (value) free(value);

				} else if (strcmp(attribute, "mimeuseext") == 0) {
					/* Use filename extension rather than
					   filetype for generating
					   content-type header */
					attr->defined.mimeuseext = 1;
					attr->mimeuseext = 1;
					if (value) free(value);

				} else if (strcmp(attribute, "mimeusefiletype") == 0) {
					/* Use filename extension rather than
					   filetype for generating
					   content-type header */
					attr->defined.mimeuseext = 1;
					attr->mimeuseext = 0;
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
							wjstrncpy(uri, attr->uri, MAX_FILENAME);
							uri[attr->urilen-1] = '\0';         /* remove the terminating / */
							read_attributes_file(value, uri, vhost);
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
					webjames_writelog(LOGLEVEL_ATTRIBUTES, "Unknown attribute: %s", attribute);
					if (value)  free(value);
				}

			/* **** or outside all sections */
			} else {
				if (strcmp(attribute, "more-attributes") == 0) {
					if (value) read_attributes_file(value, base, vhost);
				} else {
					webjames_writelog(LOGLEVEL_ATTRIBUTES, "Unknown attribute: %s", attribute);
				}
				if (value) free(value);
			}
		}
	}
	if (attr) {
		insert_attributes(attr, vhost);
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

		error = EM(malloc(sizeof(struct errordoc)));
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
	if (attr->defined.autoindex)       conn->flags.autoindex       = attr->autoindex;
	if (attr->defined.mimeuseext)      conn->flags.mimeuseext      = attr->mimeuseext;
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
		conn->defaultfiles      = attr->defaultfiles;
		conn->defaultfilescount = attr->defaultfilescount;
	}
	if (attr->defined.cacheable)        conn->flags.cacheable  = attr->cacheable;
	if (attr->defined.moved)            conn->moved            = attr->moved;
	if (attr->defined.tempmoved)        conn->tempmoved        = attr->tempmoved;
	if (attr->defined.multiviews)       conn->flags.multiviews = attr->multiviews;
	if (attr->defined.overridefilename) conn->overridefilename = attr->overridefilename;
}

static void merge_handlers(struct handlerlist *head, struct connection *conn)
/* add all handlers in given linked list onto the end of the connection linked list */
{
	struct handlerlist *entry=head;

	if (head == NULL) return;
	/* link all the entries in the attributes list together */
	while (entry->attrnext != NULL) {
		entry->connnext = entry->attrnext;
		entry = entry->attrnext;
	}

	/* add the attributes entries to the top of the connection list */
	entry->connnext = conn->handlers;
	conn->handlers = head;
}

static void merge_attributes(struct connection *conn, struct attributes *attr) {
/* merge all attributes from attr into conn */
/* split into 3 separate functions, as otherwise cc gives an internal inconsistency when memcheck is enabled */

	merge_attributes1(conn,attr);
	merge_attributes2(conn,attr);
	merge_attributes3(conn,attr);
	merge_handlers(attr->handlers,conn);
}

void find_handler(struct connection *conn)
/* find the appropriate handler from the handlers list in conn to apply to this file */
{
	struct handlerlist *entry;
	struct handler *useifnoother = NULL;

	entry = conn->handlers;

	while (entry != NULL) {
		if (entry->filetype == filetype_ALL) {
			/* unconditional match */
			conn->handler = entry->handler;
			return;
		} else if (entry->filetype == filetype_NONE) {
			if (entry->extension == NULL) {
				/* match if nothing else explicitly matches */
				if (useifnoother == NULL) useifnoother = entry->handler;
			} else {
				char *leafname, *ext;

				leafname = strrchr(conn->filename,'.');
				if (leafname) {
					ext = strrchr(leafname,'/');
					if (entry->extension[0] == '.') {
						/* match extension */
						if (ext) {
							if (strcmp(ext+1,entry->extension+1) == 0) {
								/* extesions matched */
								conn->handler = entry->handler;
								return;
							}
						}
					} else {
						/* match whole leafname */
						if (strcmp(leafname+1,entry->extension) == 0) {
							/* leafname matched */
							conn->handler = entry->handler;
							return;
						}
					}
				}
			}
		} else {
			if (entry->filetype == conn->fileinfo.filetype) {
				/* filetypes matched */
				conn->handler = entry->handler;
				return;
			}
		}
		entry = entry->connnext;
	}
	/* no explicit match */
	if (useifnoother != NULL) {
		conn->handler = useifnoother;
		return;
	}
	/* no match, so use default handler */
	conn->handler = get_handler("static-content");
}

static void check_regex(struct attributes *attr, char *match, struct connection *conn)
{
	regmatch_t pmatch[MAX_REGEX_MATCHES];

	while (attr != NULL) {
		if (attr->regex) {
			switch (regexec(attr->regex,match,MAX_REGEX_MATCHES,pmatch,0)) {
				case REG_OKAY:
					if (conn->regexmatch == NULL) conn->regexmatch = EM(malloc(sizeof(regmatch_t) * MAX_REGEX_MATCHES));
					if (conn->regexmatch) memmove(conn->regexmatch, pmatch, sizeof(regmatch_t) * MAX_REGEX_MATCHES);
					/* Use memmove rather than memcpy to avoid a bug in Norcroft 5.54 */
					merge_attributes(conn,attr);
					break;
				case REG_NOMATCH:
					break;
				/*default: use regerror? */
			}
		} else if (strcmp(attr->uri,match) == 0) {
			merge_attributes(conn,attr);
		}
		attr = attr->attrnext;
	}
}

/* Set the vhost field in the conn struct to the appropriate vhost struct */
/* Also sets default homedir */
void get_vhost(struct connection *conn)
{
	struct vhostdetails *vhost = vhostlist;

	if (conn->host) {
		while (vhost) {
			/* This would be quicker with a hash table or something */
			if (vhost->domain && strcmp(vhost->domain, conn->host) == 0) {
				conn->vhost = vhost;
				conn->homedir = vhost->homedir;
				return;
			}
			vhost = vhost->next;
		}
	}
	/* No vhost matched, so use the first in the list */
	conn->vhost = vhostlist;
	conn->homedir = vhostlist->homedir;
}

/* merge all attributes-structures that matches the directory */
/* dir              pointer to the directory to match */
/* conn             structure to fill in */
void get_attributes(char *uri, struct connection *conn)
{
	int found;
	char buffer[MAX_FILENAME], path[MAX_FILENAME], *ptr, splitchar, *leafname, leafnamebuffer[MAX_FILENAME];
	int len, last, first, key;
	struct attributes *filesattrstart = NULL, *filesattrend = NULL;
	struct vhostdetails *vhost;

	if (conn->vhost == NULL) get_vhost(conn); /* Shouldn't happen */
	vhost = conn->vhost;

	if (uri[0] != '/') {
		/* must be a directory */

		/* remove any terminating '.' */
		wjstrncpy(path,uri,MAX_FILENAME);
		len = strlen(path);
		if (path[len-1] == '.') {
			/* There isn't any leafname */
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
		if (E(xosfscontrol_canonicalise_path(path,buffer,NULL,NULL,MAX_FILENAME,&len))) wjstrncpy(buffer,path,MAX_FILENAME);
		splitchar = '.';
		if (leafname) {
			/* There isn't a leafname */
			leafname = NULL;
		} else {
			/* Find the leafname */
			leafname = strrchr(buffer,splitchar);
			if (leafname) {
				/* Convert leafname to unix style */
				/* Convieniently uri_to_filename works both ways */
				if (uri_to_filename(leafname+1,leafnamebuffer,0)) {
					/* Invalid characters in leafname */
					leafname = NULL;
				} else {
					leafname = leafnamebuffer;
				}
			}
		}

	} else {
		/* It is actually a URI */
		wjstrncpy(buffer,uri,MAX_FILENAME);
		splitchar = '/';
		leafname = NULL;

		/* Add any global handlers to the beginning of the list */
		merge_handlers(vhost->globalhandlers,conn);
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
		memcpy(path, buffer, ptr - buffer);  /* copy path upto the '.' just found */
		if (uri[0] == '/' && *ptr == '/') {
			path[ptr - buffer] = '/';
			path[ptr - buffer + 1] = '\0';
		} else {
			path[ptr - buffer] = '\0';
		}

		found = 0;

		key = generate_key(path,vhost->hashsize);
		while (vhost->hash[key].uri && !found) {
			if (strcmp(vhost->hash[key].uri,path) == 0) {
				found = 1;
				merge_attributes(conn,vhost->hash[key].attr);
				if (leafname) {
					/* Add attr to end of linked list */
					if (filesattrend == NULL) {
						filesattrstart = vhost->hash[key].attr;
					} else {
						filesattrend->connnext = vhost->hash[key].attr;
					}
					vhost->hash[key].attr->connnext = NULL;
					filesattrend = vhost->hash[key].attr;
				}
			}
			key++;
			if (key>=vhost->hashsize) key = 0;
		}

		if (!found && uri[0] != '/' && *configuration.htaccessfile && strchr(path,'$')) {
			struct attributes *newattr;
			char htaccessfile[MAX_FILENAME];

			snprintf(htaccessfile,MAX_FILENAME,"%s.%s",path,configuration.htaccessfile);
			newattr=read_attributes_file(htaccessfile, path, vhost);
			if (newattr) {
				merge_attributes(conn, newattr);
			} else {
				/* an error occoured (probably couldn't find the file), so create a blank structure for this dir */
				newattr=create_attribute_structure(path);
				if (newattr) insert_attributes(newattr, vhost);
			}
		}

	} while (!last);

	if (uri[0] == '/') {
		/* Check to see if the uri matches any <locationmatch> sections */
		check_regex(vhost->globallocations,buffer,conn);
	} else {
		/* Check to see if the filename matches any <directorymatch> sections */
		check_regex(vhost->globaldirectories,buffer,conn);
	
		if (leafname) {
			/* Check to see if the leafname matches any global <files> sections */
			check_regex(vhost->globalfiles,leafname,conn);

			/* Check each attributes section in the linked list created whilst scanning through the directories */
			while (filesattrstart != NULL) {
				check_regex(filesattrstart,leafname,conn);
				filesattrstart = filesattrstart->connnext;
			}
		}

		if (conn->attrflags.hidden)  conn->attrflags.accessallowed = 0;
	}


}

/* reads the attributes file */
/* filename         pointer to the name of the attributes file */
void init_attributes(char *filename)
{
	/* Reset stack */
	stack(section_NONE,-1);

	vhostlist = &defaultvhost;
	/* Initialise default vhost */
	if (init_vhost(vhostlist) == NULL) return;

	read_attributes_file(filename, "", vhostlist);   /* read attributes in to the hash table */
}

