
typedef struct homedirobject {
	char *username;
	int usernamelen;
	char *dir;
	struct homedirobject *next;
} homedirobject;

typedef struct movedobject {
	char *path;
	char *url;
	int pathlen;
	int temporarily;
	struct movedobject *next;
} movedobject;

typedef struct accessobject {
	char *path;
	char *realm;
	char *userpwd;
	int isfile;
	int pathlen;
	struct accessobject *next;
} accessobject;

void pollwrite(int cn);
void select_writing(int cn);
void send_file(struct connection *conn);
int uri_to_filename(char *uri, char *filename);
int get_file_info(char *filename, char *mimetype, struct tm *date, int *size, int checkcase);

int check_access(struct connection *conn, char *authorization);
