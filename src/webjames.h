#ifndef WEBJAMES_H
#define WEBJAMES_H

#define WEBJAMES_H_REVISION "$Revision: 1.2 $"

#define WEBJAMES_VERSION "0.35"
#define WEBJAMES_DATE "20/5/02"
#define WEBJAMES_SERVER_SOFTWARE "WebJames/" WEBJAMES_VERSION

#ifdef WEBJAMES_PHP_ONLY

/*Avoid pulling in any OSLib headers */
typedef int socket_s;

#else

#include "oslib/os.h"
#include "oslib/socket.h"
#include <stdarg.h>
#include "snprintf.h"

#endif

#define MAXCONNECTIONS    100
#define HTTPBUFFERSIZE    4096
#define TEMPBUFFERSIZE    4096

#define filetype_NONE -1
#define filetype_ALL  -2

#define WJ_STATUS_HEADER  1
#define WJ_STATUS_BODY    2
#define WJ_STATUS_WRITING 3
#define WJ_STATUS_DNS     4

#define METHOD_GET        0
#define METHOD_POST       1
#define METHOD_HEAD       2
#define METHOD_DELETE     3
#define METHOD_PUT        4

#define HTTP_OK            200
#define HTTP_NOCONTENT     204

#define HTTP_MOVED         301
#define HTTP_TEMPMOVED     302
#define HTTP_NOTMODIFIED   304

#define HTTP_BADREQUEST    400
#define HTTP_UNAUTHORIZED  401
#define HTTP_FORBIDDEN     403
#define HTTP_NOTFOUND      404
#define HTTP_NOTACCEPTABLE 406

#define HTTP_SERVERERR      500
#define HTTP_NOTIMPLEMENTED 501
#define HTTP_BUSY           503

#define DNS_FAILED        0
#define DNS_TRYING        1
#define DNS_OK            2

#define MAX_FILENAME 256
#define MAX_HEADERS 17
#define MAX_PANIC 504
#define MAX_IMAGEDIRS 16
#define MAX_MIMETYPE 128
#define MAX_HOSTNAME 128
#define MAX_VARY 60

typedef struct listeninfo {
	int port;
	socket_s socket;
} listeninfo;

typedef struct connection {

	struct connection *parent; /*the parent connection structure if this was #included from an SSI doc*/

	socket_s socket;
	int port;
	int status;                 /* unused/header/body/write/dns */
	char index;                 /* index in connections[] */
	char method;                /* 0 GET  1=POST  2=HEAD */
	char *methodstr;			/* method as a string*/
	int httpminor;
	int httpmajor;
	char *protocol;				/*protocol used to reply currently always "HTTP/1.0"*/
	int statuscode;             /* code returned to the user */

	char dnsstatus;             /* DNS_FAILED, DNS_TRYING or DNS_OK */
	char host[MAX_HOSTNAME];             /* a.b.c.d or name */
	char ipaddr[4];
	int dnsendtime;             /* clock() value */

	struct {
		unsigned int releasefilebuffer : 1;  /* release filebuffer when done */
		unsigned int deletefile        : 1;  /* delete file when done */
		unsigned int cacheable         : 1;
		unsigned int is_cgi            : 1;
		unsigned int stripextensions   : 1;  /* strip any filename extension when looking for the file */
		unsigned int multiviews        : 1;  /* content negotiation */
		unsigned int setcsd            : 1;  /* set the currently selected directory to the dir containing the cgi script */
		unsigned int outputheaders     : 1;  /* output the http headers. set unless this file is nested within an SSI doc */
	} flags;

	/* various header-lines, all malloc()'ed */
	char *uri, *accept, *acceptlanguage, *acceptcharset, *acceptencoding, *type, *referer, *useragent;
	char *authorization, *requestline, *cookie;
	char *requesturi;           /* only for PUT and DELETE methods */

	int bodysize;               /* body (if POST) */
	char *body;                 /* malloc()-ed */

	/* attributes - from the attributes file */
	char *homedir;              /* path to the site on server */
	int homedirignore;
	char *userandpwd;           /* username:password or NULL */
	char *accessfile;           /* NULL for no accessfile */
	char *realm;                /* NULL if not password protected */
	char *moved;                /* new URI */
	char *tempmoved;            /* new URI */
	char **defaultfiles;        /* usually 'index.html' */
	int defaultfilescount;
	int *allowedfiletypes;      /* filetypes that are allowed to be run as cgi scripts */
	int allowedfiletypescount;
	int *forbiddenfiletypes;    /* filetypes that are forbidden to be run as cgi scripts */
	int forbiddenfiletypescount;
	struct {
		unsigned int accessallowed : 1;
		unsigned int hidden : 1;
	} attrflags;
	char cgi_api;

	struct cache *cache;        /* pointer to cache entry or NULL */

	int headersize, headerallocated;
	char *header;               /* malloc()'ed */
	char vary[MAX_VARY];
	char *contentlocation;
	char *contentlanguage;

	FILE *file;                 /* if file != NULL, data will be read */
								/* directly from the file */
	char *filebuffer;           /* if file == NULL, data will be read from */
								/* the filebuffer (typically the cached file) */
	char filename[MAX_FILENAME];
	struct {
		int filetype;
		char mimetype[MAX_MIMETYPE];
		int size;               /* no. of bytes to write */
		struct tm date;
	} fileinfo;

	int fileused;               /* no. of bytes left */
	int leftinbuffer;           /* no. of bytes left in temp buffer (if */
								/*  file == NULL) */
	int positioninbuffer;       /* next unused byte in read-ahead buffer */

	struct tm if_modified_since;

	int timeoflastactivity;     /* clock() value - set when reading/writing */
	int starttime;              /* clock() value when request was received */

	int used;                   /* bytes used in the buffer */
	char buffer[HTTPBUFFERSIZE+4];

	struct errordoc *errordocs;    /* linked list used to hold custom error reports */

	struct handlerlist *handlers; /* linked list of handlers to apply */
	struct handler *handler; /* handler that is chosen from above list */


	int pwd; /* if if the file is password-protected */
	char *args; /* arguments passed to script */

	void *handlerinfo; /*ptr to a structure that holds information specific to the handler used*/

	void (*close)(struct connection *conn, int force); /*function to call to close the connection*/

} connection;

