/*
 * ViperOS C Library - utmpx implementation
 * User accounting database stubs
 */

#include <utmpx.h>
#include <string.h>
#include <errno.h>

/* Static entry for returns */
static struct utmpx utmpx_entry;

/* In-memory "database" - single entry for now */
static struct utmpx utmpx_db[16];
static int utmpx_count = 0;
static int utmpx_pos = 0;
static int utmpx_open = 0;

void setutxent(void) {
    utmpx_pos = 0;
    utmpx_open = 1;
}

void endutxent(void) {
    utmpx_open = 0;
    utmpx_pos = 0;
}

struct utmpx *getutxent(void) {
    if (!utmpx_open || utmpx_pos >= utmpx_count) {
        return NULL;
    }

    memcpy(&utmpx_entry, &utmpx_db[utmpx_pos], sizeof(struct utmpx));
    utmpx_pos++;
    return &utmpx_entry;
}

struct utmpx *getutxid(const struct utmpx *id) {
    if (!id) return NULL;

    for (int i = utmpx_pos; i < utmpx_count; i++) {
        struct utmpx *e = &utmpx_db[i];

        /* Match by type and ID */
        switch (id->ut_type) {
        case RUN_LVL:
        case BOOT_TIME:
        case NEW_TIME:
        case OLD_TIME:
            if (e->ut_type == id->ut_type) {
                utmpx_pos = i + 1;
                memcpy(&utmpx_entry, e, sizeof(struct utmpx));
                return &utmpx_entry;
            }
            break;

        case INIT_PROCESS:
        case LOGIN_PROCESS:
        case USER_PROCESS:
        case DEAD_PROCESS:
            if ((e->ut_type == INIT_PROCESS ||
                 e->ut_type == LOGIN_PROCESS ||
                 e->ut_type == USER_PROCESS ||
                 e->ut_type == DEAD_PROCESS) &&
                memcmp(e->ut_id, id->ut_id, sizeof(e->ut_id)) == 0) {
                utmpx_pos = i + 1;
                memcpy(&utmpx_entry, e, sizeof(struct utmpx));
                return &utmpx_entry;
            }
            break;
        }
    }

    return NULL;
}

struct utmpx *getutxline(const struct utmpx *line) {
    if (!line) return NULL;

    for (int i = utmpx_pos; i < utmpx_count; i++) {
        struct utmpx *e = &utmpx_db[i];

        if ((e->ut_type == USER_PROCESS || e->ut_type == LOGIN_PROCESS) &&
            strncmp(e->ut_line, line->ut_line, __UT_LINESIZE) == 0) {
            utmpx_pos = i + 1;
            memcpy(&utmpx_entry, e, sizeof(struct utmpx));
            return &utmpx_entry;
        }
    }

    return NULL;
}

struct utmpx *pututxline(const struct utmpx *utmpx) {
    if (!utmpx) {
        errno = EINVAL;
        return NULL;
    }

    /* Look for existing entry to update */
    for (int i = 0; i < utmpx_count; i++) {
        struct utmpx *e = &utmpx_db[i];

        /* Match by ID for process types */
        if ((utmpx->ut_type == INIT_PROCESS ||
             utmpx->ut_type == LOGIN_PROCESS ||
             utmpx->ut_type == USER_PROCESS ||
             utmpx->ut_type == DEAD_PROCESS) &&
            (e->ut_type == INIT_PROCESS ||
             e->ut_type == LOGIN_PROCESS ||
             e->ut_type == USER_PROCESS ||
             e->ut_type == DEAD_PROCESS) &&
            memcmp(e->ut_id, utmpx->ut_id, sizeof(e->ut_id)) == 0) {
            memcpy(e, utmpx, sizeof(struct utmpx));
            memcpy(&utmpx_entry, e, sizeof(struct utmpx));
            return &utmpx_entry;
        }
    }

    /* Add new entry */
    if (utmpx_count < 16) {
        memcpy(&utmpx_db[utmpx_count], utmpx, sizeof(struct utmpx));
        memcpy(&utmpx_entry, &utmpx_db[utmpx_count], sizeof(struct utmpx));
        utmpx_count++;
        return &utmpx_entry;
    }

    errno = ENOSPC;
    return NULL;
}

void updwtmpx(const char *file, const struct utmpx *utmpx) {
    /* Stub - would write to wtmpx file */
    (void)file;
    (void)utmpx;
}

int utmpxname(const char *file) {
    /* Stub - would set the utmpx file path */
    (void)file;
    return 0;
}
