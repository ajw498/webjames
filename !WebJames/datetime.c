/*
	$Id: datetime.c,v 1.2 2001/09/03 14:10:33 AJW Exp $
	Date and time convertion functions
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "oslib/territory.h"

#include "webjames.h"
#include "stat.h"
#include "datetime.h"


int compare_time(struct tm *time1, struct tm *time2) {
/* compare to tm structures */
/* return either -1 (time1<time2), 0 (time1==time2) or 1 (time1>time2) */
	if (time1->tm_year < time2->tm_year)  return -1;
	if (time1->tm_year > time2->tm_year)  return 1;
	if (time1->tm_mon  < time2->tm_mon)   return -1;
	if (time1->tm_mon  > time2->tm_mon)   return 1;
	if (time1->tm_mday < time2->tm_mday)  return -1;
	if (time1->tm_mday > time2->tm_mday)  return 1;
	if (time1->tm_hour < time2->tm_hour)  return -1;
	if (time1->tm_hour > time2->tm_hour)  return 1;
	if (time1->tm_min  < time2->tm_min)   return -1;
	if (time1->tm_min  > time2->tm_min)   return 1;
	if (time1->tm_sec  < time2->tm_sec)   return -1;
	if (time1->tm_sec  > time2->tm_sec)   return 1;
	return 0;
}


void utc_to_time(os_date_and_time *utc, struct tm *time) {
/* converts 5 byte UTC to tm structure */

/* utc              char[5] holding the utc value */
/* time             tm structure to fill in */
	territory_ordinals ordinals;

	EV(xterritory_convert_time_to_utc_ordinals((os_date_and_time const *)utc, &ordinals));
	time->tm_sec   = ordinals.second;
	time->tm_min   = ordinals.minute;
	time->tm_hour  = ordinals.hour;
	time->tm_mday  = ordinals.date;
	time->tm_mon   = ordinals.month - 1;
	time->tm_year  = ordinals.year - 1900;
	time->tm_wday  = ordinals.weekday - 1;
	time->tm_yday  = ordinals.yearday - 1;
	time->tm_isdst = 0;
}

void utc_to_localtime(os_date_and_time *utc, struct tm *time) {
/* converts 5 byte UTC to tm structure */

/* utc              char[5] holding the utc value */
/* time             tm structure to fill in */
	territory_ordinals ordinals;

	EV(xterritory_convert_time_to_ordinals(territory_CURRENT,(os_date_and_time const *)utc, &ordinals));
	time->tm_sec   = ordinals.second;
	time->tm_min   = ordinals.minute;
	time->tm_hour  = ordinals.hour;
	time->tm_mday  = ordinals.date;
	time->tm_mon   = ordinals.month - 1;
	time->tm_year  = ordinals.year - 1900;
	time->tm_wday  = ordinals.weekday - 1;
	time->tm_yday  = ordinals.yearday - 1;
	time->tm_isdst = 0;
}


void rfc_to_time(char *rfc, struct tm *time) {
/* converts a RFC timestring to a tm structure */

/* rfc              pointer to string */
/* time             tm structure to fill in */
	char *start;
	char days[]="303232332323"; /* array holding (the number days in the month)-28 */
	int date, month, year, hour, min, sec, letter1, letter2, letter3;
	int wday, yday, m;

	date = 0;
	month = 6;
	year = 1998;
	hour = 0;
	min = 0;
	sec = 0;
	wday = 0;
	yday = 0;

	start = rfc;
	while (*start == ' ')  start++;
	letter1 = toupper(start[0]);
	letter2 = toupper(start[1]);
	letter3 = toupper(start[2]);

	if (letter1 == 'M')
		wday = 0;
	else if ((letter1 == 'T') && (letter2 == 'U'))
		wday = 1;
	else if (letter1 == 'W')
		wday = 2;
	else if (letter1 == 'T')
		wday = 3;
	else if (letter1 == 'F')
		wday = 4;
	else if ((letter1 == 'S') && (letter2 == 'A'))
		wday = 5;
	else if (letter1 == 'S')
		wday = 6;

	start = strchr(rfc, ',');
	if (start == NULL)  start = rfc;

	while ((!isdigit(*start)) && (*start))  start++;
	if (!(*start))  return;
	date = atoi(start) - 1;

	while ((!isalpha(*start)) && (*start))  start++;
	if (!(*start))  return;

	letter1 = toupper(start[0]);
	letter2 = toupper(start[1]);
	letter3 = toupper(start[2]);
	if ((letter1 == 'J') && (letter2 == 'A'))
		month = 0;
	else if (letter1 == 'F')
		month = 1;
	else if ((letter1 == 'M') && (letter3 == 'R'))
		month = 2;
	else if ((letter1 == 'A') && (letter2 == 'P'))
		month = 3;
	else if (letter1 == 'M')
		month = 4;
	else if ((letter1 == 'J') && (letter3 == 'N'))
		month = 5;
	else if (letter1 == 'J')
		month = 6;
	else if (letter1 == 'A')
		month = 7;
	else if (letter1 == 'S')
		month = 8;
	else if (letter1 == 'O')
		month = 9;
	else if (letter1 == 'N')
		month = 10;
	else if (letter1 == 'D')
		month = 11;

	while ((!isdigit(*start)) && (*start))  start++;
	if (!(*start))  return;

	year = atoi(start);
	if (year < 40)
		year += 2000;
	else if (year < 100)
		year += 1900;
	if (year < 1990)  year = 1990;

	while ((*start != ' ') && (*start))  start++;
	if (!(*start))  return;

	hour = atoi(start);
	start = strchr(start, ':');
	if (start == NULL)  return;
	start++;

	min = atoi(start);
	start = strchr(start, ':');
	if (start == NULL)  return;
	sec = atoi(start+1);

	/* adjust the days[] array if it's a leap year */
	if ((year%4 == 0) && ((year%100 != 0) || (year%400 == 0)) )
		days[1] = '1';

	yday = date;
	for (m = 0; m < month; m++)
		yday += 28+days[m]-'0';

	time->tm_sec = sec;
	time->tm_min = min;
	time->tm_hour = hour;
	time->tm_mday = date;
	time->tm_mon = month;
	time->tm_year = year-1900;
	time->tm_wday = wday;
	time->tm_yday = yday;
	time->tm_isdst = 0;
}


void time_to_rfc(struct tm *time, char *out) {
/* converts a tm structure to a RFC timestring */

/* time             tm structure */
/* out              char-array to hold result, at least 26 bytes */

	strftime(out, 26, "%a, %d %b %Y %H:%M:%S", time);
}
