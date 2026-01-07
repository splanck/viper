//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/ndbm.c
// Purpose: Simple database functions (ndbm) for ViperOS libc.
// Key invariants: In-memory hash table; no file persistence.
// Ownership/Lifetime: Library; database freed via dbm_close().
// Links: user/libc/include/ndbm.h
//
//===----------------------------------------------------------------------===//

/**
 * @file ndbm.c
 * @brief Simple database functions (ndbm) for ViperOS libc.
 *
 * @details
 * This file implements POSIX ndbm-style database functions:
 *
 * - dbm_open: Open (create) a database
 * - dbm_close: Close and free a database
 * - dbm_fetch: Retrieve a value by key
 * - dbm_store: Store a key-value pair
 * - dbm_delete: Delete a key
 * - dbm_firstkey/dbm_nextkey: Iterate over all keys
 * - dbm_error/dbm_clearerr: Error handling
 *
 * ViperOS implements an in-memory hash table with 256 buckets.
 * No files are created; the "file" parameter to dbm_open is ignored.
 * Data is lost when the database is closed.
 */

#include <errno.h>
#include <ndbm.h>
#include <stdlib.h>
#include <string.h>

/* Hash table entry */
struct dbm_entry
{
    datum key;
    datum value;
    struct dbm_entry *next;
};

/* Database structure */
#define DBM_HASH_SIZE 256

struct __ndbm
{
    struct dbm_entry *buckets[DBM_HASH_SIZE];
    struct dbm_entry *iter_entry; /* Current iteration entry */
    int iter_bucket;              /* Current iteration bucket */
    int flags;
    int error;
};

/* Simple hash function */
static unsigned int dbm_hash(datum key)
{
    unsigned int hash = 0;
    const unsigned char *p = key.dptr;
    for (size_t i = 0; i < key.dsize; i++)
    {
        hash = hash * 31 + p[i];
    }
    return hash % DBM_HASH_SIZE;
}

/* Compare two datums */
static int datum_eq(datum a, datum b)
{
    if (a.dsize != b.dsize)
        return 0;
    return memcmp(a.dptr, b.dptr, a.dsize) == 0;
}

/* Duplicate a datum */
static datum datum_dup(datum d)
{
    datum result;
    result.dsize = d.dsize;
    result.dptr = malloc(d.dsize);
    if (result.dptr && d.dptr)
    {
        memcpy(result.dptr, d.dptr, d.dsize);
    }
    return result;
}

DBM *dbm_open(const char *file, int open_flags, mode_t mode)
{
    (void)file; /* In-memory implementation doesn't use files */
    (void)mode;

    DBM *db = calloc(1, sizeof(DBM));
    if (!db)
    {
        errno = ENOMEM;
        return NULL;
    }

    db->flags = open_flags;
    db->error = 0;
    db->iter_bucket = -1;
    db->iter_entry = NULL;

    return db;
}

void dbm_close(DBM *db)
{
    if (!db)
        return;

    /* Free all entries */
    for (int i = 0; i < DBM_HASH_SIZE; i++)
    {
        struct dbm_entry *e = db->buckets[i];
        while (e)
        {
            struct dbm_entry *next = e->next;
            free(e->key.dptr);
            free(e->value.dptr);
            free(e);
            e = next;
        }
    }

    free(db);
}

datum dbm_fetch(DBM *db, datum key)
{
    datum result = {NULL, 0};

    if (!db || !key.dptr)
    {
        return result;
    }

    unsigned int bucket = dbm_hash(key);
    struct dbm_entry *e = db->buckets[bucket];

    while (e)
    {
        if (datum_eq(e->key, key))
        {
            /* Return pointer to stored data (not a copy) */
            result.dptr = e->value.dptr;
            result.dsize = e->value.dsize;
            return result;
        }
        e = e->next;
    }

    return result;
}

