/*
 * ViperDOS C Library - ndbm.h
 * Database functions (NDBM-compatible)
 */

#ifndef _NDBM_H
#define _NDBM_H

#include <fcntl.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Data structure used for key and content.
 */
typedef struct {
    void *dptr;   /* Pointer to data */
    size_t dsize; /* Size of data */
} datum;

/*
 * Database descriptor (opaque type).
 */
typedef struct __ndbm DBM;

/*
 * Open modes (in addition to O_RDONLY, O_RDWR, O_CREAT from fcntl.h)
 */
#define DBM_INSERT 0  /* Insert only if key doesn't exist */
#define DBM_REPLACE 1 /* Replace if key exists */

/*
 * Open a database.
 * file: Base filename (implementation adds extensions)
 * open_flags: O_RDONLY, O_RDWR, O_CREAT, etc.
 * mode: File creation mode
 * Returns: Database handle, or NULL on error
 */
DBM *dbm_open(const char *file, int open_flags, mode_t mode);

/*
 * Close a database.
 */
void dbm_close(DBM *db);

/*
 * Fetch a record.
 * Returns: datum with data, or datum with dptr=NULL if not found
 */
datum dbm_fetch(DBM *db, datum key);

/*
 * Store a record.
 * store_mode: DBM_INSERT or DBM_REPLACE
 * Returns: 0 on success, -1 on error, 1 if key exists (DBM_INSERT mode)
 */
int dbm_store(DBM *db, datum key, datum content, int store_mode);

/*
 * Delete a record.
 * Returns: 0 on success, -1 on error
 */
int dbm_delete(DBM *db, datum key);

/*
 * Get the first key in the database.
 * Returns: datum with key, or datum with dptr=NULL if empty
 */
datum dbm_firstkey(DBM *db);

/*
 * Get the next key in the database.
 * Returns: datum with key, or datum with dptr=NULL if no more
 */
datum dbm_nextkey(DBM *db);

/*
 * Check if database encountered an error.
 * Returns: Non-zero if error occurred
 */
int dbm_error(DBM *db);

/*
 * Clear any error condition.
 * Returns: 0 (always)
 */
int dbm_clearerr(DBM *db);

/*
 * Get the file descriptor for the database directory file.
 * Returns: File descriptor, or -1 on error
 */
int dbm_dirfno(DBM *db);

/*
 * Get the file descriptor for the database page file.
 * Returns: File descriptor, or -1 on error
 */
int dbm_pagfno(DBM *db);

/*
 * Get read-only file descriptor (if supported).
 * Returns: File descriptor, or -1 on error
 */
int dbm_rdonly(DBM *db);

#ifdef __cplusplus
}
#endif

#endif /* _NDBM_H */
