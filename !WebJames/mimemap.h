/******************************************************************* RCS
  * $Id: mimemap.h,v 1.1 2000/08/31 12:08:43 AJW Exp $
  * File:     mimemap.h
  * Purpose:  Mimemap interface header
  * Author:   Justin Fletcher
  * Date:     03 Sep 1997
  *
  * $Log: mimemap.h,v $
  * Revision 1.1  2000/08/31 12:08:43  AJW
  * Version 0.24
  *
  * Revision 1.1  97/09/04  19:39:04  gerph
  * Initial revision
  *
  ******************************************************************/

#ifndef __MIMEMAP_H
#define __MIMEMAP_H

/* the conversion types */
#define MMM_TYPE_RISCOS         0 /* Filetype as int */
#define MMM_TYPE_RISCOS_STRING  1 /* Filetype as char* */
#define MMM_TYPE_MIME           2 /* Content type as char* */
#define MMM_TYPE_DOT_EXTN       3 /* Extention as char* */

/* some simple definitions to get around casting problems */
#define mimemap_tofiletype(x,y) \
      (int)mimemap_translate(x,y,MMM_TYPE_RISCOS,0)
#define mimemap_fromfiletype(x,y,z) \
      mimemap_translate(MMM_TYPE_RISCOS,(char *)x,y,z)

/*********************************************** <c> Gerph *********
  Function:     mimemap_translate
  Description:  Performas a mimemap_translate call and parses the result
                to be usable in C (ie return -1 if invalid)
  Parameters:   inform = input format
                indata = data, or -> buffer
                outform = output format
                outdata = pointer to buffer, or NULL if not needed
  Returns:      pointer to buffer, or data, or -1 if not valid for types
                or 0 if not valid for strings
  Note:         You'll pretty much have to cast these.
  ******************************************************************/
char *mimemap_translate(int inform,char *indata,int outform,char *outdata);

#endif
