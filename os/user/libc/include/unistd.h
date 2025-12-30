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

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef long off_t;
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
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int close(int fd);
long lseek(int fd, long offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int fsync(int fd);
int fdatasync(int fd);
void sync(void);

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

/* File access testing */
#define F_OK 0  /* Test for existence */
#define R_OK 4  /* Test for read permission */
#define W_OK 2  /* Test for write permission */
#define X_OK 1  /* Test for execute permission */

int access(const char *pathname, int mode);

/* File/directory operations */
int unlink(const char *pathname);
int rmdir(const char *pathname);
int mkdir(const char *pathname, unsigned int mode);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

/* Host/user info */
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

typedef unsigned int uid_t;
typedef unsigned int gid_t;

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

/* Process group */
pid_t getpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);

/* Pipe */
int pipe(int pipefd[2]);

/* Execute */
int execv(const char *pathname, char *const argv[]);
int execve(const char *pathname, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);

/* Fork */
pid_t fork(void);

/* Misc */
int truncate(const char *path, long length);
int ftruncate(int fd, long length);
int fsync(int fd);
long pathconf(const char *path, int name);
long fpathconf(int fd, int name);
unsigned int alarm(unsigned int seconds);
int pause(void);

/*
 * Command-line option parsing (getopt)
 */

/* Global variables for getopt */
extern char *optarg;        /* Argument to current option */
extern int optind;          /* Index of next argument to process */
extern int opterr;          /* If 0, suppress error messages */
extern int optopt;          /* Current option character */

/*
 * getopt - Parse command-line options
 *
 * Parses short options (-x) from argc/argv.
 * optstring contains valid option characters; a colon after a character
 * means that option requires an argument.
 *
 * Returns the option character, '?' for unknown options,
 * ':' for missing arguments (if optstring starts with ':'),
 * or -1 when all options have been processed.
 */
int getopt(int argc, char * const argv[], const char *optstring);

/*
 * getopt_long - Parse long command-line options
 *
 * Parses both short (-x) and long (--name) options.
 */
struct option {
    const char *name;   /* Long option name */
    int has_arg;        /* no_argument, required_argument, optional_argument */
    int *flag;          /* If non-NULL, set *flag to val when option found */
    int val;            /* Value to return (or store in *flag) */
};

#define no_argument        0
#define required_argument  1
#define optional_argument  2

int getopt_long(int argc, char * const argv[],
                const char *optstring,
                const struct option *longopts, int *longindex);

int getopt_long_only(int argc, char * const argv[],
                     const char *optstring,
                     const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
