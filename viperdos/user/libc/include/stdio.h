#ifndef _STDIO_H
#define _STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EOF (-1)

/* Buffering modes for setvbuf */
#define _IOFBF 0 /* Full buffering */
#define _IOLBF 1 /* Line buffering */
#define _IONBF 2 /* No buffering */

/* Default buffer size */
#define BUFSIZ 512

/* fseek whence values */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* File open modes */
#define FOPEN_MAX 20
#define FILENAME_MAX 256
#define L_tmpnam 20
#define TMP_MAX 10000

/* Minimal FILE abstraction for freestanding environment */
typedef struct _FILE FILE;

/* Off_t type for file offsets */
typedef long fpos_t;

/* Standard streams - defined as constants for freestanding */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Variadic argument support */
#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#endif
#ifndef va_start
#define va_start(v, l) __builtin_va_start(v, l)
#endif
#ifndef va_end
#define va_end(v) __builtin_va_end(v)
#endif
#ifndef va_arg
#define va_arg(v, l) __builtin_va_arg(v, l)
#endif
#ifndef va_copy
#define va_copy(d, s) __builtin_va_copy(d, s)
#endif

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

/* File operations */
FILE *fopen(const char *pathname, const char *mode);
FILE *freopen(const char *pathname, const char *mode, FILE *stream);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fileno(FILE *stream);

/* Binary I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* File positioning */
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

/* Unget character */
int ungetc(int c, FILE *stream);

/* Error output */
void perror(const char *s);

/* File removal/renaming */
int remove(const char *pathname);
int rename_file(const char *oldpath, const char *newpath);

/* Temporary files */
char *tmpnam(char *s);
FILE *tmpfile(void);

/* Get line (POSIX extension) */
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
