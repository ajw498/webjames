/*
	$Id: serverparsed.c,v 1.7 2001/08/31 10:48:47 AJW Exp $
	Support for Server Side Includes (SSI)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "kernel.h"

#include "oslib/osfile.h"

#include "regex/regex.h"

#include "webjames.h"
#include "ip.h"
#include "openclose.h"
#include "write.h"
#include "stat.h"
#include "datetime.h"
#include "attributes.h"
#include "report.h"
#include "cache.h"
#include "handler.h"
#include "staticcontent.h"
#include "cgiscript.h"
#include "serverparsed.h"

#define MAX_ARGS 4
/*maximum number of arguments to a directive*/

#define OUTPUT_BUFFERSIZE 256
/*size of buffer for output - should be more efficient than outputting each byte separately to the socket*/

/*There should be no globals in this file as the handler must be re-entrant to cope with SSI files #including other SSI files*/

enum serverparsed_status {
	status_BODY, /*In the body, not a command*/
	status_OPEN1, /*we have reached a "<"*/
	status_OPEN2, /*we have reached "<!"*/
	status_OPEN3, /*we have reached "<!-"*/
	status_OPEN4, /*we have reached "<!--"*/
	status_COMMAND, /*we have reached "<!--#"*/
	status_ARGS, /*we have reached the command arguments*/
	status_QUOTEDARGS,
	status_CLOSE1, /*we have reached "-"*/
	status_CLOSE2  /*we have reached "--"*/
};

typedef struct serverparsedinfo {
	char *errmsg;
	int abbrev;
	char *timefmt;
	int output; /*whether normal body text of the page should be output (used by #if etc)*/
	int conditionmet; /*whether any if the if/elif conditions have been met yet*/
	char command[256], args[256];
	int commandlength,argslength;
	enum serverparsed_status status;
	struct connection *child; /*set if we should wait for annother connection to finish first before continuing*/
} serverparsedinfo;

void serverparsed_start(struct connection *conn)
/*start the handler*/
{
	struct serverparsedinfo *info;
	char *leafname;

	info=malloc(sizeof(struct serverparsedinfo));
	conn->handlerinfo=info;
	if (info==NULL) {
		report_servererr(conn,"Out of memory");
		return;
	}
	info->status=status_BODY;
	conn->fileused = 0;
	info->timefmt=info->errmsg=NULL;
	info->abbrev=0;
	info->output=1;
	info->conditionmet=1;
	info->child=NULL;
	if (conn->cache) {
		conn->filebuffer = conn->cache->buffer;
		conn->fileinfo.size = conn->cache->size;
		conn->flags.releasefilebuffer = 0;
		conn->file = NULL;
		conn->cache->accesses++;

	} else {
		FILE *handle;

		handle = fopen(conn->filename, "rb");
		if (!handle) {
			report_notfound(conn);
			return;
		}
		/* attempt to get a read-ahead buffer for the file */
		/* notice: things will still work if malloc fails */
		conn->filebuffer = malloc(configuration.readaheadbuffer*1024);
		conn->flags.releasefilebuffer = 1;
		conn->leftinbuffer = 0;
		conn->file = handle;
	}

	if (conn->flags.outputheaders && conn->httpmajor >= 1) {
		int i;
		time_t now;
		char rfcnow[50];

		/* write header */
		webjames_writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
		/* we can't give a content length as we don't know it until the entire doc has been parsed*/
		sprintf(temp, "Content-Type: %s\r\n", conn->fileinfo.mimetype);
		webjames_writestring(conn->socket, temp);
		time(&now);
		time_to_rfc(localtime(&now),rfcnow);
		sprintf(temp, "Date: %s\r\n", rfcnow);
		webjames_writestring(conn->socket, temp);
		if (conn->vary[0]) {
			sprintf(temp, "Vary:%s\r\n", conn->vary);
			webjames_writestring(conn->socket, temp);
		}
		for (i = 0; i < configuration.xheaders; i++) {
			webjames_writestring(conn->socket, configuration.xheader[i]);
			webjames_writestring(conn->socket, "\r\n");
		}
		sprintf(temp, "Server: %s\r\n\r\n", configuration.server);
		webjames_writestring(conn->socket, temp);
	}

	/*environment vars are the same as for a CGI script, with a few additions*/
	cgiscript_setvars(conn);
	leafname=strrchr(conn->filename,'.');
	if (leafname) {
		strcpy(temp,leafname+1);
		while ((leafname=strchr(temp,'/'))!=NULL) *leafname='.';
		set_var_val("DOCUMENT_NAME",temp);
	}
	set_var_val("DOCUMENT_URI",conn->uri);

}

