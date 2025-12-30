#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EOF (-1)

/* Buffering modes for setvbuf */
#define _IOFBF 0    /* Full buffering */
#define _IOLBF 1    /* Line buffering */
#define _IONBF 2    /* No buffering */

/* Default buffer size */
#define BUFSIZ 512

/* Minimal FILE abstraction for freestanding environment */
typedef struct _FILE FILE;

/* Standard streams - defined as constants for freestanding */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Variadic argument support */
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)
#define va_copy(d, s) __builtin_va_copy(d, s)

/* Formatted output */
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

/* Variadic formatted output */
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* Formatted input */
int sscanf(const char *str, const char *format, ...);

/* Character output */
int puts(const char *s);
int fputs(const char *s, FILE *stream);
int putchar(int c);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);

/* Character input */
int getchar(void);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);

/* Error handling */
int ferror(FILE *stream);
void clearerr(FILE *stream);
int feof(FILE *stream);

/* Flushing */
int fflush(FILE *stream);

/* Buffering control */
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);
void setlinebuf(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
