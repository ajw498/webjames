/*
	$Id: wjstring.c,v 1.2 2001/09/03 22:04:09 AJW Exp $
	String handling functions for WebJames
	Warning: These are subtly different from their ANSI equivalents
*/

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*Do not include webjames.h*/
#include "wjstring.h"

#include <time.h>

#include "stat.h"
/*temporary*/
int snprintf(char *buf, size_t len, const char *format,...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret=vsprintf(buf, format, ap);
	va_end(ap);
	if (ret>len) writelog(0,"snprintf should have failed");
	return ret;
}


int vsnprintf(char *buf, size_t len, const char *format, va_list ap)
{
	int ret;
	ret=vsprintf(buf,format,ap);
	if (ret>len) writelog(0,"vsnprintf should have failed");
	return ret;
}

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