int dbm_store(DBM *db, datum key, datum content, int store_mode)
{
    if (!db || !key.dptr)
    {
        errno = EINVAL;
        db->error = 1;
        return -1;
    }

    if (db->flags == O_RDONLY)
    {
        errno = EACCES;
        db->error = 1;
        return -1;
    }

    unsigned int bucket = dbm_hash(key);
    struct dbm_entry *e = db->buckets[bucket];
    struct dbm_entry *prev = NULL;

    /* Look for existing entry */
    while (e)
    {
        if (datum_eq(e->key, key))
        {
            if (store_mode == DBM_INSERT)
            {
                return 1; /* Key exists */
            }
            /* Replace value */
            free(e->value.dptr);
            e->value = datum_dup(content);
            if (!e->value.dptr && content.dsize > 0)
            {
                db->error = 1;
                return -1;
            }
            return 0;
        }
        prev = e;
        e = e->next;
    }

    /* Insert new entry */
    e = malloc(sizeof(struct dbm_entry));
    if (!e)
    {
        db->error = 1;
        return -1;
    }

    e->key = datum_dup(key);
    e->value = datum_dup(content);
    e->next = NULL;

    if (!e->key.dptr || (!e->value.dptr && content.dsize > 0))
    {
        free(e->key.dptr);
        free(e->value.dptr);
        free(e);
        db->error = 1;
        return -1;
    }

    if (prev)
    {
        prev->next = e;
    }
    else
    {
        db->buckets[bucket] = e;
    }

    return 0;
}

int dbm_delete(DBM *db, datum key)
{
    if (!db || !key.dptr)
    {
        errno = EINVAL;
        return -1;
    }

    if (db->flags == O_RDONLY)
    {
        errno = EACCES;
        return -1;
    }

    unsigned int bucket = dbm_hash(key);
    struct dbm_entry *e = db->buckets[bucket];
    struct dbm_entry *prev = NULL;

    while (e)
    {
        if (datum_eq(e->key, key))
        {
            if (prev)
            {
                prev->next = e->next;
            }
            else
            {
                db->buckets[bucket] = e->next;
            }
            free(e->key.dptr);
            free(e->value.dptr);
            free(e);
            return 0;
        }
        prev = e;
        e = e->next;
    }

    return -1; /* Key not found */
}

datum dbm_firstkey(DBM *db)
{
    datum result = {NULL, 0};

    if (!db)
        return result;

    /* Reset iteration */
    db->iter_bucket = 0;
    db->iter_entry = NULL;

    /* Find first non-empty bucket */
    while (db->iter_bucket < DBM_HASH_SIZE)
    {
        if (db->buckets[db->iter_bucket])
        {
            db->iter_entry = db->buckets[db->iter_bucket];
            result.dptr = db->iter_entry->key.dptr;
            result.dsize = db->iter_entry->key.dsize;
            return result;
        }
        db->iter_bucket++;
    }

    return result;
}

datum dbm_nextkey(DBM *db)
{
    datum result = {NULL, 0};

    if (!db || db->iter_bucket < 0)
        return result;

    /* Move to next entry */
    if (db->iter_entry)
    {
        db->iter_entry = db->iter_entry->next;
    }

    /* If no next entry, move to next bucket */
    while (!db->iter_entry && db->iter_bucket < DBM_HASH_SIZE - 1)
    {
        db->iter_bucket++;
        db->iter_entry = db->buckets[db->iter_bucket];
    }

    if (db->iter_entry)
    {
        result.dptr = db->iter_entry->key.dptr;
        result.dsize = db->iter_entry->key.dsize;
    }

    return result;
}

int dbm_error(DBM *db)
{
    return db ? db->error : 1;
}

int dbm_clearerr(DBM *db)
{
    if (db)
        db->error = 0;
    return 0;
}

int dbm_dirfno(DBM *db)
{
    (void)db;
    return -1; /* Not using files */
}

int dbm_pagfno(DBM *db)
{
    (void)db;
    return -1; /* Not using files */
}

int dbm_rdonly(DBM *db)
{
    return (db && db->flags == O_RDONLY) ? 1 : 0;
}
