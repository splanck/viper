/*
 * ViperOS libc - search.h
 * Hash and tree search functions
 */

#ifndef _SEARCH_H
#define _SEARCH_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hash table entry */
typedef struct entry {
    char *key;
    void *data;
} ENTRY;

/* Action for hash/tree operations */
typedef enum {
    FIND,       /* Find existing entry */
    ENTER       /* Enter new entry if not found */
} ACTION;

/* Visit order for tree walk */
typedef enum {
    preorder,   /* Before visiting children */
    postorder,  /* After visiting both children */
    endorder,   /* After visiting node entirely (leaf visits once) */
    leaf        /* Node is a leaf */
} VISIT;

/* ============================================================
 * Hash table functions (global hash table)
 * ============================================================ */

/*
 * hcreate - Create hash table
 *
 * Creates a hash table with at least 'nel' entries.
 * Returns non-zero on success, 0 on failure.
 */
int hcreate(size_t nel);

/*
 * hdestroy - Destroy hash table
 *
 * Frees resources used by the hash table created by hcreate().
 * Does not free keys or data - caller is responsible.
 */
void hdestroy(void);

/*
 * hsearch - Search hash table
 *
 * If action is FIND, searches for item.key and returns pointer to entry.
 * If action is ENTER, inserts item if not found.
 * Returns NULL if not found (FIND) or table is full (ENTER).
 */
ENTRY *hsearch(ENTRY item, ACTION action);

/* ============================================================
 * Hash table functions (re-entrant, POSIX extension)
 * ============================================================ */

/* Opaque hash table data type */
struct hsearch_data {
    void *table;
    size_t size;
    size_t filled;
};

/*
 * hcreate_r - Create hash table (reentrant)
 */
int hcreate_r(size_t nel, struct hsearch_data *htab);

/*
 * hdestroy_r - Destroy hash table (reentrant)
 */
void hdestroy_r(struct hsearch_data *htab);

/*
 * hsearch_r - Search hash table (reentrant)
 *
 * Result is stored in *retval.
 * Returns non-zero on success, 0 on failure.
 */
int hsearch_r(ENTRY item, ACTION action, ENTRY **retval,
              struct hsearch_data *htab);

/* ============================================================
 * Binary search tree functions
 * ============================================================ */

/*
 * tsearch - Insert element in tree
 *
 * Searches for key in tree rooted at *rootp.
 * If not found, inserts key and returns pointer to new node.
 * If found, returns pointer to existing node.
 * Returns NULL on allocation failure.
 *
 * compar(a, b) should return:
 *   < 0 if a < b
 *   = 0 if a == b
 *   > 0 if a > b
 */
void *tsearch(const void *key, void **rootp,
              int (*compar)(const void *, const void *));

/*
 * tfind - Find element in tree
 *
 * Like tsearch, but does not insert if not found.
 * Returns NULL if not found.
 */
void *tfind(const void *key, void *const *rootp,
            int (*compar)(const void *, const void *));

/*
 * tdelete - Delete element from tree
 *
 * Returns pointer to parent of deleted node, or NULL if not found.
 * Root deletion returns undefined value.
 */
void *tdelete(const void *key, void **rootp,
              int (*compar)(const void *, const void *));

/*
 * twalk - Walk tree in-order
 *
 * Calls action for each node in the tree.
 * action(nodep, visit, depth) where:
 *   nodep = pointer to node data
 *   visit = when the node is being visited
 *   depth = depth in tree (root = 0)
 */
void twalk(const void *root,
           void (*action)(const void *nodep, VISIT which, int depth));

/*
 * twalk_r - Walk tree with user data (GNU extension)
 *
 * Like twalk, but passes closure to action.
 */
void twalk_r(const void *root,
             void (*action)(const void *nodep, VISIT which, void *closure),
             void *closure);

/*
 * tdestroy - Destroy tree (GNU extension)
 *
 * Frees all nodes in the tree.
 * free_node is called for each node's data.
 */
void tdestroy(void *root, void (*free_node)(void *nodep));

/* ============================================================
 * Linear search function
 * ============================================================ */

/*
 * lfind - Linear search
 *
 * Searches base[0..nmemb-1] for key using compar.
 * Returns pointer to matching element, or NULL if not found.
 */
void *lfind(const void *key, const void *base,
            size_t *nmemb, size_t size,
            int (*compar)(const void *, const void *));

/*
 * lsearch - Linear search with insert
 *
 * Like lfind, but appends key to array if not found.
 * *nmemb is incremented if element is added.
 */
void *lsearch(const void *key, void *base,
              size_t *nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* ============================================================
 * Sorted array functions
 * ============================================================ */

/*
 * insque - Insert element in queue/list
 *
 * element->next and element->prev are set.
 * If pred is NULL, creates new list.
 */
void insque(void *element, void *pred);

/*
 * remque - Remove element from queue/list
 */
void remque(void *element);

#ifdef __cplusplus
}
#endif

#endif /* _SEARCH_H */
