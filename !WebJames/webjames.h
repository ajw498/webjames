

#define MAXCONNECTIONS    100
#define HTTPBUFFERSIZE    4096

#define WJ_STATUS_HEADER  1
#define WJ_STATUS_BODY    2
#define WJ_STATUS_WRITING 3
#define WJ_STATUS_DNS     4

#define METHOD_GET        0
#define METHOD_POST       1
#define METHOD_HEAD       2
#define METHOD_DELETE     3
#define METHOD_PUT        4

#define HTTP_OK           200
#define HTTP_NOCONTENT    204

#define HTTP_MOVED        301
#define HTTP_TEMPMOVED    302
#define HTTP_NOTMODIFIED  304

#define HTTP_BADREQUEST   400
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN    403
#define HTTP_NOTFOUND     404

#define HTTP_SERVERERR    500
#define HTTP_NOTIMPLEMENTED 501
#define HTTP_BUSY         503

#define DNS_FAILED        0
#define DNS_TRYING        1
#define DNS_OK            2

#ifndef ERRORDOC
#define ERRORDOC

typedef struct errordoc {
	int status; /* http status code */
	char *report; /* text to use, or url to redirect to */
	struct errordoc *next; /* only used in conn structures, not in attributes structures */
} errordoc;

#endif


typedef struct serverinfo {
	int port;
	int socket;
} serverinfo;


typedef struct connection {

	int socket, port;
	int status;                 /* unused/header/body/write/dns */
	char index;                 /* index in connections[] */
	char method;                /* 0 GET  1=POST  2=HEAD */
	int httpminor;
	int httpmajor;
	int statuscode;             /* code returned to the user */

	char dnsstatus;             /* DNS_FAILED, DNS_TRYING or DNS_OK */
	char host[128];             /* a.b.c.d or name */
	char ipaddr[4];
	int dnsendtime;             /* clock() value */

	struct {
		unsigned int releasefilebuffer : 1;  /* release filebuffer when done */
		unsigned int deletefile        : 1;  /* delete file when done */
		unsigned int cacheable         : 1;
		unsigned int is_cgi            : 1;
		unsigned int stripextensions   : 1;  /* strip any filename extension when looking for the file */
		unsigned int setcsd            : 1;  /* set the currently selected directory to the dir containing the cgi script */
	} flags;

	/* various header-lines, all malloc()'ed */
	char *uri, *accept, *type, *referer, *useragent;
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

	FILE *file;                 /* if file != NULL, data will be read */
								/* directly from the file */
	char *filebuffer;           /* if file == NULL, data will be read from */
								/* the filebuffer (typically the cached file) */
	char filename[256];
	struct {
		int filetype;
		char mimetype[128];
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


} connection;


extern struct connection *connections[MAXCONNECTIONS];

/* configuration */
typedef struct config {
	int timeout, bandwidth;
	char server[128], panic[504], *xheader[17], webmaster[256];
	char delete_script[256], put_script[256], site[256];
	char attributesfile[256], serverip[256], cgi_in[256], cgi_out[256];
	char htaccessfile[256];
	int xheaders;
	int casesensitive;
	int reversedns;
} config;

extern struct config configuration;

extern struct serverinfo servers[8];

extern int readcount, writecount, dnscount, slowdown;
extern int activeconnections;
extern char select_read[32], select_write[32], select_except[32];
extern int cachesize, maxcachefilesize;
extern int readaheadbuffer, maxrequestsize;

extern char line[HTTPBUFFERSIZE], temp[HTTPBUFFERSIZE];


int webjames_init(char *config);
void webjames_kill(void);
int webjames_poll(void);
void webjames_command(char *cmd, int release);

void writestring(int socket, char *string);
void abort_reverse_dns(struct connection *conn, int newstatus);
void read_config(char *config);

void time_to_rfc(struct tm *time, char *out);
void rfc_to_time(char *rfc, struct tm *time);
void utc_to_time(char *utc, struct tm *time);
int compare_time(struct tm *time1, struct tm *time2);
