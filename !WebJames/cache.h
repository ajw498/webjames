
/* max no. of files to cache - the more, the slower the cache-scan */
#define MAXCACHEFILES         30


typedef struct cache {
	char filename[256];
	char name[256];
	int size;
	int namelen, checksum;	/* namelen and checksum are used */
							/* to speed up the cache-scanning */
	struct tm date;
	int filetype;
	char mimetype[128];

	char *buffer;

	int removewhenidle;
	int timeoflastaccess;                 /* clock()/100 values */
	int accesses, totalaccesses;
} cache;


struct cache *get_file_through_cache(struct connection *conn);
void flushcache(char *name);
void init_cache(char *list);
void cache_release_file(struct cache *entry);
void kill_cache(void);
void remove_from_cache(int i);
