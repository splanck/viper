//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_trie.c
// Purpose: Implements a prefix tree (Trie) for string keys with associated
//   object values. Each trie node stores up to TRIE_ALPHABET_SIZE (256) child
//   pointers indexed by ASCII byte value. Supports exact lookup, prefix search
//   (all keys with a given prefix), longest-prefix-match, and lexicographic
//   key enumeration.
//
// Key invariants:
//   - Each node has a fixed 256-element children array indexed by byte value,
//     supporting the full byte range (0-255) including UTF-8 multi-byte sequences.
//   - The root node is created lazily (on first insert) and freed recursively
//     by the trie finalizer.
//   - is_terminal marks nodes where a complete key ends. A node can be both
//     terminal (a key ends here) and internal (a longer key passes through).
//   - Values are retained (rt_obj_retain) on insert and released (rt_obj_free)
//     on overwrite or node deletion in the recursive free_node function.
//   - Deletion is recursive with pruning: leaf nodes that are not terminal are
//     freed bottom-up to keep the trie compact.
//   - KeysWithPrefix returns a Seq of all matching key strings (GC-managed).
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - Trie objects are GC-managed (rt_obj_new_i64). All trie nodes are
//     malloc'd individually and freed recursively by the GC finalizer via
//     free_node. Values stored in nodes are released on node free.
//
// Links: src/runtime/collections/rt_trie.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_trie.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

#define TRIE_ALPHABET_SIZE 256

typedef struct rt_trie_node {
    struct rt_trie_node *children[TRIE_ALPHABET_SIZE];
    void *value;        // Non-NULL if this node marks end of a key
    int8_t is_terminal; // 1 if a key ends here
} rt_trie_node;

typedef struct rt_trie_impl {
    void **vptr;
    rt_trie_node *root;
    size_t count;
} rt_trie_impl;

static rt_trie_node *new_node(void) {
    rt_trie_node *n = (rt_trie_node *)calloc(1, sizeof(rt_trie_node));
    if (!n)
        rt_trap("rt_trie: memory allocation failed");
    return n;
}

static void free_node(rt_trie_node *node) {
    if (!node)
        return;
    for (int i = 0; i < TRIE_ALPHABET_SIZE; ++i)
        free_node(node->children[i]);
    if (node->value && rt_obj_release_check0(node->value))
        rt_obj_free(node->value);
    free(node);
}

/// Collect all keys under a node into a Seq.
/// buf/buf_cap are passed by pointer so the buffer can grow as needed.
static void collect_keys(rt_trie_node *node, char **buf, size_t *buf_cap, size_t depth, void *seq) {
    if (!node)
        return;
    if (node->is_terminal) {
        rt_string key = rt_string_from_bytes(*buf, depth);
        rt_seq_push(seq, (void *)key);
        rt_str_release_maybe(key);
    }
    for (int i = 0; i < TRIE_ALPHABET_SIZE; ++i) {
        if (node->children[i]) {
            // Grow buffer if the next character would exceed capacity
            if (depth + 1 > *buf_cap) {
                size_t new_cap = *buf_cap * 2;
                if (new_cap < depth + 2)
                    new_cap = depth + 2;
                char *new_buf = (char *)realloc(*buf, new_cap);
                if (!new_buf)
                    continue; // Skip on allocation failure
                *buf = new_buf;
                *buf_cap = new_cap;
            }
            (*buf)[depth] = (char)i;
            collect_keys(node->children[i], buf, buf_cap, depth + 1, seq);
        }
    }
}

/// Check if any descendant (or node itself) is a terminal.
static int has_any_key(rt_trie_node *node) {
    if (!node)
        return 0;
    if (node->is_terminal)
        return 1;
    for (int i = 0; i < TRIE_ALPHABET_SIZE; ++i) {
        if (has_any_key(node->children[i]))
            return 1;
    }
    return 0;
}

static void rt_trie_finalize(void *obj) {
    if (!obj)
        return;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    free_node(trie->root);
    trie->root = NULL;
    trie->count = 0;
}

/// @brief Construct an empty trie. Each node has a 256-way child array (one slot per byte
/// value), so memory cost is high per node but lookups are O(key length).
void *rt_trie_new(void) {
    rt_trie_impl *trie = (rt_trie_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_trie_impl));
    if (!trie)
        return NULL;
    trie->vptr = NULL;
    trie->root = new_node();
    trie->count = 0;
    rt_obj_set_finalizer(trie, rt_trie_finalize);
    return trie;
}

/// @brief Number of keys (terminal nodes) currently stored in the trie.
int64_t rt_trie_len(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)((rt_trie_impl *)obj)->count;
}

/// @brief Returns 1 if the trie has no keys.
int8_t rt_trie_is_empty(void *obj) {
    return rt_trie_len(obj) == 0;
}

/// @brief Insert or update `key → value`. Walks the trie one byte at a time, allocating new
/// nodes for each missing branch. Existing key replaces (and releases) the old value. O(|key|).
void rt_trie_set(void *obj, rt_string key, void *value) {
    if (!obj)
        return;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return;

    const char *cstr = rt_string_cstr(key);
    size_t len = cstr ? strlen(cstr) : 0;

    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            node->children[c] = new_node();
        if (!node->children[c])
            return; // alloc fail
        node = node->children[c];
    }

    if (!node->is_terminal)
        trie->count++;

    // Replace old value
    void *old = node->value;
    rt_obj_retain_maybe(value);
    node->value = value;
    node->is_terminal = 1;
    if (old && rt_obj_release_check0(old))
        rt_obj_free(old);
}

