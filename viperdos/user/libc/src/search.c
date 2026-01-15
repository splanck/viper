//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/search.c
// Purpose: Search and data structure functions for ViperDOS libc.
// Key invariants: Hash uses linear probing; BST is unbalanced.
// Ownership/Lifetime: Library; dynamic allocation for trees/tables.
// Links: user/libc/include/search.h
//
//===----------------------------------------------------------------------===//

/**
 * @file search.c
 * @brief Search and data structure functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX search functions:
 *
 * Hash Tables:
 * - hcreate/hdestroy: Create/destroy global hash table
 * - hsearch: Search/insert in global hash table
 * - hcreate_r/hdestroy_r/hsearch_r: Reentrant versions
 *
 * Binary Search Trees:
 * - tsearch/tfind: Insert/find in BST
 * - tdelete: Delete from BST
 * - twalk/twalk_r: Walk tree in-order
 * - tdestroy: Destroy entire tree
 *
 * Linear Search:
 * - lfind/lsearch: Linear search (with optional insert)
 *
 * Queues:
 * - insque/remque: Doubly-linked list operations
 */

#include "../include/search.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* ============================================================
 * Hash table implementation
 * ============================================================ */

/* Hash entry for table */
typedef struct hash_entry
{
    int used;
    ENTRY entry;
} hash_entry;

/* Global hash table */
static hash_entry *global_table = NULL;
static size_t global_size = 0;
static size_t global_filled = 0;

/* Simple string hash function */
static unsigned long hash_string(const char *key)
{
    unsigned long h = 5381;
    int c;

    while ((c = (unsigned char)*key++))
    {
        h = ((h << 5) + h) + c; /* h * 33 + c */
    }

    return h;
}

/*
 * hcreate - Create global hash table
 */
int hcreate(size_t nel)
{
    /* Free existing table */
    if (global_table)
    {
        free(global_table);
    }

    /* Size should be a prime for better distribution */
    /* Use a simple prime larger than nel */
    size_t size = nel;
    if (size < 7)
        size = 7;

    global_table = (hash_entry *)calloc(size, sizeof(hash_entry));
    if (!global_table)
    {
        return 0;
    }

    global_size = size;
    global_filled = 0;
    return 1;
}

/*
 * hdestroy - Destroy global hash table
 */
void hdestroy(void)
{
    if (global_table)
    {
        free(global_table);
        global_table = NULL;
        global_size = 0;
        global_filled = 0;
    }
}

/*
 * hsearch - Search global hash table
 */
ENTRY *hsearch(ENTRY item, ACTION action)
{
    if (!global_table || !item.key)
    {
        return NULL;
    }

    unsigned long h = hash_string(item.key);
    size_t idx = h % global_size;
    size_t start = idx;

    /* Linear probing */
    do
    {
        hash_entry *ent = &global_table[idx];

        if (!ent->used)
        {
            /* Empty slot */
            if (action == FIND)
            {
                return NULL;
            }
            /* ENTER - check if table is full */
            if (global_filled >= global_size)
            {
                return NULL;
            }
            ent->used = 1;
            ent->entry = item;
            global_filled++;
            return &ent->entry;
        }

        /* Check if key matches */
        if (strcmp(ent->entry.key, item.key) == 0)
        {
            return &ent->entry;
        }

        idx = (idx + 1) % global_size;
    } while (idx != start);

    return NULL; /* Table full or not found */
}

/*
 * hcreate_r - Create reentrant hash table
 */
int hcreate_r(size_t nel, struct hsearch_data *htab)
{
    if (!htab)
    {
        return 0;
    }

    size_t size = nel;
    if (size < 7)
        size = 7;

    htab->table = calloc(size, sizeof(hash_entry));
    if (!htab->table)
    {
        return 0;
    }

    htab->size = size;
    htab->filled = 0;
    return 1;
}

/*
 * hdestroy_r - Destroy reentrant hash table
 */
void hdestroy_r(struct hsearch_data *htab)
{
    if (htab && htab->table)
    {
        free(htab->table);
        htab->table = NULL;
        htab->size = 0;
        htab->filled = 0;
    }
}

