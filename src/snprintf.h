#ifndef SNPRINTF_H
#define SNPRINTF_H


int snprintf(char *buf, size_t len, const char *format,...);
int vsnprintf(char *buf, size_t len, const char *format, va_list ap);

#endif
