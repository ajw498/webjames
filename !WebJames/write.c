#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/osfile.h"
#include "oslib/osgbpb.h"
#include "oslib/osfscontrol.h"

#include "webjames.h"
#include "cache.h"
#include "ip.h"
#include "openclose.h"
#include "stat.h"
#include "attributes.h"
#include "report.h"
#include "write.h"
#include "mimemap.h"
#include "handler.h"


#define FILE_DOESNT_EXIST  -1
#define FILE_LOCKED        -2
#define FILE_ERROR         -3
#define OBJECT_IS_DIRECTORY -4
#define FILE_NO_MIMETYPE   0x1000

#define ACCESS_FREE        0
#define ACCESS_ALLOWED     1
#define ACCESS_FAILED      2


void pollwrite(int cn) {
/* attempt to write a chunk of data from the file (bandwidth-limited) */
/* close the connection if EOF is reached */
	int bytes, maxbytes=0;

	/* limit bandwidth */
	if (slowdown) {
		int maxbps, secs, written;
		written = statistics.written + statistics.written2;
		secs = (statistics.currenttime - statistics.time)/100 + 60;
		/* calculate the max bandwidth for the immediate future */
		maxbps = configuration.bandwidth - 60*written/secs;
		if (maxbps < 0)  return;
		maxbps = maxbps/writecount;  /* spread even across all connections */
		maxbytes = slowdown * maxbps/100;
		if (maxbytes < 100)  return;
	}

	bytes = handler_poll(connections[cn],maxbytes);

	if (bytes > 0) {
		/* update statistics */
		statistics.written += bytes;
	}
}


void select_writing(int cn) {
/* mark a connection as writing */
	fd_set(select_write, connections[cn]->socket);
	writecount++;
	connections[cn]->status = WJ_STATUS_WRITING;

}

int uri_to_filename(char *uri, char *filename, int stripextension) {
/* swap '.' and '/' */
/* returns non zero if illegal chars are present */
/* optionally removes the leafname extension */
	char *lastdot = NULL;
	while (*uri) {
		if (*uri == '/') {
			*filename++ = '.';
			lastdot = NULL;
		} else if (*uri == '.') {
			lastdot = filename;
			*filename++ = '/';
		} else if ((*uri == '%') || (*uri == '*') || (*uri == '^') || (*uri == '&')
					|| (*uri == '#') || (*uri == ':') || (*uri == '<') || (*uri == '>')
					|| (*uri == '$') || (*uri == '@') || (*uri == '\\') ) {
			return 1;
		} else {
			*filename++ = *uri;
		}
		uri++;
	}
	*filename = '\0';

	if (stripextension && lastdot) *lastdot = '\0';

	return 0;
}