static void serverparsed_writeerror(struct connection *conn,char *msg)
/*write an error to the socket, using the configured error message if there is one*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;

	if (info->errmsg) {
		webjames_writestring(conn->socket,info->errmsg);
	} else {
		webjames_writestring(conn->socket,msg);
	}
}

static char *serverparsed_reltofilename(char *base,char *rel)
/*convert a unix style filename relative to base to a full RISC OS filename*/
{
	static char filename[256];
	char *p,*q=rel;

	strcpy(filename,base);
	p=strrchr(filename,'.');
	if (p++) {
		while (*q) {
			switch (*q) {
				case '/':
					*p++='.';
					q++;
					break;
				case '.':
					if (q==rel || q[-1]=='/') {
						if (q[1]=='.' && q[2]=='/') { /*support ../ and /../ */
							*p++='^';
							*p++='.';
							q+=3;
						} else if (q[1]=='/') { /*support ./ and /./ */
							q+=2;
						} else {
							*p++='/';
							q++;
						}
					} else {
						*p++='/';
						q++;
					}
					break;
				default:
					*p++=*q++;
			}
		}
		*p='\0';
	}
	return filename;
}

static char *serverparsed_reltouri(char *base,char *rel)
/*convert a uri relative to base to a complete uri*/
{
	static char uri[256];
	char *p,*q=rel;

	if (rel[0]=='/') {
		strcpy(uri,rel);
	} else {
		strcpy(uri,base);
		p=strrchr(uri,'/');
		if (p++) {
			while (*q) {
				switch (*q) {
					case '.':
						if (q==rel || q[-1]=='/') {
							if (q[1]=='.' && q[2]=='/') { /*support ../ and /../ */
								p--;
								while (p>uri && p[-1]!='/') p--;
								if (p==uri) *p++='/';
								q+=3;
							} else if (q[1]=='/') { /*support ./ and /./ */
								q+=2;
							} else {
								*p++='.';
								q++;
							}
						} else {
							*p++='.';
							q++;
						}
						break;
					default:
						*p++=*q++;
				}
			}
			*p='\0';
		}
	}
	return uri;
}

static char *serverparsed_getvar(struct connection *conn,char *var)
/*get an environment var, treat dates as special cases*/
{
	static char *result=NULL;
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;

	if (result) free(result);
	result=NULL;
	if (strcmp(var,"DATE_GMT")==0) {
		time_t tm1;
		struct tm *tm2;

		result=malloc(50);
		if (result) {
			time(&tm1);
			tm2=gmtime(&tm1);
			if (info->timefmt==NULL) time_to_rfc(tm2,result); else strftime(result,49,info->timefmt,tm2);
			result[49]='\0';
		}
	} else if (strcmp(var,"DATE_LOCAL")==0) {
		time_t tm1;
		struct tm *tm2;

		result=malloc(50);
		if (result) {
			time(&tm1);
			tm2=localtime(&tm1);
			if (info->timefmt==NULL) time_to_rfc(tm2,result); else strftime(result,49,info->timefmt,tm2);
			result[49]='\0';
		}
	} else if (strcmp(var,"LAST_MODIFIED")==0) {
		os_error *err;
		fileswitch_object_type objtype;
		bits load,exec,filetype;

		err=xosfile_read_stamped_no_path(conn->filename,&objtype,&load,&exec,NULL,NULL,&filetype); {int fixme;/*image files?*/}
		if (err) {
			serverparsed_writeerror(conn,err->errmess);
		} else if (objtype==fileswitch_NOT_FOUND) {
			serverparsed_writeerror(conn,"File not found");
		} else if (filetype==-1) {
			serverparsed_writeerror(conn,"File does not have a datestamp");
		} else {
			struct tm time;
			os_date_and_time utc;

			utc[4] = load &255;
			utc[3] = (exec>>24) &255;
			utc[2] = (exec>>16) &255;
			utc[1] = (exec>>8) &255;
			utc[0] = exec &255;
			utc_to_localtime(&utc,&time);

			result=malloc(50);
			if (result) {
				if (info->timefmt==NULL) time_to_rfc(&time,result); else strftime(result,49,info->timefmt,&time);
				result[49]='\0';
			}
		}
	} else {
		int size=-1;
		_kernel_swi_regs regs;

		regs.r[0]=(int)var;
		regs.r[1]=(int)result;
		regs.r[2]=size;
		regs.r[3]=0;
		regs.r[4]=0;
		_kernel_swi(0x23,&regs,&regs); /*OS_ReadVarVal to find size of buffer*/
		size=regs.r[2];

		if (size) {
			size=~size;
			result=malloc(size+1);
			if (result) {
				if (xos_read_var_val(var,result,size,0,os_VARTYPE_STRING,NULL,NULL,NULL)==NULL) result[size]='\0'; else result[0]='\0';
			}
		}
	}
	return result;
}

#define INCREASE_SIZE(inc) {\
	if ((o-expanded)+inc>len) {\
		char *newmem;\
		size_t pos=o-expanded;\
\
		len=len+(inc>128 ? 2*inc : 256);\
		newmem=realloc(expanded,len);\
		if (newmem==NULL) {\
			serverparsed_writeerror(conn,"Not enough memory");\
			expanded[0]='\0';\
			return expanded;\
		} else {\
			expanded=newmem;\
			o=expanded+pos;\
		}\
	}\
}

