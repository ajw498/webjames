#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "swis.h"

#include "regex/regex.h"

#include "webjames.h"
#include "ip.h"
#include "openclose.h"
#include "stat.h"
#include "attributes.h"
#include "report.h"
#include "cache.h"
#include "cgiscript.h"
#include "serverparsed.h"

#include "oslib/osfile.h"

#ifdef MemCheck_MEMCHECK
#include "MemCheck:MemCheck.h"
#endif

#define MAX_ARGS 10

#define OUTPUT_BUFFERSIZE 256

enum status {
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

static enum status status; /*Should be local to conn so it is reentrant*/

void serverparsed_start(struct connection *conn)
{
	char *leafname;
	
	status=status_BODY;
	conn->fileused = 0;
	conn->serverparsedinfo.timefmt=conn->serverparsedinfo.errmsg=NULL;
	conn->serverparsedinfo.abbrev=0;
	conn->serverparsedinfo.output=1;
	conn->serverparsedinfo.conditionmet=1;
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

	if (conn->httpmajor >= 1) {
		int i;
		/* write header */
		writestring(conn->socket, "HTTP/1.0 200 OK\r\n");
/*		sprintf(temp, "Content-Length: %d\r\n", conn->fileinfo.size);
		writestring(conn->socket, temp); */
		sprintf(temp, "Content-Type: %s\r\n", conn->fileinfo.mimetype);
		writestring(conn->socket, temp);
		if (conn->vary[0]) {
			sprintf(temp, "Vary:%s\r\n", conn->vary);
			writestring(conn->socket, temp);
		}
		for (i = 0; i < configuration.xheaders; i++) {
			writestring(conn->socket, configuration.xheader[i]);
			writestring(conn->socket, "\r\n");
		}
		sprintf(temp, "Server: %s\r\n\r\n", configuration.server);
		writestring(conn->socket, temp);
	}

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
{
	if (conn->serverparsedinfo.errmsg) {
		writestring(conn->socket,conn->serverparsedinfo.errmsg);
	} else {
		writestring(conn->socket,msg);
	}
}

/*don't forget to test files that are marked uncacheable*/

static char *serverparsed_reltofilename(char *base,char *q)
{
	static char filename[256];
	char *p;

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
					/*q[-1] will always be valid, even if q points to the first char of vals[0] as then it will be the terminator to args[0]*/
					if (q[-1]=='\0' || q[-1]=='/') {
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
	}
	*p='\0';
	return filename;
}

static char *serverparsed_getvar(struct connection *conn,char *var)
{
	static char *result=NULL;

	if (result) free(result);
	result=NULL;
	if (strcmp(var,"DATE_GMT")==0) {
		time_t tm1;
		struct tm *tm2;

		result=malloc(50);
		if (result) {
			time(&tm1);
			tm2=gmtime(&tm1);
			if (conn->serverparsedinfo.timefmt==NULL) time_to_rfc(tm2,result); else strftime(result,49,conn->serverparsedinfo.timefmt,tm2);
			result[49]='\0';
		}
	} else if (strcmp(var,"DATE_LOCAL")==0) {
		time_t tm1;
		struct tm *tm2;

		result=malloc(50);
		if (result) {
			time(&tm1);
			tm2=localtime(&tm1);
			if (conn->serverparsedinfo.timefmt==NULL) time_to_rfc(tm2,result); else strftime(result,49,conn->serverparsedinfo.timefmt,tm2);
			result[49]='\0';
		}
	} else if (strcmp(var,"LAST_MODIFIED")==0) {
		os_error *err;
		fileswitch_object_type objtype;
		bits load,exec,filetype;

		err=xosfile_read_stamped_no_path(conn->filename,&objtype,&load,&exec,NULL,NULL,&filetype);
		if (err) {
			serverparsed_writeerror(conn,err->errmess);
		} else if (objtype==fileswitch_NOT_FOUND) {
			serverparsed_writeerror(conn,"File not found");
		} else if (filetype==-1) {
			serverparsed_writeerror(conn,"File does not have a datestamp");
		} else {
			struct tm time;
			char utc[5];

			utc[4] = load &255;
			utc[3] = (exec>>24) &255;
			utc[2] = (exec>>16) &255;
			utc[1] = (exec>>8) &255;
			utc[0] = exec &255;
			utc_to_localtime(utc,&time);

			result=malloc(50);
			if (result) {
				if (conn->serverparsedinfo.timefmt==NULL) time_to_rfc(&time,result); else strftime(result,49,conn->serverparsedinfo.timefmt,&time);
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
					while (*end && (brackets ? *end!='}' : !isspace(*end))) end++;
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
					if (expandedvar==NULL) expandedvar="(none)";
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
{
	int brackets;
	size_t len;
	char *p;
	char op[]="\0\0";
	int ret=0; /*return value (true/false)*/
	char *lhs,*rhs;

	/*remove leading and trailling spaces*/
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
		case '&':
			*p++='\0';
			if (*p++!='&') {
				serverparsed_writeerror(conn,"Unknown operator");
				return 0;
			}
			return serverparsed_evaluateexpression(conn,exp) && serverparsed_evaluateexpression(conn,p);
			break;
		case '|':
			*p++='\0';
			if (*p++!='|') {
				serverparsed_writeerror(conn,"Unknown operator");
				return 0;
			}
			return serverparsed_evaluateexpression(conn,exp) || serverparsed_evaluateexpression(conn,p);
			break;
		default:
			/*expression contains no && or || operators*/
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
				serverparsed_writeerror(conn,"Syntax error");
				return 0;
			}

			lhs=malloc(len+1);
			if (lhs==NULL) return 0;
			rhs=malloc(len+1);
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
			if (op[0]) {
				len=strlen(rhs);
				if (rhs[0]=='/' && rhs[len-1]=='/') {
					/*rhs is a regex*/
					regex_t regex;

					rhs[--len]='\0';
					if (regcomp(&regex,rhs+1,REG_EXTENDED | REG_NOSUB)) {
						serverparsed_writeerror(conn,"Syntax error in regex");
					} else {
						ret=regexec(&regex,lhs,0,NULL,0)==REG_OKAY;
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
			free(lhs);
			free(rhs);
			return ret;
	}
	/*should never reach here*/
	return 0;
}

static void serverparsed_command(struct connection *conn,char *command,char *args)
{
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

	if (strcmp(command,"config")==0) {
		for (i=0;i<MAX_ARGS && attrs[i];i++) {
			vals[i]=serverparsed_expandvars(conn,vals[i]);
			if (strcmp(attrs[i],"errmsg")==0) {
				size_t len=strlen(vals[i]);
				if (conn->serverparsedinfo.errmsg) free(conn->serverparsedinfo.errmsg);
				conn->serverparsedinfo.errmsg=malloc(len+1);
				if (conn->serverparsedinfo.errmsg) memcpy(conn->serverparsedinfo.errmsg,vals[i],len+1);
			} else if (strcmp(attrs[i],"sizefmt")==0) {
				lower_case(vals[i]);
				if (strcmp(vals[i],"bytes")==0) {
					conn->serverparsedinfo.abbrev=0;
				} else if (strcmp(vals[i],"abbrev")==0) {
					conn->serverparsedinfo.abbrev=1;
				} else {
					serverparsed_writeerror(conn,"Unknown value for sizefmt");
				}
			} else if (strcmp(attrs[i],"timefmt")==0) {
				size_t len=strlen(vals[i]);
				if (conn->serverparsedinfo.timefmt) free(conn->serverparsedinfo.timefmt);
				conn->serverparsedinfo.timefmt=malloc(len+1);
				if (conn->serverparsedinfo.timefmt) memcpy(conn->serverparsedinfo.timefmt,vals[i],len+1);
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
			writestring(conn->socket,var);
		} else {
			serverparsed_writeerror(conn,"Syntax error");
		}
	} else if (strcmp(command,"exec")==0) {
		serverparsed_writeerror(conn,"exec command is not yet supported");
	} else if (strcmp(command,"fsize")==0) {
		vals[0]=serverparsed_expandvars(conn,vals[0]);
		if (strcmp(attrs[0],"file")==0) {
			char *filename;
			os_error *err;
			fileswitch_object_type objtype;
			int size;

			filename=serverparsed_reltofilename(conn->filename,vals[0]);
			err=xosfile_read_no_path(filename,&objtype,NULL,NULL,&size,NULL);
			if (err) {
				serverparsed_writeerror(conn,err->errmess);
			} else if (objtype==fileswitch_NOT_FOUND) {
				serverparsed_writeerror(conn,"File not found");
			} else {
				char buf[12];

				if (conn->serverparsedinfo.abbrev) {
					if (size<1024) sprintf(buf,"%d bytes",size);
					else if (size<1024*10) sprintf(buf,"%.1f KB",(double)size/1024);
					else if (size<1024*1024) sprintf(buf,"%d KB",size/1024);
					else if (size<1024*1024*10) sprintf(buf,"%.1f MB",(double)size/(1024*1024));
					else sprintf(buf,"%d MB",size/(1024*1024));
				} else {
					sprintf(buf,"%d",size);
				}
				writestring(conn->socket,buf);
			}
		} else {
			serverparsed_writeerror(conn,"Unknown attribute to fsize command");
		}
	} else if (strcmp(command,"flastmod")==0) {
		vals[0]=serverparsed_expandvars(conn,vals[0]);
		if (strcmp(attrs[0],"file")==0) {
			char *filename;
			os_error *err;
			fileswitch_object_type objtype;
			bits load,exec,filetype;

			filename=serverparsed_reltofilename(conn->filename,vals[0]);
			err=xosfile_read_stamped_no_path(filename,&objtype,&load,&exec,NULL,NULL,&filetype);
			if (err) {
				serverparsed_writeerror(conn,err->errmess);
			} else if (objtype==fileswitch_NOT_FOUND) {
				serverparsed_writeerror(conn,"File not found");
			} else if (filetype==-1) {
				serverparsed_writeerror(conn,"File does not have a datestamp");
			} else {
				struct tm time;
				char utc[5];
				char str[50]="";

				utc[4] = load &255;
				utc[3] = (exec>>24) &255;
				utc[2] = (exec>>16) &255;
				utc[1] = (exec>>8) &255;
				utc[0] = exec &255;
				utc_to_localtime(utc,&time);
				if (conn->serverparsedinfo.timefmt==NULL) time_to_rfc(&time,str); else strftime(str,49,conn->serverparsedinfo.timefmt,&time);
				writestring(conn->socket,str);
			}
		}
	} else if (strcmp(command,"include")==0) {
		/**/
	} else if (strcmp(command,"set")==0) {
		vals[0]=serverparsed_expandvars(conn,vals[0]);
		vals[1]=serverparsed_expandvars(conn,vals[1]);
		if (attrs[0]==NULL || attrs[1]==NULL || strcmp(attrs[0],"var")!=0 || strcmp(attrs[1],"value")!=0) {
			serverparsed_writeerror(conn,"Syntax error");
		} else {
			set_var_val(vals[0],vals[1]);
		}
	} else if (strcmp(command,"if")==0) {
		vals[0]=serverparsed_expandvars(conn,vals[0]);
		if (attrs[0]==NULL || strcmp(attrs[0],"expr")!=0) {
			serverparsed_writeerror(conn,"Syntax error");
		} else {
			conn->serverparsedinfo.output=conn->serverparsedinfo.conditionmet=serverparsed_evaluateexpression(conn,vals[0]);
		}
	} else if (strcmp(command,"elif")==0) {
		if (conn->serverparsedinfo.conditionmet) {
			conn->serverparsedinfo.output=0;
		} else {
			vals[0]=serverparsed_expandvars(conn,vals[0]);
			if (attrs[0]==NULL || strcmp(attrs[0],"expr")!=0) {
				serverparsed_writeerror(conn,"Syntax error");
			} else {
				conn->serverparsedinfo.output=conn->serverparsedinfo.conditionmet=serverparsed_evaluateexpression(conn,vals[0]);
			}
		}
	} else if (strcmp(command,"else")==0) {
		if (conn->serverparsedinfo.conditionmet) conn->serverparsedinfo.output=0; else conn->serverparsedinfo.output=1;
	} else if (strcmp(command,"endif")==0) {
		conn->serverparsedinfo.output=1;
	}
}

#define WRITEBUFFER {\
	int bytes=o-outputbuffer;\
	int bytessent;\
	if (!conn->serverparsedinfo.output) {\
		o=outputbuffer;\
	} else if (bytes>0) {\
		bytessent = ip_write(conn->socket, outputbuffer, bytes);\
/*		fwrite(outputbuffer,1,bytes,stderr);*/\
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

static char command[256], args[256];
static int commandlength,argslength;
static int serverparsed_parse(struct connection *conn, char *buffer, int bytesleft)
{
	char *p=buffer;
	char outputbuffer[OUTPUT_BUFFERSIZE];
	char *o=outputbuffer;
	char *oend=outputbuffer+OUTPUT_BUFFERSIZE-1; /*ptr to last char in buffer*/
	int byteswritten=0;
	int bytestowrite=bytesleft;

	while (bytesleft>0) {
		switch (status) {
			case status_BODY:
				if (*p=='<') {
					p++;
					bytesleft--;
					status=status_OPEN1;
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
					status=status_OPEN2;
				} else {
					*o++='<';
					status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN2:
				if (*p=='-') {
					p++;
					bytesleft--;
					status=status_OPEN3;
				} else {
					*o++='<';
					if (o>oend) WRITEBUFFER;
					*o++='!';
					status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN3:
				if (*p=='-') {
					p++;
					bytesleft--;
					status=status_OPEN4;
				} else {
					*o++='<';
					if (o>oend) WRITEBUFFER;
					*o++='!';
					if (o>oend) WRITEBUFFER;
					*o++='-';
					status=status_BODY;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_OPEN4:
				if (*p=='#') {
					p++;
					bytesleft--;
					status=status_COMMAND;
					commandlength=0;
					argslength=0;
				} else {
					status=status_BODY;
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
					command[commandlength]='\0';
					status=status_ARGS;
				} else {
					command[commandlength++]=*p++;
					/*check commandlength is within limits*/
					bytesleft--;
				}
				break;
			case status_ARGS:
				/*fixme: cope with '-' or '>' in args*/
				if (*p=='-') {
					p++;
					bytesleft--;
					args[argslength]='\0';
					status=status_CLOSE1;
				} else if (*p=='"') {
					status=status_QUOTEDARGS;
					args[argslength++]=*p++;
					bytesleft--;
				} else {
					args[argslength++]=*p++;
					/*check argslength*/
					bytesleft--;
				}
				break;
			case status_QUOTEDARGS:
				if (*p=='"') {
					status=status_ARGS;
					args[argslength++]=*p++;
					bytesleft--;
				} else {
					args[argslength++]=*p++;
					/*check argslength*/
					bytesleft--;
				}
				break;
			case status_CLOSE1:
				if (*p=='-') {
					p++;
					bytesleft--;
					status=status_CLOSE2;
				} else {
					*o++='-';
					status=status_ARGS;
					if (o>oend) WRITEBUFFER;
				}
				break;
			case status_CLOSE2:
				if (*p=='>') {
					p++;
					bytesleft--;
					status=status_BODY;
					WRITEBUFFER;
					serverparsed_command(conn,command,args);
				} else {
					status=status_ARGS;
					*o++='-';
					if (o>oend) WRITEBUFFER;
					*o++='-';
					if (o>oend) WRITEBUFFER;
				}
				break;
		}
	}
	WRITEBUFFER;
	return bytestowrite;
}

static void serverparsed_tidyup(void)
{
	cgiscript_removevars();
	remove_var("DOCUMENT_NAME");
	remove_var("DOCUMENT_URI");
}

int serverparsed_poll(struct connection *conn,int maxbytes) {
/* attempt to write a chunk of data from the file (bandwidth-limited) */
/* close the connection if EOF is reached */
/* return the number of bytes written */
	int bytes = 0;
	char temp[HTTPBUFFERSIZE];

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
				/*  IDEA: if there's less than 512 bytes left in the buffer, */
				/* move those to the start of the buffer, and refill; that may */
				/* be faster than sending the 512 bytes on their own */
				if (conn->leftinbuffer == 0) {
					conn->leftinbuffer = fread(conn->filebuffer, 1,configuration.readaheadbuffer*1024, conn->file);
					conn->positioninbuffer = 0;
				}
				if (bytes > conn->leftinbuffer)  bytes = conn->leftinbuffer;
				bytes = serverparsed_parse(conn, conn->filebuffer + conn->positioninbuffer, bytes);
/*				bytes = ip_write(conn->socket, conn->filebuffer + conn->positioninbuffer,bytes);*/
				conn->positioninbuffer += bytes;
				conn->leftinbuffer -= bytes;
			} else {
				fseek(conn->file, conn->fileused, SEEK_SET);
				bytes = fread(temp, 1, bytes, conn->file);
				bytes = serverparsed_parse(conn, temp, bytes);
/*				bytes = ip_write(conn->socket, temp, bytes);*/
			}
		} else {            /* read from buffer */
			bytes = serverparsed_parse(conn, conn->filebuffer+conn->fileused, bytes);
/*			bytes = ip_write(conn->socket, conn->filebuffer+conn->fileused, bytes);*/
		}

		conn->timeoflastactivity = clock();

		if (bytes < 0) {
			/* errorcode = -bytes; */
			*temp = '\0';
			switch (-bytes) {
			case IPERR_BROKENPIPE:
#ifdef LOG
				writelog(LOGLEVEL_ABORT, "ABORT connection closed by client");
#endif
				break;
			}
			close(conn->index, 1);           /* close the connection, with force */
			serverparsed_tidyup();
			return 0;
		} else {
			conn->fileused += bytes;
		}
	}


	/* if EOF reached, close */
	if (conn->fileused >= conn->fileinfo.size) {
		close(conn->index, 0);
		serverparsed_tidyup();
	}

	return bytes;
}

