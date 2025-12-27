#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long size_t;

#define NULL ((void *)0)
#define EOF (-1)

int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

int puts(const char *s);
int putchar(int c);
int getchar(void);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
