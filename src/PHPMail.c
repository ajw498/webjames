/*
	PHPMail
	Copyright © Alex Waugh 2001

	$Id: PHPMail.c,v 1.1 2002/02/17 22:50:10 ajw Exp $

	PHPMail provides a wrapper for Justin Fletcher's GMail to allow it to be called from PHP
	It should be linked with a build of UnixLib that is more recent than 16/8/2001

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>

#include "oslib/os.h"


#define IPFILE "<Wimp$ScrapDir>.phpmail"
#define OPFILE "null:"

#define SYS_VAR "PHP$MailOutput"

#define BUF_SIZE 1024


int main(void)
{
	char str[BUF_SIZE];
	char output[BUF_SIZE]=OPFILE;
	FILE *file;
	int ret, used;

	/*Copy all of stdin to a temporary file*/
	/*This needs to be done, as GMail is not compiled with UnixLib, and so won't realise it is being piped to*/
	file=fopen(IPFILE,"w");
	if (file==NULL) return 1; /*We can't really do much if this fails*/
	while (fgets(str,BUF_SIZE,stdin)!=NULL) fputs(str,file);
	fclose(file);

	/*Let the user override the default output file (null:) for debugging*/
	if (xos_read_var_val(SYS_VAR,str,BUF_SIZE,0,os_VARTYPE_STRING,&used,NULL,NULL)==NULL) {
		str[used]='\0';
		strcpy(output,str);
	}

	sprintf(str,"mail -x < " IPFILE " > %s",output);
	ret=system(str);
	remove(IPFILE);
	return ret;
}