/*
 * hsearch_r - Search reentrant hash table
 */
int hsearch_r(ENTRY item, ACTION action, ENTRY **retval, struct hsearch_data *htab)
{
    if (!htab || !htab->table || !item.key || !retval)
    {
        if (retval)
            *retval = NULL;
        return 0;
    }

    hash_entry *table = (hash_entry *)htab->table;
    unsigned long h = hash_string(item.key);
    size_t idx = h % htab->size;
    size_t start = idx;

    do
    {
        hash_entry *ent = &table[idx];

        if (!ent->used)
        {
            if (action == FIND)
            {
                *retval = NULL;
                return 0;
            }
            if (htab->filled >= htab->size)
            {
                *retval = NULL;
                return 0;
            }
            ent->used = 1;
            ent->entry = item;
            htab->filled++;
            *retval = &ent->entry;
            return 1;
        }

        if (strcmp(ent->entry.key, item.key) == 0)
        {
            *retval = &ent->entry;
            return 1;
        }

        idx = (idx + 1) % htab->size;
    } while (idx != start);

    *retval = NULL;
    return 0;
}

/* ============================================================
 * Binary search tree implementation
 * ============================================================ */

/* Tree node structure */
typedef struct tree_node
{
    const void *key;
    struct tree_node *left;
    struct tree_node *right;
} tree_node;

/*
 * tsearch - Insert element in tree
 */
void *tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    tree_node **node;
    tree_node *new_node;

    if (!rootp || !compar)
    {
        return NULL;
    }

    node = (tree_node **)rootp;

    while (*node)
    {
        int cmp = compar(key, (*node)->key);
        if (cmp == 0)
        {
            /* Found - return pointer to node */
            return *node;
        }
        if (cmp < 0)
        {
            node = &(*node)->left;
        }
        else
        {
            node = &(*node)->right;
        }
    }

    /* Not found - insert new node */
    new_node = (tree_node *)malloc(sizeof(tree_node));
    if (!new_node)
    {
        return NULL;
    }

    new_node->key = key;
    new_node->left = NULL;
    new_node->right = NULL;
    *node = new_node;

    return new_node;
}

/*
 * tfind - Find element in tree
 */
void *tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *))
{
    tree_node *const *node;

    if (!rootp || !compar)
    {
        return NULL;
    }

    node = (tree_node *const *)rootp;

    while (*node)
    {
        int cmp = compar(key, (*node)->key);
        if (cmp == 0)
        {
            return (void *)*node;
        }
        if (cmp < 0)
        {
            node = &(*node)->left;
        }
        else
        {
            node = &(*node)->right;
        }
    }

    return NULL;
}

/*
 * tdelete - Delete element from tree
 */
void *tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    tree_node **node;
    tree_node *parent = NULL;

    if (!rootp || !compar)
    {
        return NULL;
    }

    node = (tree_node **)rootp;

    /* Find the node */
    while (*node)
    {
        int cmp = compar(key, (*node)->key);
        if (cmp == 0)
        {
            break;
        }
        parent = *node;
        if (cmp < 0)
        {
            node = &(*node)->left;
        }
        else
        {
            node = &(*node)->right;
        }
    }

    if (!*node)
    {
        return NULL; /* Not found */
    }

    tree_node *to_delete = *node;

    if (!to_delete->left)
    {
        /* No left child - replace with right child */
        *node = to_delete->right;
    }
    else if (!to_delete->right)
    {
        /* No right child - replace with left child */
        *node = to_delete->left;
    }
    else
    {
        /* Two children - find in-order successor */
        tree_node **succ = &to_delete->right;
        while ((*succ)->left)
        {
            succ = &(*succ)->left;
        }

        tree_node *succ_node = *succ;
        *succ = succ_node->right;

        succ_node->left = to_delete->left;
        succ_node->right = to_delete->right;
        *node = succ_node;
    }

    free(to_delete);
    return parent;
}

/*
 * twalk_helper - Recursive tree walk
 */
