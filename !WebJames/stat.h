
// loglevel for various log entries
#define LOGLEVEL_OPEN         4
#define LOGLEVEL_CMD          4
#define LOGLEVEL_DNS          6
#define LOGLEVEL_ABORT        4
#define LOGLEVEL_REQUEST      4
#define LOGLEVEL_CGISTART     4
#define LOGLEVEL_CACHE        4
#define LOGLEVEL_REPORT       3

#define LOGLEVEL_ALWAYS       0
#define LOGLEVEL_NEVER        10

#define LOGLEVEL_FROM         5
#define LOGLEVEL_REFERER      7
#define LOGLEVEL_USERAGENT    7


typedef struct stats {
  time_t starttime;
  int time, currenttime, adjusttime;
  int written, access;
  int written2, access2;
} stats;

extern struct stats statistics;
extern char weblog[256], clflog[256], rename_cmd[256];
extern int clfupdatetime;
extern FILE *clffile;
extern int loglevel, log_close_delay;

extern int log_max_age, log_max_copies, log_max_size;
extern int clf_max_age, clf_max_copies, clf_max_size;

void init_statistics(void);
void write_statistics(void);
void update_statistics(void);

void writelog(int level, char *string);
void closelog(void);
void rotate_log(char *logfile, int copies);

void clf_connection_closed(int cn);
void clf_cgi_finished(int code, int bytes, char *host, char *request);
void writeclf(char *string);