static char *serverparsed_expandvars(struct connection *conn,char *str)
/*expand all variables in the given string*/
{
	static char *expanded=NULL;
	static int valid=0;
	size_t len;

	if (valid && expanded) free(expanded);
	len=strlen(str)*2;
	if (len<256) len=256;
	expanded=malloc(len);
	if (expanded) {
		char *o=expanded;
		char *end, *expandedvar;
		char var[256];
		int brackets=0;
		size_t varlen;

		valid=1;
		while (*str) {
			switch (*str) {
				case '\\':
					/*allow escaped chars*/
					str++;
					if (*str) *o++=*str++;
					break;
				case '$':
					/*supports $VAR_NAME and ${VAR_NAME} */
					if (*++str=='{') {
						str++;
						brackets=1;
					}
					end=str;
					/*find end of variable name*/
					while (*end && (brackets ? *end!='}' : (isalnum(*end) || *end=='_'))) end++;
					varlen=end-str;
					if (varlen>255) {
						serverparsed_writeerror(conn,"Variable name too long");
						expanded[0]='\0';
						return expanded;
					}
					memcpy(var,str,varlen);
					var[varlen]='\0';
					str+=varlen;
					expandedvar=serverparsed_getvar(conn,var);
					if (expandedvar==NULL) expandedvar="";
					varlen=strlen(expandedvar);
					INCREASE_SIZE(varlen);
					memcpy(o,expandedvar,varlen);
					o+=varlen;
					if (*end=='}') end++;
					str=end;
					break;
				default:
					INCREASE_SIZE(1);
					*o++=*str++;
			}
		}
		INCREASE_SIZE(1);
		*o='\0';
	} else {
		valid=0;
		expanded="";
	}
	return expanded;
}

static int serverparsed_evaluateexpression(struct connection *conn,char *exp)
/*recursively evaluate the given expression, returning if it is true or false*/
{
	int brackets;
	size_t len;
	char *p;
	char op[]="\0\0";
	int ret=0; /*return value (true/false)*/
	char *lhs,*rhs;
	char *lhs0,*rhs0;

	/*remove leading and trailing spaces*/
	while (*exp && isspace(*exp)) exp++;
	len=strlen(exp);
	while (len>0 && isspace(exp[len-1])) len--;
	exp[len]='\0';
	/*if entire expression is enclosed in brackets then remove them*/
	if (exp[0]=='(' && exp[len-1]==')') {
		exp++[--len]='\0';
		len--;
	}
	/*remove any more leading and trailling spaces*/
	while (*exp && isspace(*exp)) exp++;
	while (len>0 && isspace(exp[len-1])) len--;
	exp[len]='\0';

	p=exp;
	brackets=0;
	/*find the first && or || operator that is not enclosed in brackets*/
	while (*p) {
		if (brackets==0) {
			if (*p=='&' || *p=='|') break;
		} else if (brackets<0) {
			serverparsed_writeerror(conn,"Mismatched brackets");
			return 0;
		}
		if (*p=='(') brackets++; else if (*p==')') brackets--;
		p++;
	}

	if (brackets<0) {
		serverparsed_writeerror(conn,"Mismatched brackets");
		return 0;
	}

	switch (*p) {
		case '&': /* && */
			*p++='\0';
			if (*p++!='&') {
				serverparsed_writeerror(conn,"Unknown operator");
				return 0;
			}
			return serverparsed_evaluateexpression(conn,exp) && serverparsed_evaluateexpression(conn,p);
			break;
		case '|': /* || */
			*p++='\0';
			if (*p++!='|') {
				serverparsed_writeerror(conn,"Unknown operator");
				return 0;
			}
			return serverparsed_evaluateexpression(conn,exp) || serverparsed_evaluateexpression(conn,p);
			break;
		default:
			/*expression contains no && or || operators, so look for the first ! operator*/
			p=exp;
			brackets=0;
			while (*p) {
				if (brackets==0) {
					if (p[0]=='!' && p[1]!='=') break;
				} else if (brackets<0) {
					serverparsed_writeerror(conn,"Mismatched brackets");
					return 0;
				}
				if (*p=='(') brackets++; else if (*p==')') brackets--;
				p++;
			}
		
			if (brackets<0) {
				serverparsed_writeerror(conn,"Mismatched brackets");
				return 0;
			}

			if (*p=='!') return !serverparsed_evaluateexpression(conn,p+1);

			/*expression contained no ! operators*/
			/*it shouldn't contain any brackets at this stage*/
			if (strchr(exp,'(') || strchr(exp,')')) {
				serverparsed_writeerror(conn,"Syntax error (unexpected brackets)");
				return 0;
			}

			/*split the string into everything to the left of an =, <=, etc and everything to the right*/
			lhs=lhs0=malloc(len+1);
			if (lhs==NULL) return 0;
			rhs=rhs0=malloc(len+1);
			if (rhs==NULL) {
				free(lhs);
				return 0;
			}
			brackets=0;
			p=lhs;
			while (*exp) {
				while (*exp && isspace(*exp)) exp++;
				if (*exp=='\'') {
					exp++;
					while (*exp && *exp!='\'') *p++=*exp++;
					if (*exp) exp++;
				} else {
					while (*exp && !(isspace(*exp) || *exp=='=' || *exp=='!' || *exp=='<' || *exp=='>')) *p++=*exp++;
				}
				while (*exp && isspace(*exp)) exp++;
				switch (*exp) {
					case '=':
						op[0]=*exp++;
						*p='\0';
						p=rhs;
						break;
					case '!':
						op[0]=*exp++;
						op[1]=*exp++;
						if (op[1]!='=') {
							serverparsed_writeerror(conn,"Syntax error");
							free(lhs);
							free(rhs);
							return 0;
						}
						*p='\0';
						p=rhs;
						break;
					case '<':
					case '>':
						op[0]=*exp++;
						if (*exp=='=') op[1]=*exp++;
						*p='\0';
						p=rhs;
						break;
					default:
						if (*exp) *p++=' ';
				}
			}
			*p='\0';
			/*lhs now contains everything upto the operator*/
			/*rhs contains everthing after the operator, or nothing if there wasn't an operator*/
			lhs=serverparsed_expandvars(conn,lhs);
			while (*lhs && isspace(*lhs)) lhs++;
			len=strlen(lhs);
			while (len>0 && isspace(lhs[len-1])) len--;
			lhs[len]='\0';
			if (op[0]) {
				rhs=serverparsed_expandvars(conn,rhs);
				while (*rhs && isspace(*rhs)) rhs++;
				len=strlen(rhs);
				while (len>0 && isspace(rhs[len-1])) len--;
				rhs[len]='\0';
				if (rhs[0]=='/' && rhs[len-1]=='/') {
					/*rhs is a regex*/
					regex_t regex;

					rhs[--len]='\0';
					if (regcomp(&regex,rhs+1,REG_EXTENDED | REG_NOSUB)) {
						serverparsed_writeerror(conn,"Syntax error in regex");
					} else {
						ret=(regexec(&regex,lhs,0,NULL,0)==REG_OKAY);
						regfree(&regex);
					}
				} else {
					int val;

					val=strcmp(lhs,rhs);
					switch (op[0]) {
						case '=':
							ret=val==0;
							break;
						case '!':
							ret=val!=0;
							break;
						case '<':
							if (op[1]=='=') ret=val<=0; else ret=val<0;
							break;
						case '>':
							if (op[1]=='=') ret=val>=0; else ret=val>0;
							break;
						default:
							ret=0;
					}
				}
			} else {
				ret=(lhs[0]!='\0');
			}
			free(lhs0);
			free(rhs0);
			return ret;
	}
	/*should never reach here*/
	return 0;
}

