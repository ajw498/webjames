#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "osfile.h"
#include "osgbpb.h"
#include "osfscontrol.h"

#include "cache.h"
#include "webjames.h"
#include "cgi.h"
#include "ip.h"
#include "openclose.h"
#include "stat.h"
#include "attributes.h"
#include "report.h"
#include "write.h"
#include "mimemap.h"


int readaheadbuffer;

#define FILE_DOESNT_EXIST  -1
#define FILE_LOCKED        -2
#define FILE_NO_MIMETYPE   -3
#define FILE_ERROR         -4
#define OBJECT_IS_DIRECTORY -5

#define ACCESS_FREE        0
#define ACCESS_ALLOWED     1
#define ACCESS_FAILED      2



void pollwrite(int cn) {
/* attempt to write a chunk of data from the file (bandwidth-limited) */
/* close the connection if EOF is reached */
	struct connection *conn;
	int bytes, socket;
	char temp[HTTPBUFFERSIZE];

	conn = connections[cn];
	socket = conn->socket;

	if (conn->fileused < conn->filesize) {
		/* send a bit more of the file or buffered data */

		/* find max no. of bytes to send */
		bytes = conn->filesize - conn->fileused;
		if (conn->file) {
			if (conn->filebuffer) {
				if (bytes > readaheadbuffer*1024)  bytes = readaheadbuffer*1024;
			} else {
				if (bytes > HTTPBUFFERSIZE)  bytes = HTTPBUFFERSIZE;
			}
		}

		/* limit bandwidth */
		if (slowdown) {
			int maxbps, secs, maxbytes, written;
			written = statistics.written + statistics.written2;
			secs = (statistics.currenttime - statistics.time)/100 + 60;
			/* calculate the max bandwidth for the immediate future */
			maxbps = configuration.bandwidth - 60*written/secs;
			if (maxbps < 0)  return;
			maxbps = maxbps/writecount;  /* spread even across all connections */
			maxbytes = slowdown * maxbps/100;
			if (maxbytes < 100)  return;
			if (bytes > maxbytes)  bytes = maxbytes;
		}

		/* read from buffer or from file */
		if (conn->file) {   /* read from file (possibly through temp buffer) */
			if (conn->filebuffer) {
				/*  IDEA: if there's less than 512 bytes left in the buffer, */
				/* move those to the start of the buffer, and refill; that may */
				/* be faster than sending the 512 bytes on their own */
				if (conn->leftinbuffer == 0) {
					conn->leftinbuffer = fread(conn->filebuffer, 1,readaheadbuffer*1024, conn->file);
					conn->positioninbuffer = 0;
				}
				if (bytes > conn->leftinbuffer)  bytes = conn->leftinbuffer;
				bytes = ip_write(socket, conn->filebuffer + conn->positioninbuffer,bytes);
				conn->positioninbuffer += bytes;
				conn->leftinbuffer -= bytes;
			} else {
				fseek(conn->file, conn->fileused, SEEK_SET);
				bytes = fread(temp, 1, bytes, conn->file);
				bytes = ip_write(socket, temp, bytes);
			}
		} else {            /* read from buffer */
			bytes = ip_write(socket, conn->filebuffer+conn->fileused, bytes);
		}

		if (bytes > 0) {
			/* update statistics */
			statistics.written += bytes;
			conn->timeoflastactivity = clock();
			conn->fileused += bytes;
		} else if (bytes < 0) {
			/* errorcode = -bytes; */
			*temp = '\0';
			switch (-bytes) {
			case IPERR_BROKENPIPE:
#ifdef LOG
				writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
#endif
				break;
			}
			close(cn, 1);           /* close the connection, with force */
			return;
		}
	}

	/* if EOF reached, close */
	if (conn->fileused >= conn->filesize)   close(cn, 0);
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
	int size, type, len, i, pwd;
	char *ptr, *name, *args, file[256], mimetype[128];
	FILE *handle;
	struct tm date;
	struct cache *cacheentry;

	/* select the socket for writing */
	select_writing(conn->index);

	/* split the request in filename and args (seperated by a ?) */
	args = strchr(conn->uri, '?');
	if (args)  *args++ = '\0';

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

	/* make a copy of the uri for processing */
	strcpy(file, conn->uri);


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
	pwd = check_access(conn, conn->authorization);
	if (pwd == ACCESS_FAILED)  return;

	/* check if is it a cgi-script */
	if (conn->flags.is_cgi) {
		int forbidden, allowed;

		forbidden = 0;
		allowed = 1;
		/* check if cgi-script program exists, and has a suitable filetype */
		type = get_file_info(conn->filename,NULL,NULL,&size,1);

		if (type == FILE_DOESNT_EXIST) {
			report_notfound(conn);
			return;
		}

		if (type != OBJECT_IS_DIRECTORY) {
			if (conn->forbiddenfiletypescount) {
				int i;

				for (i = 0; i < conn->forbiddenfiletypescount; i++) {
					if (conn->forbiddenfiletypes[i] == type) forbidden = 1;
				}
			}

			if (!forbidden) {
				if (conn->allowedfiletypescount) {
					int i;

					allowed = 0;
					for (i = 0; i < conn->allowedfiletypescount; i++) {
						if (conn->allowedfiletypes[i] == type) allowed = 1;
					}
				}
			}

			if (!forbidden && allowed) {
				script_start(SCRIPT_CGI, conn, conn->filename, pwd, args);
				return;
			}
		}
	}

	/* check if object is cached */
	if (conn->flags.cacheable)
		cacheentry = get_file_through_cache(file, conn->filename);
	else
		cacheentry = NULL;

	if (cacheentry) {
		strcpy(mimetype, cacheentry->mimetype);
		size = cacheentry->size;
		date = cacheentry->date;

	} else {
		/* check if object exist and get the filetype/mimetype at the same time */
		type = get_file_info(conn->filename, mimetype, &date, &size,1);
		if (type == FILE_DOESNT_EXIST) {
			report_notfound(conn);
			return;
		} else if (type == OBJECT_IS_DIRECTORY) {
			if (file[len-1] != '/') {
				char newurl[512];
				sprintf(newurl, "%s/", file);
				report_moved(conn, newurl);
			} else {
				report_notfound(conn);
			}
			return;
		} else if (type == FILE_LOCKED) {
			report_forbidden(conn);
			return;
		} else if (type == FILE_ERROR) {
			report_badrequest(conn, "error occured when reading file info");
			return;
		} else if (type == FILE_NO_MIMETYPE)
			strcpy(mimetype, "text/plain");
	}

	if ((conn->method == METHOD_GET) || (conn->method == METHOD_PUT) || (conn->method == METHOD_DELETE)) {
		/* was there a valid If-Modified-Since field in the request?? */
		if (conn->if_modified_since.tm_year > 0) {
			if (compare_time(&date, &conn->if_modified_since) < 0) {
				report_notmodified(conn);
				return;
			}
		}
		/* if everything OK until now, prepare to send file */
		conn->fileused = 0;
		if (cacheentry) {
			conn->filebuffer = cacheentry->buffer;
			conn->filesize = cacheentry->size;
			conn->flags.releasefilebuffer = 0;
			conn->file = NULL;
			conn->cache = cacheentry;
			cacheentry->accesses++;

		} else {
			handle = fopen(conn->filename, "rb");
			if (!handle) {
				report_notfound(conn);
				return;
			}
			/* attempt to get a read-ahead buffer for the file */
			/* notice: things will still work if malloc fails */
			conn->filebuffer = malloc(readaheadbuffer*1024);
			conn->flags.releasefilebuffer = 1;
			conn->leftinbuffer = 0;
			/* set the fields in the structure, and that's it! */
			conn->file = handle;
			conn->filesize = size;
		}
	}

	if (conn->httpmajor >= 1) {
		/* write header */
		writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
		sprintf(temp, "Content-Length: %d\r\n", size);
		writestring(conn->socket, temp);
		sprintf(temp, "Content-Type: %s\r\n", mimetype);
		writestring(conn->socket, temp);
		for (i = 0; i < configuration.xheaders; i++) {
			writestring(conn->socket, configuration.xheader[i]);
			writestring(conn->socket, "\r\n");
		}
		sprintf(temp, "Server: %s\r\n\r\n", configuration.server);
		writestring(conn->socket, temp);
	}
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

	/* check if cgi-script program exists */
	if (xosfile_read_stamped_no_path(filename, &objtype, &load, &exec, size,&attr, &filetype))  return FILE_ERROR;
	if (objtype != 1 && objtype != 2)  return FILE_DOESNT_EXIST;

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
			return FILE_NO_MIMETYPE;
	}
	return filetype;
}
