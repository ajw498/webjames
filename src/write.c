/*
	$Id$
	Get attributes for each request the send the file
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/osfile.h"
#include "oslib/osgbpb.h"
#include "oslib/osfscontrol.h"
#include "oslib/mimemap.h"

#include "webjames.h"
#include "wjstring.h"
#include "cache.h"
#include "ip.h"
#include "datetime.h"
#include "openclose.h"
#include "stat.h"
#include "attributes.h"
#include "report.h"
#include "write.h"
#include "handler.h"
#include "content.h"


void pollwrite(int cn)
{
	/* attempt to write a chunk of data from the file (bandwidth-limited) */
	/* close the connection if EOF is reached */
	int maxbytes=0;

	/* limit bandwidth */
	if (serverinfo.slowdown) {
		int maxbps, secs, written;
		written = statistics.written + statistics.written2;
		secs = (statistics.currenttime - statistics.time)/100 + 60;
		/* calculate the max bandwidth for the immediate future */
		maxbps = configuration.bandwidth - 60*written/secs;
		if (maxbps < 0)  return;
		maxbps = maxbps/serverinfo.writecount;  /* spread even across all connections */
		maxbytes = serverinfo.slowdown * maxbps/100;
		if (maxbytes < 100)  return;
	}

	handler_poll(connections[cn],maxbytes);
}


void select_writing(int cn)
{
	/* mark a connection as writing */
	ip_fd_set(serverinfo.select_write, connections[cn]->socket);
	serverinfo.writecount++;
	connections[cn]->status = WJ_STATUS_WRITING;

}

int uri_to_filename(char *uri, char *filename, int stripextension)
{
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

static void copyandsubst(char *dest, char *src, int len, char *match, regmatch_t *pmatch)
{
	while (len > 0 && *src) {
		if (*src == '\\') {
			src++;
			if (*src >= '0' && *src <= '9') {
				if (pmatch[*src - '0'].rm_so != -1) {
					regoff_t i;

					for (i = pmatch[*src - '0'].rm_so; i < pmatch[*src - '0'].rm_eo && len > 0; i++) {
						*dest++ = match[i];
						len--;
					}
				}
				src++;
			} else {
				if (*src) {
					*dest++ = *src++;
					len--;
				}
			}
		} else {
			*dest++ = *src++;
			len--;
		}
	}
	dest[len == 0 ? -1 : 0] = '\0';
}


void send_file(struct connection *conn)
{
	/* parse the header, find file, do the job etc. */
	int len;
	char *ptr, *name;
	char buffer[MAX_FILENAME];

	/* select the socket for writing */
	select_writing(conn->index);

	/* split the request in filename and args (seperated by a ?) */
	conn->args = strchr(conn->uri, '?');
	if (conn->args)  *(conn->args)++ = '\0';

	/* uri MUST start with a / */
	if (conn->uri[0] != '/') {
		report_badrequest(conn, "uri must start with a /");
		return;
	}

	get_vhost(conn);
	
	get_attributes(conn->uri, conn);

	/* RISC OS filename limit - really should be removed... */
	len = strlen(conn->uri);
	if (len + strlen(conn->homedir) > 240) {
		report_badrequest(conn, "filename too long");
		return;
	}

	webjames_writelog(LOGLEVEL_REQUEST, "URI %s", conn->uri);

	/* build RISCOS filename */
	name = conn->filename;
	copyandsubst(name, conn->homedir, MAX_FILENAME, conn->uri, conn->regexmatch);
	len = strlen(name);
	name += len;
	ptr = conn->uri + conn->homedirignore;
	if (conn->overridefilename) ptr = conn->overridefilename;

	copyandsubst(buffer, ptr, MAX_FILENAME - len, conn->uri, conn->regexmatch);

	/* append requested URI, with . and / switched */
	if (uri_to_filename(buffer, name, conn->flags.stripextensions)) {
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
		char testfile[MAX_FILENAME];
		int i;

		wjstrncpy(testfile,conn->filename,MAX_FILENAME);
		/* Remove the trailling dot so the filename points to the
		   directory if no index file is found */
		conn->filename[len-1] = '\0';
		for(i=0; i<conn->defaultfilescount; i++) {
			int type, size;

			uri_to_filename(conn->defaultfiles[i], testfile+len, conn->flags.stripextensions);
			type = get_file_info(testfile,NULL,NULL,NULL,&size,1);
			if (type != FILE_DOESNT_EXIST) {
				wjstrncpy(conn->filename,testfile,MAX_FILENAME);
				break;
			}
		}
	}


	/* check if the file has been moved */
	if (conn->moved)
		report_moved(conn, conn->moved);
	else if (conn->tempmoved)
		report_movedtemporarily(conn, conn->tempmoved);

	/* if if the file is password-protected */
	conn->pwd = check_access(conn, conn->authorization);
	if (conn->pwd == ACCESS_FAILED)  return;

	/* Content negotiation */
	if (content_negotiate(conn)) return;

	/* check if object exist and get the filetype/mimetype at the same time */
	conn->fileinfo.filetype = get_file_info(conn->filename, conn->fileinfo.mimetype, &conn->fileinfo.date, NULL, &conn->fileinfo.size,1);
	len = strlen(conn->uri);
	if (conn->fileinfo.filetype == FILE_DOESNT_EXIST) {
		report_notfound(conn);
		return;
	} else if (conn->fileinfo.filetype == OBJECT_IS_DIRECTORY) {
		if (conn->uri[len-1] != '/') {
			char newurl[MAX_FILENAME];
			snprintf(newurl, MAX_FILENAME,"%s/", conn->uri);
			report_moved(conn, newurl);
			return;
		}
		if (conn->flags.autoindex) {
			conn->handler = get_handler("autoindex");
		} else {
			report_notfound(conn);
			return;
		}
	} else if (conn->fileinfo.filetype == FILE_LOCKED) {
		report_forbidden(conn);
		return;
	} else if (conn->fileinfo.filetype == FILE_ERROR) {
		report_badrequest(conn, "error occured when reading file info");
		return;
	} else if (conn->uri[len-1] == '/') {
		/* A file with a trailing / is not allowed */
		report_notfound(conn);
		return;
	} else if (conn->fileinfo.filetype >= FILE_NO_MIMETYPE) {
		wjstrncpy(conn->fileinfo.mimetype, "text/plain", MAX_MIMETYPE);
		conn->fileinfo.filetype-=FILE_NO_MIMETYPE;
	}

	/*must be a real file if we get here */

	/* check if it is a cgi-script */
	if (conn->handler == NULL && conn->flags.is_cgi) {
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
		wjstrncpy(conn->fileinfo.mimetype, conn->cache->mimetype,MAX_MIMETYPE);
		conn->fileinfo.size = conn->cache->size;
		conn->fileinfo.date = conn->cache->date;
	}

	handler_start(conn);
}



int check_access(struct connection *conn, char *authorization)
{
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
	} else {
		webjames_writelog(LOGLEVEL_ALWAYS, "ERR the password file %s wouldn't open", line);
	}
	return ACCESS_FAILED;
}

