
#define ATTR___A              0
#define ATTR_A_Z              1
#define ATTR_Z_a              27
#define ATTR_a_z              28
#define ATTR_z__              56


typedef struct attributes {
  char *accessfile;
  char *userandpwd;
  char *realm;

  char *moved;
  char *tempmoved;
  char *defaultfile;

  char cacheable;   // 0 no, 1 yes
  char hidden;      // 0 no, 1 yes
  char is_cgi;      // 0 no, 1 yes
  char cgi_api;     // 0 normal, 1 redirect

  int methods;      // allowed methods, default is GET/HEAD/POST

  char *homedir;
  int ignore;       // no. of  chars of the URI to ignore when converting the
                    // URI to a filename - set when an homedir-entry is found
                    // for the URL
                    // if the URI is /~johnny/pics/index.html and there's an
                    // attribute-section for /~johnny/pics/, 'ignore' will be
                    // set to strlen("/~johnny/pics/) and so the RISC OS
                    // filename will be <johnnys-home-dir>.index/html
  int urilen;
  char *uri;

  int port;         // allowed port number

  int *allowedhosts;    // list of allowed hosts (ip-address)
  int *forbiddenhosts;  // list of forbidden hosts (ip-address)
  int allowedhostscount, forbiddenhostscount;

  struct {
    unsigned int accessfile  : 1;
    unsigned int userandpwd  : 1;
    unsigned int realm       : 1;
    unsigned int moved       : 1;
    unsigned int tempmoved   : 1;
    unsigned int cacheable   : 1;
    unsigned int is_cgi      : 1;
    unsigned int cgi_api     : 1;
    unsigned int homedir     : 1;
    unsigned int methods     : 1;
    unsigned int port        : 1;
    unsigned int hidden      : 1;
    unsigned int defaultfile : 1;
  } defined;

  struct attributes *next, *previous;
} attributes;


void init_attributes(char *filename);
void read_attributes_file(char *filename, char *baseuri);
void get_attributes(char *uri, struct connection *conn);
void insert_attributes(struct attributes *attr);
