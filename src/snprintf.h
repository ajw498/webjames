#ifndef SNPRINTF_H
#define SNPRINTF_H

#if defined(__CC_NORCROFT) && __CC_NORCROFT_VERSION >= 552
#define HAVE_SNPRINTF
#define HAVE_VSNPRINTF
#endif

#if defined(__GNUC__) && !defined(__TARGET_SCL__)
#define HAVE_SNPRINTF
#define HAVE_VSNPRINTF
#endif


#if !defined(HAVE_SNPRINTF) || !defined(HAVE_VSNPRINTF)

int snprintf(char *buf, size_t len, const char *format,...);
int vsnprintf(char *buf, size_t len, const char *format, va_list ap);

#endif

#endif