void send_file(struct connection *conn) {
/* parse the header, find file, do the job etc. */
	int len;
	char *ptr, *name;

	/* select the socket for writing */
	select_writing(conn->index);

	/* split the request in filename and args (seperated by a ?) */
	conn->args = strchr(conn->uri, '?');
	if (conn->args)  *(conn->args)++ = '\0';

	get_attributes(conn->uri, conn);

	/* uri MUST start with a / */
	if (conn->uri[0] != '/') {
		report_badrequest(conn, "uri must start with a /");
		return;
	}

	/* RISC OS filename limit - really should be removed... */
	len = strlen(conn->uri);
	if (len + strlen(conn->homedir) > 240) {
		report_badrequest(conn, "filename too long");
		return;
	}

#ifdef LOG
	sprintf(temp, "URI %s", conn->uri);
	writelog(LOGLEVEL_REQUEST, temp);
#endif


	/* build RISCOS filename */
	name = conn->filename;
	strcpy(name, conn->homedir);
	name += strlen(name);
	ptr = conn->uri + conn->homedirignore;
	/* append requested URI, with . and / switched */
	if (uri_to_filename(ptr,name,conn->flags.stripextensions)) {
		report_badrequest(conn, "filename includes illegal characters");
		return;
	}

	get_attributes(conn->filename,conn);

	if (!(conn->attrflags.accessallowed)) {
		report_notfound(conn);
		return;
	}

	/* if requesting a directory (ie. the uri ends with a /) use index.html */
	len = strlen(conn->filename);
	if (conn->filename[len-1] == '.') {
		char testfile[256];
		int i;

		strcpy(testfile,conn->filename);
		uri_to_filename("index.html", testfile+len, conn->flags.stripextensions); /* incase the list is empty */
		for(i=0; i<conn->defaultfilescount; i++) {
			int type, size;

			uri_to_filename(conn->defaultfiles[i], testfile+len, conn->flags.stripextensions);
			type = get_file_info(testfile,NULL,NULL,&size,1);
			if (type != FILE_DOESNT_EXIST) break;
		}
		strcpy(conn->filename,testfile);
	}


	/* check if the file has been moved */
	if (conn->moved)
		report_moved(conn, conn->moved);
	else if (conn->tempmoved)
		report_movedtemporarily(conn, conn->tempmoved);

	/* if if the file is password-protected */
	conn->pwd = check_access(conn, conn->authorization);
	if (conn->pwd == ACCESS_FAILED)  return;

	/* check if object exist and get the filetype/mimetype at the same time */
	conn->fileinfo.filetype = get_file_info(conn->filename, conn->fileinfo.mimetype, &conn->fileinfo.date, &conn->fileinfo.size,1);
	if (conn->fileinfo.filetype == FILE_DOESNT_EXIST) {
		report_notfound(conn);
		return;
	} else if (conn->fileinfo.filetype == OBJECT_IS_DIRECTORY) {
		len = strlen(conn->uri);
		if (conn->uri[len-1] != '/') {
			char newurl[512];
			sprintf(newurl, "%s/", conn->uri);
			report_moved(conn, newurl);
		} else {
			report_notfound(conn);
		}
		return;
	} else if (conn->fileinfo.filetype == FILE_LOCKED) {
		report_forbidden(conn);
		return;
	} else if (conn->fileinfo.filetype == FILE_ERROR) {
		report_badrequest(conn, "error occured when reading file info");
		return;
	} else if (conn->fileinfo.filetype >= FILE_NO_MIMETYPE) {
		strcpy(conn->fileinfo.mimetype, "text/plain");
		conn->fileinfo.filetype-=FILE_NO_MIMETYPE;
	}

	/*must be a real file if we get here */

	/* check if it is a cgi-script */
	if (conn->flags.is_cgi) {
		/* this is only for backwards compatibility */
		/* Add[Filetype]Handler should be used in preference to is-cgi */
		int forbidden, allowed;

		forbidden = 0;
		allowed = 1;


		if (conn->forbiddenfiletypescount) {
			int i;

			for (i = 0; i < conn->forbiddenfiletypescount; i++) {
				if (conn->forbiddenfiletypes[i] == conn->fileinfo.filetype) forbidden = 1;
			}
		}

		if (!forbidden) {
			if (conn->allowedfiletypescount) {
				int i;

				allowed = 0;
				for (i = 0; i < conn->allowedfiletypescount; i++) {
					if (conn->allowedfiletypes[i] == conn->fileinfo.filetype) allowed = 1;
				}
			}
		}

		if (!forbidden && allowed) {
			if (conn->cgi_api == CGI_API_REDIRECT) {
				conn->handler = get_handler("cgi-script");
			} else if (conn->cgi_api == CGI_API_WEBJAMES) {
				conn->handler = get_handler("webjames-script");
			}
		}
	}

	if (conn->handler == NULL) {
		/* work out what handler to use */
		find_handler(conn);
	}

	if (conn->handler == NULL) {
		report_nocontent(conn);
		return;
	}
	/* don't cache CGI scripts if the handler can't cope with it */
	if (conn->handler->cache == 0) conn->flags.cacheable = 0;

	/* check if object is cached */
	if (conn->flags.cacheable) {
		conn->cache = get_file_through_cache(conn);
	} else {
		conn->cache = NULL;
	}

	if (conn->cache) {
		conn->fileinfo.filetype = conn->cache->filetype;
		strcpy(conn->fileinfo.mimetype, conn->cache->mimetype);
		conn->fileinfo.size = conn->cache->size;
		conn->fileinfo.date = conn->cache->date;
	}

	handler_start(conn);
}



