
// number of report files to cache - this should probably be about
// 50% of the total number of report files in <WebJames$Dir>.Reports
#define REPORTCACHECOUNT  8

#define REPORT_SUBSTITUTE_NOT_TESTED    0
#define REPORT_SUBSTITUTE_NOT_NEEDED    1
#define REPORT_SUBSTITUTE_NEEDED        2


typedef struct reportcache {
  char *buffer;
  int size;                   // size of the cached file
  int report;                 // report no. or -1
  int time;                   // clock() value of last access
  char substitute;            // 0 = not tested, 1 = none, 2 = some
  char reload;                // set to 1 to force reload at next access
} reportcache;

typedef struct substitute {
  char *name;
  char *value;
  int namelen, valuelen;
} substitute;

extern struct reportcache reports[REPORTCACHECOUNT];

void report(struct connection *conn, int code, int subno, int headerlines);
struct reportcache *report_getfile(int report);
void report_moved(struct connection *conn, char *newurl);
void report_movedtemporarily(struct connection *conn, char *newurl);
void report_notmodified(struct connection *conn);
void report_nocontent(struct connection *conn);
void report_badrequest(struct connection *conn, char *info);
void report_unauthorized(struct connection *conn, char *realm);
void report_forbidden(struct connection *conn);
void report_notfound(struct connection *conn);
void report_busy(struct connection *conn, char *text);
void report_servererr(struct connection *conn);
void report_notimplemented(struct connection *conn, char *request);
void report_flushcache(void);
