#ifndef WRITE_H
#define WRITE_H

#define FILE_DOESNT_EXIST  -1
#define FILE_LOCKED        -2
#define FILE_ERROR         -3
#define OBJECT_IS_DIRECTORY -4
#define FILE_NO_MIMETYPE   0x1000

#define ACCESS_FREE        0
#define ACCESS_ALLOWED     1
#define ACCESS_FAILED      2

int check_case(char *filename);
void pollwrite(int cn);
void select_writing(int cn);
void send_file(struct connection *conn);
int uri_to_filename(char *uri, char *filename, int stripextension);
int get_file_info(char *filename, char *mimetype, struct tm *date, os_date_and_time *utcdate, int *size, int checkcase, int mimeuseext);

int check_access(struct connection *conn, char *authorization);

#endif

