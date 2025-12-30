/*
 * ViperOS C Library - utmpx.h
 * User accounting database
 */

#ifndef _UTMPX_H
#define _UTMPX_H

#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size limits */
#define __UT_LINESIZE   32
#define __UT_NAMESIZE   32
#define __UT_HOSTSIZE   256

/* Entry types */
#define EMPTY           0   /* No valid user accounting information */
#define RUN_LVL         1   /* The system's runlevel */
#define BOOT_TIME       2   /* Time of system boot */
#define NEW_TIME        3   /* Time after system clock change */
#define OLD_TIME        4   /* Time before system clock change */
#define INIT_PROCESS    5   /* Process spawned by init */
#define LOGIN_PROCESS   6   /* Session leader for a logged-in user */
#define USER_PROCESS    7   /* Normal process */
#define DEAD_PROCESS    8   /* Terminated process */

/* utmpx entry structure */
struct utmpx {
    char            ut_user[__UT_NAMESIZE]; /* Username */
    char            ut_id[4];               /* Inittab ID */
    char            ut_line[__UT_LINESIZE]; /* Device name (tty) */
    pid_t           ut_pid;                 /* Process ID */
    short           ut_type;                /* Type of entry */
    struct timeval  ut_tv;                  /* Time entry was made */
    char            ut_host[__UT_HOSTSIZE]; /* Hostname for remote login */
    /* Implementation-specific fields */
    int             __ut_pad[4];
};

/*
 * Open the user accounting database.
 */
void setutxent(void);

/*
 * Close the user accounting database.
 */
void endutxent(void);

/*
 * Read the next entry from the database.
 * Returns pointer to static utmpx structure, or NULL at end.
 */
struct utmpx *getutxent(void);

/*
 * Search for an entry by ID.
 * Returns pointer to static utmpx structure, or NULL if not found.
 */
struct utmpx *getutxid(const struct utmpx *id);

/*
 * Search for an entry by line (tty).
 * Returns pointer to static utmpx structure, or NULL if not found.
 */
struct utmpx *getutxline(const struct utmpx *line);

/*
 * Write an entry to the database.
 * Returns pointer to written entry, or NULL on error.
 */
struct utmpx *pututxline(const struct utmpx *utmpx);

/* Non-standard but common extensions */

/*
 * Update the wtmp file (login history).
 */
void updwtmpx(const char *file, const struct utmpx *utmpx);

/*
 * Get the pathname of the utmpx file.
 */
int utmpxname(const char *file);

#ifdef __cplusplus
}
#endif

#endif /* _UTMPX_H */
