#ifndef DATETIME_H
#define DATETIME_H

void time_to_rfc(struct tm *time, char *out);
void rfc_to_time(char *rfc, struct tm *time);
void utc_to_time(os_date_and_time *utc, struct tm *time);
void utc_to_localtime(os_date_and_time *utc, struct tm *time);
int compare_time(struct tm *time1, struct tm *time2);

#endif /*DATETIME_H*/