int check_case(char *filename)
{
	/* Check that the given filename matches (case sensitive) with an actual file */
	/* It is a bit inefficient, but the only way I can think of */
	/* If only osfscontrol_canonicalise_path returned the correct case... */
	int count, more, found;
	int end;
	char leafname[MAX_FILENAME], buffer[MAX_FILENAME];

	end=strlen(filename);
	while (1) {
		/* Get the leafname and dirname */
		while (filename[end]!='.' && end>0) end--;
		if (end <= 0) return 0;
		wjstrncpy(leafname,filename+end+1,MAX_FILENAME);
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

int get_file_info(char *filename, char *mimetype, struct tm *date, os_date_and_time *utcdate, int *size, int checkcase)
{
	/* return filetype or error-code, fill in date (secs since 1990) and mimetype */
	os_date_and_time utc;
	char typename[MAX_MIMETYPE];
	char buffer[MAX_FILENAME];
	fileswitch_object_type objtype;
	bits load, exec;
	int filetype;
	fileswitch_attr attr;

	if (E(xosfile_read_no_path(filename, &objtype, &load, &exec, size,&attr)))  return FILE_ERROR;
	if (objtype == fileswitch_NOT_FOUND)  return FILE_DOESNT_EXIST;

	if (checkcase && configuration.casesensitive) {
		if (E(xosfscontrol_canonicalise_path(filename,buffer,0,0,MAX_FILENAME,NULL))) return FILE_ERROR;
		if (check_case(buffer) == 0) return FILE_DOESNT_EXIST;
	}

	if (objtype == fileswitch_IS_DIR) return OBJECT_IS_DIRECTORY;
	if (!(attr &1))  return FILE_LOCKED;
	if ((load & 0xFFF00000) != 0xFFF00000)  return FILE_NO_MIMETYPE;
	filetype=(load & 0x000FFF00) >> 8;

	if (objtype == fileswitch_IS_IMAGE) {
		int i;
		for (i=0;i<configuration.numimagedirs;i++) {
			if (configuration.imagedirs[i]==filetype_ALL || configuration.imagedirs[i]==filetype) return OBJECT_IS_DIRECTORY;
		}
	}

	utc[4] = load &255;
	utc[3] = (exec>>24) &255;
	utc[2] = (exec>>16) &255;
	utc[1] = (exec>>8) &255;
	utc[0] = exec &255;
	if (utcdate) memcpy(utcdate,&utc,sizeof(os_date_and_time));
	if (date) utc_to_time(&utc, date);
	
	if (mimetype) {
		if (E(xmimemaptranslate_filetype_to_mime_type(filetype, typename))) return FILE_NO_MIMETYPE + filetype;
		wjstrncpy(mimetype, typename, MAX_MIMETYPE);
	}
	return filetype;
}
