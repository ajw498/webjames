/*
	$Id: wjstring.c,v 1.1 2002/02/17 22:50:10 ajw Exp $
	String handling functions for WebJames
	Warning: These are subtly different from their ANSI equivalents
*/

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/*Do not include webjames.h*/
#include "wjstring.h"

#include <time.h>

#include "stat.h"

char *wjstrncpy(char *dest, const char *src, const size_t n)
/*copy at most n characters from src to dest, and ensure that dest is 0 terminated (even if the string gets truncated)*/
{
	dest[n-1]='\0';
	return strncpy(dest,src,n-1);
}

char *wjstrncat(char *dest, const char *src, const size_t n)
/*concatenate src to the end of dest, ensuring that the total length of dest does not exceed n*/
/*also makes sure that the result is 0 ternimated*/
{
	size_t len;

	len=strlen(dest);
	if (len>n) return dest;
	strncpy(dest+len,src,n-len);
	dest[n-1]='\0';
	return dest;
}

int wjstrnicmp(char *s1, char *s2,size_t n)
/*compares n characters, case insensitively. returns zero if equal*/
{
	int i;

	for (i=0;i<n;i++) if (tolower(s1[i])!=tolower(s2[i])) return 1;
	return 0;
}