static void serverparsed_close(struct connection *conn,int force)
/*an aliased version of close_connection() from openclose.c*/
/*called when a child has finished, so return to parent rather than actually closing the connection*/
{
	struct connection *parent;

	parent=conn->parent;
	close_connection(conn,force,0);
	if (force) {
		parent->close(parent,force);
	} else {
		((struct serverparsedinfo *)parent->handlerinfo)->child=NULL;
		cgiscript_setvars(parent); /*some vars (eg PATH_TRANSLATED) need resetting to suitable values for the parent*/
	}
}

#define COPY(member) newconn->member=conn->member
#define STRCOPY(member) strcpy(newconn->member,conn->member)
#define MEMCOPY(member) {\
	if (conn->member==NULL) {\
		newconn->member=NULL;\
	} else {\
		size_t len;\
		len=strlen(conn->member)+1;\
		newconn->member=malloc(len);\
		if (newconn==NULL) {\
			serverparsed_writeerror(conn,"Out of memory");\
			return;\
		}\
		memcpy(newconn->member,conn->member,len);\
	}\
}

static void serverparsed_includevirtual(struct connection *conn,char *reluri)
/*create a child connection structure, fake the relevant fields then pretend it */
/* is a new connection and invoke a suitable handler (which could be serverparsed again)*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	char *uri;
	struct connection *newconn;
	size_t len;

	uri=serverparsed_reltouri(conn->uri,reluri);
	newconn=create_conn();
	if (newconn==NULL) {
		serverparsed_writeerror(conn,"Out of memory");
		return;
	}

	len=strlen(uri)+1;
	newconn->uri=malloc(len);
	if (newconn->uri==NULL) {
		serverparsed_writeerror(conn,"Out of memory");
		return;
	}
	memcpy(newconn->uri,uri,len);

	COPY(socket);
	COPY(port);
	COPY(index);
	COPY(method);
	COPY(httpminor);
	COPY(httpmajor);
	STRCOPY(host);
	COPY(ipaddr[0]);
	COPY(ipaddr[1]);
	COPY(ipaddr[2]);
	COPY(ipaddr[3]);
	MEMCOPY(accept);
	MEMCOPY(acceptlanguage);
	MEMCOPY(acceptcharset);
	MEMCOPY(acceptencoding);
	MEMCOPY(referer);
	MEMCOPY(useragent);
	MEMCOPY(authorization);
	MEMCOPY(requestline);
	MEMCOPY(cookie);
	MEMCOPY(requesturi);
	COPY(bodysize);
	MEMCOPY(body);
	COPY(headersize);
	COPY(headerallocated);
	COPY(args);
	if (conn->header==NULL) {
		newconn->header=NULL;
	} else {
		newconn->header=malloc(newconn->headersize);
		if (newconn==NULL) {
			serverparsed_writeerror(conn,"Out of memory");
			return;
		}
		memcpy(newconn->header,conn->header,newconn->headersize);
	}

	newconn->flags.cacheable=0;
	newconn->flags.outputheaders=0;
    newconn->close=serverparsed_close;
	newconn->parent=conn;
	info->child=newconn;
    send_file(newconn);
}

static char *serverparsed_virtualtofilename(struct connection *conn,char *virt)
/*convert a relative uri into a filename*/
{
	char *uri;
	struct connection *newconn;
	char *name,*ptr;
	static char filename[256];

	uri=serverparsed_reltouri(conn->uri,virt); /*convert to an absolute uri*/
	newconn=create_conn();

	get_attributes(uri,newconn);

	/* build RISCOS filename */
	name = newconn->filename;
	strcpy(name, newconn->homedir);
	name += strlen(name);
	ptr = uri + newconn->homedirignore;
	/* append URI, with . and / switched */
	if (uri_to_filename(ptr,name,conn->flags.stripextensions)) {
		serverparsed_writeerror(conn,"Filename includes illegal characters");
		close_connection(newconn,0,0);
		return NULL;
	}

	strcpy(filename,newconn->filename);
	close_connection(newconn,0,0);
	return filename;
}

