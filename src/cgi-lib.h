/* cgi-lib - C library for WebJames cgi-scripts */

//   macros

#define MESSAGE_WEBJAMES_CMD  0x4f988

#define CGI_HEADER_UNKNOWN    0
#define CGI_HEADER_RMA        1
#define CGI_HEADER_FILE       2
#define CGI_HEADER_DYNAMIC    3

#define CGI_METHOD_GET        0
#define CGI_METHOD_POST       1
#define CGI_METHOD_HEAD       2

typedef struct webjames_message_body {
  int flags;
  union cmd {
    char *pointer;
    char embedded[232];
  } cmd;
} webjames_message_body;

extern char _cgi_temp[2048], _cgi_requestline[4100], _cgi_headerfile[256];
extern char _cgi_host[128], _cgi_username[64], _cgi_taskname[32];
extern char *_cgi_parameters, *_cgi_headeraddress;
extern int _cgi_multitask, _cgi_multitasktime, _cgi_headerstorage;
extern int _cgi_headermeminfo, _cgi_releaseheader, _cgi_requestsize;
extern socket_s _cgi_socket;
extern int _cgi_httpversion, _cgi_requesttype;
extern int _cgi_maxbytespersecond, _cgi_headerdone, _cgi_byteswritten;
extern int _cgi_polldelay, _cgi_starttime, _cgi_responscode;
extern int _cgi_requestlinelength, _cgi_parameterssize, _cgi_parametercount;

int cgi_init(int argc, char **argv);
void cgi_multitask(char *taskname, int wimpslot);
void cgi_headerdone(void);
void cgi_respons(int code, char *text);
void cgi_writeheader(char *text);
void cgi_writestring(char *string);
void cgi_reporterror(char *error);
void cgi_mimetype(char *filename, char *mimetype);
void cgi_sendhtml(char *buffer, int length);
int cgi_sendfile(char *filename);
void cgi_writefromfile(FILE *file, int bytes);
void cgi_writebuffer(char *buffer, int length);
int *cgi_writetosocket(char *buffer, int bytes, int *written);
void cgi_quit(void);
void cgi_poll(void);
void cgi_webjamescommand(char *ptr, int release);
int cgi_decodeescaped(char *in, char *out, int max);
int cgi_extractparameter(char *in, char **name, int *nlen, char **val, int *vlen);