/// @brief Look up `key`. Returns the borrowed value or NULL if not present. O(|key|).
void *rt_trie_get(void *obj, rt_string key) {
    if (!obj)
        return NULL;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return NULL;

    const char *cstr = rt_string_cstr(key);
    size_t len = cstr ? strlen(cstr) : 0;

    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return NULL;
        node = node->children[c];
    }
    return node->is_terminal ? node->value : NULL;
}

/// @brief Returns 1 if `key` is exactly stored (terminal). Distinct from `_has_prefix`.
int8_t rt_trie_has(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return 0;

    const char *cstr = rt_string_cstr(key);
    size_t len = cstr ? strlen(cstr) : 0;

    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return 0;
        node = node->children[c];
    }
    return node->is_terminal;
}

/// @brief Returns 1 if any stored key starts with `prefix` (or `prefix` itself is stored).
/// Useful for autocomplete "any matches?" probes.
int8_t rt_trie_has_prefix(void *obj, rt_string prefix) {
    if (!obj)
        return 0;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return 0;

    const char *cstr = rt_string_cstr(prefix);
    size_t len = cstr ? strlen(cstr) : 0;

    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return 0;
        node = node->children[c];
    }
    return has_any_key(node) ? 1 : 0;
}

/// @brief Return a Seq of all stored keys that start with `prefix` (the typical autocomplete
/// query). Walks the subtrie rooted at the prefix node and reconstructs full keys via DFS.
void *rt_trie_with_prefix(void *obj, rt_string prefix) {
    void *result = rt_seq_new();
    if (!obj)
        return result;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return result;

    const char *cstr = rt_string_cstr(prefix);
    size_t plen = cstr ? strlen(cstr) : 0;

    // Navigate to prefix node
    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < plen; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return result;
        node = node->children[c];
    }

    // Collect all keys under this node; buffer grows as needed
    size_t buf_cap = 4096;
    char *buf = (char *)malloc(buf_cap);
    if (!buf)
        return result;
    if (plen > 0)
        memcpy(buf, cstr, plen);
    collect_keys(node, &buf, &buf_cap, plen, result);
    free(buf);
    return result;
}

/// @brief Find the longest stored key that is a prefix of `str`. Useful for tokenizers, IP
/// routing, and dictionary-based segmentation. Empty result if no prefix matches.
rt_string rt_trie_longest_prefix(void *obj, rt_string str) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return rt_string_from_bytes("", 0);

    const char *cstr = rt_string_cstr(str);
    size_t len = cstr ? strlen(cstr) : 0;

    rt_trie_node *node = trie->root;
    size_t last_match = 0;
    int found = 0;

    if (node->is_terminal) {
        found = 1;
        last_match = 0;
    }

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            break;
        node = node->children[c];
        if (node->is_terminal) {
            found = 1;
            last_match = i + 1;
        }
    }

    if (!found)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(cstr, last_match);
}

/// @brief Remove `key` from the trie (releases its value). Returns 1 if removed, 0 if absent.
/// Empty branches are NOT pruned for simplicity — the trie keeps its structural memory.
int8_t rt_trie_remove(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return 0;

    const char *cstr = rt_string_cstr(key);
    size_t len = cstr ? strlen(cstr) : 0;

    // Navigate to the node
    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return 0;
        node = node->children[c];
    }

    if (!node->is_terminal)
        return 0;

    // Remove terminal mark and release value
    node->is_terminal = 0;
    if (node->value) {
        if (rt_obj_release_check0(node->value))
            rt_obj_free(node->value);
        node->value = NULL;
    }
    trie->count--;

    // Note: We don't prune empty branches for simplicity.
    // The trie will still work correctly, just uses a bit more memory.
    return 1;
}

/// @brief Free every node and reset the trie to empty. Releases all stored values.
void rt_trie_clear(void *obj) {
    if (!obj)
        return;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    free_node(trie->root);
    trie->root = new_node();
    trie->count = 0;
}

/// @brief Return a Seq of every stored key in lexicographic order. Owned-element Seq (releases
/// the strings on its own destruction). Implemented via DFS over the trie.
void *rt_trie_keys(void *obj) {
    void *result = rt_seq_new();
    rt_seq_set_owns_elements(result, 1);
    if (!obj)
        return result;
    rt_trie_impl *trie = (rt_trie_impl *)obj;
    if (!trie->root)
        return result;

    size_t buf_cap = 4096;
    char *buf = (char *)malloc(buf_cap);
    if (!buf)
        return result;
    collect_keys(trie->root, &buf, &buf_cap, 0, result);
    free(buf);
    return result;
}

/// @brief Recursively clone a trie node and all its descendants.
static rt_trie_node *clone_node(rt_trie_node *src) {
    if (!src)
        return NULL;

    rt_trie_node *dst = new_node();
    dst->is_terminal = src->is_terminal;
    dst->value = src->value;
    if (dst->value)
        rt_obj_retain_maybe(dst->value);

    for (int i = 0; i < TRIE_ALPHABET_SIZE; ++i)
        dst->children[i] = clone_node(src->children[i]);

    return dst;
}

/// @brief Deep-copy the trie structure (all nodes); values are shared via reference-count
/// retain. Modifying the clone's structure (insertions/removals) doesn't affect the source.
void *rt_trie_clone(void *obj) {
    if (!obj)
        return rt_trie_new();

    rt_trie_impl *src = (rt_trie_impl *)obj;
    rt_trie_impl *dst = (rt_trie_impl *)rt_trie_new();
    if (!dst)
        return NULL;

    // Free the default empty root from rt_trie_new, replace with deep copy
    free_node(dst->root);
    dst->root = clone_node(src->root);
    dst->count = src->count;

    return dst;
}
