#include "regex/regex.h"


#define ATTR___A              0
#define ATTR_A_Z              1
#define ATTR_Z_a              27
#define ATTR_a_z              28
#define ATTR_z__              56

#ifndef ERRORDOC
#define ERRORDOC

typedef struct errordoc {
	int status; /* http status code */
	char *report; /* text to use, or url to redirect to */
	struct errordoc *next; /* only used in conn structures, not in attrributes strcutures */
} errordoc;

#endif

typedef struct attributes {
	char *accessfile;
	char *userandpwd;
	char *realm;

	char *moved;
	char *tempmoved;
	char **defaultfiles; /* list of default files to use for an index page */
	int defaultfilescount;

	char cacheable;        /* 0 no, 1 yes */
	char hidden;           /* 0 no, 1 yes */
	char is_cgi;           /* 0 no, 1 yes */
	char setcsd;           /* 0 no, 1 yes */
	char stripextensions;  /* 0 no, 1 yes */
	char cgi_api;          /* 0 normal, 1 redirect */

	int methods;	/* allowed methods, default is GET/HEAD/POST */

	char *homedir;
	int ignore;		/* no. of  chars of the URI to ignore when converting the */
					/* URI to a filename - set when an homedir-entry is found */
					/* for the URL */
					/* if the URI is /~johnny/pics/index.html and there's an */
					/* attribute-section for /~johnny/pics/, 'ignore' will be */
					/* set to strlen("/~johnny/pics/) and so the RISC OS */
					/* filename will be <johnnys-home-dir>.index/html */
	int urilen;
	char *uri;

	int port;		/* allowed port number */

	int *allowedhosts;    /* list of allowed hosts (ip-address) */
	int *forbiddenhosts;  /* list of forbidden hosts (ip-address) */
	int allowedhostscount, forbiddenhostscount;

	int *allowedfiletypes;    /* list of allowed filetypes for cgi scripts */
	int *forbiddenfiletypes;  /* list of forbidden filetypes for cgi scripts */
	int allowedfiletypescount, forbiddenfiletypescount;

	struct errordoc *errordocs;  /* list of custom error reports */
	int errordocscount;

	regex_t *regex; /* ptr to compiled regex */

	struct {
		unsigned int accessfile         : 1;
		unsigned int userandpwd         : 1;
		unsigned int realm              : 1;
		unsigned int moved              : 1;
		unsigned int tempmoved          : 1;
		unsigned int cacheable          : 1;
		unsigned int stripextensions    : 1;
		unsigned int setcsd             : 1;
		unsigned int is_cgi             : 1;
		unsigned int cgi_api            : 1;
		unsigned int homedir            : 1;
		unsigned int methods            : 1;
		unsigned int port               : 1;
		unsigned int hidden             : 1;
		unsigned int defaultfile        : 1;
		unsigned int allowedfiletypes   : 1;
		unsigned int forbiddenfiletypes : 1;
	} defined;

	struct attributes *next;
} attributes;


void init_attributes(char *filename);
void get_attributes(char *uri, struct connection *conn);

