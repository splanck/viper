#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long size_t;

#define NULL ((void *)0)

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void exit(int status);
void abort(void);

int atoi(const char *nptr);
long atol(const char *nptr);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