static void serverparsed_fsize(struct connection *conn,char *filename)
/*output the filesize of the given file, according to the configured sizefmt*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	os_error *err;
	fileswitch_object_type objtype;
	int size;

	err=xosfile_read_no_path(filename,&objtype,NULL,NULL,&size,NULL);
	if (err) {
		serverparsed_writeerror(conn,err->errmess);
	} else if (objtype==fileswitch_NOT_FOUND) {
		serverparsed_writeerror(conn,"File not found");
	} else {
		char buf[12];

		if (info->abbrev) {
			if (size<1024) sprintf(buf,"%d bytes",size);
			else if (size<1024*10) sprintf(buf,"%.1f KB",(double)size/1024);
			else if (size<1024*1024) sprintf(buf,"%d KB",size/1024);
			else if (size<1024*1024*10) sprintf(buf,"%.1f MB",(double)size/(1024*1024));
			else sprintf(buf,"%d MB",size/(1024*1024));
		} else {
			sprintf(buf,"%d",size);
		}
		webjames_writestring(conn->socket,buf);
	}
}

static void serverparsed_flastmod(struct connection *conn,char *filename)
/*output the date/time the given file was last modified*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	os_error *err;
	fileswitch_object_type objtype;
	bits load,exec,filetype;

	err=xosfile_read_stamped_no_path(filename,&objtype,&load,&exec,NULL,NULL,&filetype);
	if (err) {
		serverparsed_writeerror(conn,err->errmess);
	} else if (objtype==fileswitch_NOT_FOUND) {
		serverparsed_writeerror(conn,"File not found");
	} else if (filetype==-1) {
		serverparsed_writeerror(conn,"File does not have a datestamp");
	} else {
		struct tm time;
		os_date_and_time utc;
		char str[50]="";

		utc[4] = load &255;
		utc[3] = (exec>>24) &255;
		utc[2] = (exec>>16) &255;
		utc[1] = (exec>>8) &255;
		utc[0] = exec &255;
		utc_to_localtime(&utc,&time);
		if (info->timefmt==NULL) time_to_rfc(&time,str); else strftime(str,49,info->timefmt,&time);
		webjames_writestring(conn->socket,str);
	}
}

static void serverparsed_execcommand(struct connection *conn,char *cmd)
/*execute a *command as if it were a CGI script*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	struct connection *newconn;
	static struct handler cmdhandler;

	newconn=create_conn();
	if (newconn==NULL) {
		serverparsed_writeerror(conn,"Out of memory");
		return;
	}

	strcpy(newconn->filename,conn->filename);
	newconn->flags.setcsd=conn->flags.setcsd;
	newconn->method=METHOD_GET;
	MEMCOPY(uri);

	newconn->socket=conn->socket;

	cmdhandler.name="exec cmd";
	cmdhandler.command=cmd;
	cmdhandler.unix=0;
	cmdhandler.cache=0;
	cmdhandler.startfn=cgiscript_start;
	cmdhandler.pollfn=staticcontent_poll;
	cmdhandler.next=NULL;

	newconn->handler = &cmdhandler;
	newconn->flags.outputheaders=0;
    newconn->close=serverparsed_close;
	newconn->parent=conn;
	info->child=newconn;

	handler_start(newconn);
}

static void serverparsed_command(struct connection *conn,char *command,char *args)
/*execute an SSI command*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	char *attrs[MAX_ARGS];
	char *vals[MAX_ARGS];
	int i=0;

	lower_case(command);

	/*split args string into individual attributes and values*/
	do {
		while (*args && isspace(*args)) args++; /*skip initial whitespace*/
		attrs[i]=args;
		while (*args && *args!='=' && !isspace(*args)) args++; /*find end of attribute*/
		if (*args) *args++='\0';
		lower_case(attrs[i]);
		while (isspace(*args) || *args=='=') args++; /*find start of value*/
		if (*args=='"') {
			/*value is quoted*/
			vals[i]=++args;
			while (*args && *args!='"') args++; /*find closing quote*/
		} else {
			vals[i]=args;
			while (*args && !isspace(*args)) args++; /*find end of value*/
		}
		if (*args) *args++='\0';
		i++;
	} while (*args && i<MAX_ARGS);
	if (i<MAX_ARGS) attrs[i]=vals[i]=NULL;

	if (strcmp(command,"if")==0) {
		if (attrs[0]==NULL || strcmp(attrs[0],"expr")!=0) {
			serverparsed_writeerror(conn,"Syntax error");
		} else {
			info->output=info->conditionmet=serverparsed_evaluateexpression(conn,vals[0]);
		}
	} else if (strcmp(command,"elif")==0) {
		if (info->conditionmet) {
			info->output=0;
		} else {
			if (attrs[0]==NULL || strcmp(attrs[0],"expr")!=0) {
				serverparsed_writeerror(conn,"Syntax error");
			} else {
				info->output=info->conditionmet=serverparsed_evaluateexpression(conn,vals[0]);
			}
		}
	} else if (strcmp(command,"else")==0) {
		if (info->conditionmet) info->output=0; else info->output=1;
	} else if (strcmp(command,"endif")==0) {
		info->output=1;
	} else {
		if (info->output) {
			/*Don't execute any commands within an #if block unless the #if condition is true*/
			if (strcmp(command,"config")==0) {
				for (i=0;i<MAX_ARGS && attrs[i];i++) {
					vals[i]=serverparsed_expandvars(conn,vals[i]);
					if (strcmp(attrs[i],"errmsg")==0) {
						size_t len=strlen(vals[i]);
						if (info->errmsg) free(info->errmsg);
						info->errmsg=malloc(len+1);
						if (info->errmsg) memcpy(info->errmsg,vals[i],len+1);
					} else if (strcmp(attrs[i],"sizefmt")==0) {
						lower_case(vals[i]);
						if (strcmp(vals[i],"bytes")==0) {
							info->abbrev=0;
						} else if (strcmp(vals[i],"abbrev")==0) {
							info->abbrev=1;
						} else {
							serverparsed_writeerror(conn,"Unknown value for sizefmt");
						}
					} else if (strcmp(attrs[i],"timefmt")==0) {
						size_t len=strlen(vals[i]);
						if (info->timefmt) free(info->timefmt);
						info->timefmt=malloc(len+1);
						if (info->timefmt) memcpy(info->timefmt,vals[i],len+1);
					} else if (attrs[i][0]=='\0') {
						/*ignore*/
					} else {
						serverparsed_writeerror(conn,"Unknown attribute to config command");
					}
				}
			} else if (strcmp(command,"echo")==0) {
				if (strcmp(attrs[0],"var")==0) {
					char *var;
		
					var=serverparsed_getvar(conn,vals[0]);
					if (var==NULL) var="(none)";
					webjames_writestring(conn->socket,var);
				} else {
					serverparsed_writeerror(conn,"Syntax error");
				}
			} else if (strcmp(command,"exec")==0) {
				vals[0]=serverparsed_expandvars(conn,vals[0]);
				if (strcmp(attrs[0],"cgi")==0) {
					serverparsed_includevirtual(conn,vals[0]);
				} else if (strcmp(attrs[0],"cmd")==0) {
					serverparsed_execcommand(conn,vals[0]);
				} else {
					serverparsed_writeerror(conn,"Unknown attribute to exec command");
				}
			} else if (strcmp(command,"fsize")==0) {
				vals[0]=serverparsed_expandvars(conn,vals[0]);
				if (strcmp(attrs[0],"file")==0) {
					char *filename;
		
					filename=serverparsed_reltofilename(conn->filename,vals[0]);
					if (filename) serverparsed_fsize(conn,filename);
				} else if (strcmp(attrs[0],"virtual")==0) {
					char *filename;

					filename=serverparsed_virtualtofilename(conn,vals[0]);
					if (filename) serverparsed_fsize(conn,filename);
				} else {
					serverparsed_writeerror(conn,"Unknown attribute to fsize command");
				}
			} else if (strcmp(command,"flastmod")==0) {
				vals[0]=serverparsed_expandvars(conn,vals[0]);
				if (strcmp(attrs[0],"file")==0) {
					char *filename;

					filename=serverparsed_reltofilename(conn->filename,vals[0]);
					serverparsed_flastmod(conn,filename);
				} else if (strcmp(attrs[0],"virtual")==0) {
					char *filename;

					filename=serverparsed_virtualtofilename(conn,vals[0]);
					if (filename) serverparsed_flastmod(conn,filename);
				} else {
					serverparsed_writeerror(conn,"Unknown attribute to flastmod command");
				}
			} else if (strcmp(command,"include")==0) {
				vals[0]=serverparsed_expandvars(conn,vals[0]);
				if (strcmp(attrs[0],"virtual")==0) {
					serverparsed_includevirtual(conn,vals[0]);
				} else if (strcmp(attrs[0],"file")==0) {
					char *filename;
					struct connection *newconn;

					filename=serverparsed_reltofilename(conn->filename,vals[0]);
					newconn=create_conn();
					if (newconn==NULL) {
						serverparsed_writeerror(conn,"Out of memory");
						return;
					}

					strcpy(newconn->filename,filename);
					newconn->method=METHOD_GET;
					MEMCOPY(uri);

					newconn->socket=conn->socket;

					newconn->handler = get_handler("static-content");
					newconn->flags.outputheaders=0;
				    newconn->close=serverparsed_close;
					newconn->parent=conn;
					info->child=newconn;

					/* check if object is cached */
					newconn->cache = get_file_through_cache(newconn);
					if (newconn->cache) {
						newconn->fileinfo.size = newconn->cache->size;
					} else {
						get_file_info(newconn->filename, NULL, NULL, &newconn->fileinfo.size,1);
						switch (newconn->fileinfo.filetype) {
							case FILE_DOESNT_EXIST:
							case OBJECT_IS_DIRECTORY:
								report_notfound(newconn);
								return;
							case FILE_LOCKED:
								report_forbidden(newconn);
								return;
							case FILE_ERROR:
								report_badrequest(newconn, "error occured when reading file info");
								return;
							default:
								break;
						}
					}
					handler_start(newconn);
				} else {
					serverparsed_writeerror(conn,"Unknown attribute to include command");
				}
			} else if (strcmp(command,"printenv")==0) {
				serverparsed_execcommand(conn,"show");
			} else if (strcmp(command,"set")==0) {
				vals[0]=serverparsed_expandvars(conn,vals[0]);
				vals[1]=serverparsed_expandvars(conn,vals[1]);
				if (attrs[0]==NULL || attrs[1]==NULL || strcmp(attrs[0],"var")!=0 || strcmp(attrs[1],"value")!=0) {
					serverparsed_writeerror(conn,"Syntax error");
				} else {
					set_var_val(vals[0],vals[1]);
				}
			}
		}
	}
}

