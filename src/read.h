void pollread(int cn);
void pollread_body(struct connection *conn, int bytes);
void pollread_header(struct connection *conn, int bytes);
int readline(char *buffer, int bytes, char *line);
