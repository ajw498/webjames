int check_case(char *filename);
void pollwrite(int cn);
void select_writing(int cn);
void send_file(struct connection *conn);
int uri_to_filename(char *uri, char *filename, int stripextension);
int get_file_info(char *filename, char *mimetype, struct tm *date, int *size, int checkcase);

int check_access(struct connection *conn, char *authorization);
