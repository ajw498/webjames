
#ifndef WJSTRING_H
#define WJSTRING_H

#include <stddef.h>

char *wjstrncpy(char *dest, const char *src, const size_t n);

char *wjstrncat(char *dest, const char *src, const size_t n);

int wjstrnicmp(char *s1, char *s2,size_t n);

#endif
