
typedef void (*handlerstartfn)(struct connection *conn);
typedef int  (*handlerpollfn)(struct connection *conn, int maxbytes);

typedef struct handlerentry {
	char *name;
	int cache;
	int (*initfn)(void);
    handlerstartfn startfn;
    handlerpollfn pollfn;
	void (*quitfn)(void);
} handlerentry;

typedef struct handler {
	char *name;             /* name of handler */
	char *command;          /* *command to use for handler */
	char unix;              /* 0 use RISC OS redirection, 1 use Unix style redirection */
	char cache;             /* 1 to indicate that the handler wants the file cached if possible */
	handlerstartfn startfn; /* function to call to start handler */
	handlerpollfn pollfn;   /* function to call to poll handler */
	struct handler *next;
} handler;

void handler_start(struct connection *conn);
int handler_poll(struct connection *conn, int maxbytes);
void add_handler(char *name, char *command);
struct handler *get_handler(char *name);
int init_handlers(void);
void quit_handlers(void);