#define WRITEBUFFER {\
/*write the contents of the output buffer to the socket*/\
	int bytes=o-outputbuffer;\
	int bytessent;\
	if (!info->output) {\
		o=outputbuffer;\
	} else if (bytes>0) {\
		int fixme;\
		bytessent = webjames_writebuffer(conn->socket, outputbuffer, bytes);\
		byteswritten+=bytessent;\
		if (bytessent<0) return bytessent; /*does this return error if it would block?*/ \
		if (bytessent==0) return byteswritten; /*would this ever happen? we would lose the contents of outputbuffer if it does*/ \
		if (bytessent<bytes) {\
			memmove(outputbuffer,outputbuffer+bytessent,bytes-bytessent);\
			o=outputbuffer+bytes-bytessent;\
		} else {\
			o=outputbuffer;\
		}\
	}\
}

static int serverparsed_parse(struct connection *conn, char *buffer, int bytesleft)
/*parse some characters from the buffer*/
/*implemented as a state machine, as there might only be part of a tag in the buffer*/
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	char *p=buffer;
	char outputbuffer[OUTPUT_BUFFERSIZE];
	char *o=outputbuffer;
	char *oend=outputbuffer+OUTPUT_BUFFERSIZE-1; /*ptr to last char in buffer*/
	int byteswritten=0;
	int bytestowrite=bytesleft;

	while (bytesleft>0 && info->child==NULL) {
		switch (info->status) {
			case status_BODY:
				if (*p=='<') {
					p++;
					bytesleft--;
					info->status=status_OPEN1;
				} else {
					*o++=*p++;
					bytesleft--;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN1:
				if (*p=='!') {
					p++;
					bytesleft--;
					info->status=status_OPEN2;
				} else {
					*o++='<';
					info->status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN2:
				if (*p=='-') {
					p++;
					bytesleft--;
					info->status=status_OPEN3;
				} else {
					*o++='<';
					if (o>oend) WRITEBUFFER;
					*o++='!';
					info->status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN3:
				if (*p=='-') {
					p++;
					bytesleft--;
					info->status=status_OPEN4;
				} else {
					*o++='<';
					if (o>oend) WRITEBUFFER;
					*o++='!';
					if (o>oend) WRITEBUFFER;
					*o++='-';
					info->status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN4:
				if (*p=='#') {
					p++;
					bytesleft--;
					info->status=status_COMMAND;
					info->commandlength=0;
					info->argslength=0;
				} else {
					info->status=status_BODY;
					*o++='<';
					if (o>oend) WRITEBUFFER;
					*o++='!';
					if (o>oend) WRITEBUFFER;
					*o++='-';
					if (o>oend) WRITEBUFFER;
					*o++='-';
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_COMMAND:
				if (isspace(*p)) {
					info->command[info->commandlength]='\0';
					info->status=status_ARGS;
				} else {
					info->command[info->commandlength++]=*p++;
					/*check commandlength is within limits*/
					bytesleft--;
				}
				break;
			case status_ARGS:
				if (*p=='-') {
					p++;
					bytesleft--;
					info->args[info->argslength]='\0';
					info->status=status_CLOSE1;
				} else if (*p=='"') {
					info->status=status_QUOTEDARGS;
					info->args[info->argslength++]=*p++;
					bytesleft--;
				} else {
					info->args[info->argslength++]=*p++;
					if (info->argslength>255) info->argslength=255; /*ignore the end of any command that wont fit in the buffer*/
					bytesleft--;
				}
				break;
			case status_QUOTEDARGS:
				if (*p=='"') {
					info->status=status_ARGS;
					info->args[info->argslength++]=*p++;
					bytesleft--;
				} else {
					info->args[info->argslength++]=*p++;
					if (info->argslength>255) info->argslength=255; /*ignore any args that wont fit in the buffer*/
					bytesleft--;
				}
				break;
			case status_CLOSE1:
				if (*p=='-') {
					p++;
					bytesleft--;
					info->status=status_CLOSE2;
				} else {
					info->args[info->argslength++]='-';
					if (info->argslength>255) info->argslength=255;
					info->status=status_ARGS;
				}
				break;
			case status_CLOSE2:
				if (*p=='>') {
					p++;
					bytesleft--;
					info->status=status_BODY;
					WRITEBUFFER; /*ensure that the buffer is empty before executing the command, as the commands output directly to the socket*/
					serverparsed_command(conn,info->command,info->args);
				} else {
					info->status=status_ARGS;
					info->args[info->argslength++]='-';
					if (info->argslength>254) info->argslength=254;
					info->args[info->argslength++]='-';
				}
				break;
		}
	}
	WRITEBUFFER;
	return bytestowrite-bytesleft;
}

static void serverparsed_tidyup(void)
{
	cgiscript_removevars();
	remove_var("DOCUMENT_NAME");
	remove_var("DOCUMENT_URI");
}

int serverparsed_poll(struct connection *conn,int maxbytes)
/* attempt to write a chunk of data from the file (bandwidth-limited) */
/* close the connection if EOF is reached */
/* return the number of bytes written */
{
	struct serverparsedinfo *info=(struct serverparsedinfo *)conn->handlerinfo;
	int bytes = 0;
	char temp[HTTPBUFFERSIZE];

	if (info->child) return handler_poll(info->child,maxbytes);

	if (conn->fileused < conn->fileinfo.size) {
		/* send a bit more of the file or buffered data */

		/* find max no. of bytes to send */
		bytes = conn->fileinfo.size - conn->fileused;
		if (conn->file) {
			if (conn->filebuffer) {
				if (bytes > configuration.readaheadbuffer*1024)  bytes = configuration.readaheadbuffer*1024;
			} else {
				if (bytes > HTTPBUFFERSIZE)  bytes = HTTPBUFFERSIZE;
			}
		}

		if (maxbytes > 0 && bytes > maxbytes)  bytes = maxbytes;

		/* read from buffer or from file */
		if (conn->file) {   /* read from file (possibly through temp buffer) */
			if (conn->filebuffer) {
				if (conn->leftinbuffer == 0) {
					conn->leftinbuffer = fread(conn->filebuffer, 1,configuration.readaheadbuffer*1024, conn->file);
					conn->positioninbuffer = 0;
				}
				if (bytes > conn->leftinbuffer)  bytes = conn->leftinbuffer;
				bytes = serverparsed_parse(conn, conn->filebuffer + conn->positioninbuffer, bytes);
				conn->positioninbuffer += bytes;
				conn->leftinbuffer -= bytes;
			} else {
				fseek(conn->file, conn->fileused, SEEK_SET);
				bytes = fread(temp, 1, bytes, conn->file);
				bytes = serverparsed_parse(conn, temp, bytes);
			}
		} else {            /* read from buffer */
			bytes = serverparsed_parse(conn, conn->filebuffer+conn->fileused, bytes);
		}

		conn->timeoflastactivity = clock();

		if (bytes < 0) {
			/* errorcode = -bytes; */
			*temp = '\0';
			switch (-bytes) {
			case IPERR_BROKENPIPE:
				webjames_writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
				break;
			}
			serverparsed_tidyup();
			conn->close(conn, 1);           /* close the connection, with force */
			return 0;
		} else {
			conn->fileused += bytes;
		}
	}


	/* if EOF reached, close */
	if (conn->fileused >= conn->fileinfo.size) {
		if (conn->parent==NULL) serverparsed_tidyup();
		conn->close(conn, 0);
	}

	return bytes;
}