int check_access(struct connection *conn, char *authorization) {
/* check if the file is password-protected */
/* returns: */
/*  2 if the file is password-protected and the user doesn't have access */
/*    in which case a suitable report will have been returned */
/*  1 if the file is password-protected and the user has access */
/*  0 if the file isn't password-protected */

/* conn       connections structure with uri-attributes */
/* autho..    username:password or NULL */


	FILE *file;
	int i;
	char line[256];

	/* is the URI password protected at all?? */
	if ((!conn->accessfile) && (!conn->userandpwd))  return ACCESS_FREE;

	/* the URI _is_ password-protected */

	/* no username:password is given... */
	if (!authorization) {
		report_unauthorized(conn, conn->realm);
		return ACCESS_FAILED;
	}

	/* there's only a single single username/password for this URI */
	if (conn->userandpwd) {
		if (strcmp(conn->userandpwd, authorization)) {
			report_unauthorized(conn, conn->realm);
			return ACCESS_FAILED;
		} else
			return ACCESS_ALLOWED;
	}

	/* multiple username:passwords for this URI */
	file = fopen(conn->accessfile, "r");
	if (file) {
		while (!feof(file)) {
			fgets(line, 255, file);
			i = 0;
			while (line[i] >= ' ')  i++;
			line[i] = '\0';
			if (strcmp(line, authorization) == 0) {
				fclose(file);
				return ACCESS_ALLOWED;
			}
		}
		fclose(file);
		report_unauthorized(conn, conn->realm);
	}
#ifdef LOG
	else {
		sprintf(temp, "ERR the password file %s wouldn't open", line);
		writelog(LOGLEVEL_ALWAYS, temp);
	}
#endif
	return ACCESS_FAILED;
}

static int check_case(char *filename)
/* Check that the given filename matches (case sensitive) with an actual file */
/* It is a bit inefficient, but the only way I can think of */
/* If only osfscontrol_canonicalise_path returned the correct case... */
{
	int count, more, found;
	int end;
	char leafname[256], buffer[256];

	end=strlen(filename);
	while (1) {
		/* Get the leafname and dirname */
		while (filename[end]!='.' && end>0) end--;
		if (end <= 0) return 0;
		strcpy(leafname,filename+end+1);
		filename[end] = '\0';
		/* Then enumerate the directory contents to see if there is a matching file/directory */
		found = 0;
		more = 0;
		do {
			if (xosgbpb_dir_entries(filename,(osgbpb_string_list *)buffer,1,more,256,NULL,&count,&more)) return 0;
			if (count) {
				if (strcmp(buffer,leafname) == 0) {
					found = 1;
					break;
				}
			}
		} while(more != -1);
		if (!found) return 0; /* Not found, so don't bother to check rest of pathname */
		if (filename[end-1] == '$') return 1; /* We have reached the root directory, so the filename must match exactly */
	}
}

int get_file_info(char *filename, char *mimetype, struct tm *date, int *size, int checkcase) {
/* return filetype or error-code, fill in date (secs since 1990) and mimetype */

	char utc[5], typename[128];
	char buffer[256];
	fileswitch_object_type objtype;
	bits load, exec, filetype;
	fileswitch_attr attr;

	if (xosfile_read_stamped_no_path(filename, &objtype, &load, &exec, size,&attr, &filetype))  return FILE_ERROR;
	if (objtype == 0)  return FILE_DOESNT_EXIST;

	if (checkcase && configuration.casesensitive) {
		if (xosfscontrol_canonicalise_path(filename,buffer,0,0,256,NULL)) return FILE_ERROR;
		if (check_case(buffer) == 0) return FILE_DOESNT_EXIST;
	}

	if (objtype == 2)  return OBJECT_IS_DIRECTORY;
	if (!(attr &1))  return FILE_LOCKED;
	if (filetype == -1)  return FILE_NO_MIMETYPE;

	if (date) {
		utc[4] = load &255;
		utc[3] = (exec>>24) &255;
		utc[2] = (exec>>16) &255;
		utc[1] = (exec>>8) &255;
		utc[0] = exec &255;
		utc_to_time(utc, date);
	}

	if (mimetype) {
		if (mimemap_fromfiletype(filetype, MMM_TYPE_MIME, typename) == typename)
			strcpy(mimetype, typename);
		else
			return FILE_NO_MIMETYPE + filetype;
	}
	return filetype;
}