typedef	void (*closefn)(struct connection *conn, int force); /*function to call to close the connection*/

/* configuration */
typedef struct config {
	int timeout, bandwidth;
	char server[MAX_FILENAME], panic[MAX_PANIC], *xheader[MAX_HEADERS], *logheader[MAX_HEADERS], webmaster[MAX_FILENAME];
	char delete_script[MAX_FILENAME], put_script[MAX_FILENAME], site[MAX_FILENAME];
	char attributesfile[MAX_FILENAME], serverip[MAX_FILENAME], cgi_in[MAX_FILENAME], cgi_out[MAX_FILENAME], cgi_dir[MAX_FILENAME];
	char htaccessfile[MAX_FILENAME];
	int imagedirs[MAX_IMAGEDIRS];
	int numimagedirs;
	int xheaders;
	int logheaders;
	int casesensitive;
	int reversedns;

	int cachesize, maxcachefilesize;
	int readaheadbuffer, maxrequestsize;

	char weblog[MAX_FILENAME], clflog[MAX_FILENAME], rename_cmd[MAX_FILENAME];
	int syslog;
	int clfupdatetime;
	int loglevel, log_close_delay;
	int log_max_age, log_max_copies, log_max_size;
	int clf_max_age, clf_max_copies, clf_max_size;
	int logbuffersize;
	char *webjames_h_revision;
} config;

extern struct config configuration;

#ifndef WEBJAMES_PHP_ONLY
/* WEBJAMES_PHP_ONLY is defined if webjames.h is included by PHP*/
/* PHP only needs the connection and config structures, and the webjames_writestring/buffer/log functions*/

/*Error handling*/
/*Globals, but only used by the error handling macros*/
extern os_error *webjames_last_error;
extern void *webjames_last_malloc;

#ifdef MemCheck_MEMCHECK

#include "MemCheck:MemCheck.h"

#define DEBUG_FMT " %s, line %d"
#define DEBUG_ARGS ,__FILE__,__LINE__
#define DEBUG_REGISTER MemCheck_RegisterMiscBlock(webjames_last_error,sizeof(os_error)),
#define DEBUG_UNREGISTER MemCheck_UnRegisterMiscBlock(webjames_last_error),

#else

#define DEBUG_FMT
#define DEBUG_ARGS
#define DEBUG_REGISTER
#define DEBUG_UNREGISTER

#endif

/*Check the return from a SWI call, log the error if the was one, then return the error block pointer (or NULL)*/
#define E(x) ((webjames_last_error=(x))==NULL ? NULL : (\
DEBUG_REGISTER \
webjames_writelog(LOGLEVEL_OSERROR,"ERROR %s" DEBUG_FMT,webjames_last_error->errmess DEBUG_ARGS),\
DEBUG_UNREGISTER \
webjames_last_error))
/*As above, but prevent warning when used as a statement rather than as a test*/
#define EV(x) ((void)(E(x)))
/*Same as E(), but for malloc*/
#define EM(x) ((webjames_last_malloc=(x))!=NULL ? webjames_last_malloc : (webjames_writelog(LOGLEVEL_OSERROR,"ERROR malloc failed" DEBUG_FMT DEBUG_ARGS)))

/*The following functions should not be used; the wjstring functions or [v]snprintf should be used instead*/
#define strncpy(x,y,z) dontusestrncpy(x,y,z)
#define strcpy(x,y) dontusestrcpy(x,y)
#define strncat(x,y,z) dontusestrncat(x,y,z)
#define strcat(x,y) dontusestrcat(x,y)
#define sprintf dontusesprintf
#define vsprintf dontusevsprintf

typedef struct globalserverinfo {
	struct listeninfo servers[8];
	int serverscount;
	int readcount;
	int writecount;
	int slowdown;
	int dnscount;
	int activeconnections;
	char select_read[32], select_write[32], select_except[32];
} globalserverinfo;


/*globals*/
extern struct connection *connections[MAXCONNECTIONS];
extern struct globalserverinfo serverinfo;

extern char temp[HTTPBUFFERSIZE];

int webjames_init(char *config);
void webjames_kill(void);
int webjames_poll(int cs);
void webjames_command(char *cmd, int release);

void abort_reverse_dns(struct connection *conn, int newstatus);
void read_config(char *config);

#endif /*WEBJAMES_PHP_ONLY*/

#define webjames_writestringr(conn,string) \
	if (webjames_writestring(conn,string)<0) return

int webjames_writestring(struct connection *conn, const char *string);
int webjames_writebuffer(struct connection *conn, const char *buffer, int size);

int webjames_readbuffer(struct connection *conn, char *buffer, int size);

void *webjames_writelog(int level, char *fmt, ...);

#endif /*WEBJAMES_H*/
