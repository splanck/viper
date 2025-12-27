#ifndef _UNISTD_H
#define _UNISTD_H

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

typedef int pid_t;
typedef unsigned int useconds_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Only define if not already defined (e.g., by syscall.hpp) */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* File operations */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
long lseek(int fd, long offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

/* Process operations */
pid_t getpid(void);
pid_t getppid(void);

/* Memory operations */
void *sbrk(long increment);

/* Sleep operations */
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);

/* Working directory */
char *getcwd(char *buf, size_t size);
int chdir(const char *path);

/* System info */
int isatty(int fd);
long sysconf(int name);

/* sysconf names */
#define _SC_CLK_TCK 2
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