static void twalk_helper(const tree_node *node, void (*action)(const void *, VISIT, int), int depth)
{
    if (!node)
    {
        return;
    }

    if (!node->left && !node->right)
    {
        /* Leaf node */
        action(node, leaf, depth);
    }
    else
    {
        /* Internal node */
        action(node, preorder, depth);
        twalk_helper(node->left, action, depth + 1);
        action(node, postorder, depth);
        twalk_helper(node->right, action, depth + 1);
        action(node, endorder, depth);
    }
}

/*
 * twalk - Walk tree in-order
 */
void twalk(const void *root, void (*action)(const void *nodep, VISIT which, int depth))
{
    if (root && action)
    {
        twalk_helper((const tree_node *)root, action, 0);
    }
}

/*
 * twalk_r_helper - Recursive tree walk with closure
 */
static void twalk_r_helper(const tree_node *node,
                           void (*action)(const void *, VISIT, void *),
                           void *closure,
                           int depth)
{
    (void)depth;

    if (!node)
    {
        return;
    }

    if (!node->left && !node->right)
    {
        action(node, leaf, closure);
    }
    else
    {
        action(node, preorder, closure);
        twalk_r_helper(node->left, action, closure, depth + 1);
        action(node, postorder, closure);
        twalk_r_helper(node->right, action, closure, depth + 1);
        action(node, endorder, closure);
    }
}

/*
 * twalk_r - Walk tree with user data
 */
void twalk_r(const void *root,
             void (*action)(const void *nodep, VISIT which, void *closure),
             void *closure)
{
    if (root && action)
    {
        twalk_r_helper((const tree_node *)root, action, closure, 0);
    }
}

/*
 * tdestroy - Destroy tree
 */
void tdestroy(void *root, void (*free_node)(void *nodep))
{
    tree_node *node = (tree_node *)root;

    if (!node)
    {
        return;
    }

    tdestroy(node->left, free_node);
    tdestroy(node->right, free_node);

    if (free_node)
    {
        free_node((void *)node->key);
    }
    free(node);
}

/* ============================================================
 * Linear search implementation
 * ============================================================ */

/*
 * lfind - Linear search
 */
void *lfind(const void *key,
            const void *base,
            size_t *nmemb,
            size_t size,
            int (*compar)(const void *, const void *))
{
    if (!key || !base || !nmemb || !compar || size == 0)
    {
        return NULL;
    }

    const char *p = (const char *)base;
    for (size_t i = 0; i < *nmemb; i++)
    {
        if (compar(key, p) == 0)
        {
            return (void *)p;
        }
        p += size;
    }

    return NULL;
}

/*
 * lsearch - Linear search with insert
 */
void *lsearch(const void *key,
              void *base,
              size_t *nmemb,
              size_t size,
              int (*compar)(const void *, const void *))
{
    void *found = lfind(key, base, nmemb, size, compar);
    if (found)
    {
        return found;
    }

    /* Not found - append */
    char *dest = (char *)base + (*nmemb) * size;
    memcpy(dest, key, size);
    (*nmemb)++;

    return dest;
}

/* ============================================================
 * Queue functions
 * ============================================================ */

/* Queue element structure for insque/remque */
struct qelem
{
    struct qelem *q_forw;
    struct qelem *q_back;
};

/*
 * insque - Insert element in queue
 */
void insque(void *element, void *pred)
{
    struct qelem *elem = (struct qelem *)element;
    struct qelem *p = (struct qelem *)pred;

    if (!elem)
    {
        return;
    }

    if (!p)
    {
        /* Create new circular list */
        elem->q_forw = elem;
        elem->q_back = elem;
    }
    else
    {
        /* Insert after pred */
        elem->q_forw = p->q_forw;
        elem->q_back = p;
        if (p->q_forw)
        {
            p->q_forw->q_back = elem;
        }
        p->q_forw = elem;
    }
}

/*
 * remque - Remove element from queue
 */
void remque(void *element)
{
    struct qelem *elem = (struct qelem *)element;

    if (!elem)
    {
        return;
    }

    if (elem->q_back)
    {
        elem->q_back->q_forw = elem->q_forw;
    }
    if (elem->q_forw)
    {
        elem->q_forw->q_back = elem->q_back;
    }

    elem->q_forw = NULL;
    elem->q_back = NULL;
}
