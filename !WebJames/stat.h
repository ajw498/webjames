#ifndef STAT_H
#define STAT_H

/* loglevel for various log entries */
#define LOGLEVEL_OPEN         4
#define LOGLEVEL_CMD          4
#define LOGLEVEL_DNS          6
#define LOGLEVEL_ABORT        4
#define LOGLEVEL_REQUEST      4
#define LOGLEVEL_CGISTART     4
#define LOGLEVEL_CACHE        4
#define LOGLEVEL_REPORT       3
#define LOGLEVEL_ATTRIBUTES   2

#define LOGLEVEL_ALWAYS       0
#define LOGLEVEL_NEVER        10

#define LOGLEVEL_FROM         5
#define LOGLEVEL_REFERER      7
#define LOGLEVEL_USERAGENT    7
#define LOGLEVEL_HEADER       8


typedef struct stats {
	time_t starttime;
	int time, currenttime, adjusttime;
	int written, access;
	int written2, access2;
} stats;

extern struct stats statistics;

void init_statistics(void);
void update_statistics(void);

void writelog(int level, char *string);
void close_log(void);

void clf_connection_closed(int cn);
void clf_cgi_finished(int code, int bytes, char *host, char *request);
void writeclf(char *string);

#endif
