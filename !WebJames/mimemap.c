/******************************************************************* RCS
 * $Id: mimemap.c,v 1.1 2000/08/31 12:08:42 AJW Exp $
 * File:     mimemap.c
 * Purpose:  Mimemap interface
 * Author:   Justin Fletcher
 *
 * $Log: mimemap.c,v $
 * Revision 1.1  2000/08/31 12:08:42  AJW
 * Version 0.24
 *
 ******************************************************************/

#include "mimemap.h"
#include "kernel.h"

#define MimeMap_Translate     0x50B00

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
char *mimemap_translate(int inform,char *indata,int outform,char *outdata) {

  _kernel_swi_regs ARM;
  ARM.r[0]=inform;
  ARM.r[1]=(int)indata;
  ARM.r[2]=(int)outform;
  ARM.r[3]=(int)outdata;
  if (_kernel_swi(MimeMap_Translate,&ARM,&ARM))   return (char *)-1;

  return (char *)ARM.r[3];
}
