
#define CGI_API_WEBJAMES      0
#define CGI_API_REDIRECT      1

#define SCRIPT_CGI            0


void script_start(int scripttype, struct connection *conn, char *script, int pwd, char *args);
void script_start_webjames(int scripttype, struct connection *conn, char *script, int pwd);
void script_start_redirect(char *script, struct connection *conn, char *args, int pwd);
