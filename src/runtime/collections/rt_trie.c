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
//   - The root node is allocated at construction and freed iteratively by the
//     trie finalizer to avoid C stack growth on deep keys.
//   - is_terminal marks nodes where a complete key ends. A node can be both
//     terminal (a key ends here) and internal (a longer key passes through).
//   - Values are retained (rt_obj_retain) on insert and released (rt_obj_free)
//     on overwrite or node deletion in the iterative free_node function.
//   - Deletion clears terminal nodes without pruning; clear/finalize free the
//     whole node tree iteratively.
//   - KeysWithPrefix returns a Seq of all matching key strings (GC-managed).
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - Trie objects are GC-managed (rt_obj_new_i64). All trie nodes are
//     malloc'd individually and freed iteratively by the GC finalizer via
//     free_node. Values stored in nodes are released on node free.
//
// Links: src/runtime/collections/rt_trie.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_trie.h"

#include "rt_collection_ids.h"
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

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

typedef struct {
    rt_trie_node *node;
    size_t next_child;
} trie_walk_frame;

typedef struct {
    rt_trie_node *node;
    size_t next_child;
    size_t depth;
    int8_t emitted;
} trie_collect_frame;

typedef struct {
    rt_trie_node *src;
    rt_trie_node *dst;
} trie_clone_pair;

/// @brief Checked cast of an opaque handle to the Trie implementation;
///        traps with @p what if @p obj is NULL or not a Trie.
static rt_trie_impl *as_trie(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_TRIE_CLASS_ID, sizeof(rt_trie_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_trie_impl *)obj;
}

static void trie_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void trie_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

/// @brief Allocate a zero-initialized trie node (traps on OOM).
static rt_trie_node *new_node(void) {
    rt_trie_node *n = (rt_trie_node *)calloc(1, sizeof(rt_trie_node));
    if (!n) {
        rt_trap("rt_trie: memory allocation failed");
        return NULL;
    }
    return n;
}

/// @brief Borrow the byte buffer + length of an rt_string (empty "" if null).
static const char *trie_string_data(rt_string s, size_t *out_len) {
    int64_t len = rt_str_len(s);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }

    const char *data = rt_string_cstr(s);
    if (!data) {
        *out_len = 0;
        return "";
    }

    *out_len = (size_t)len;
    return data;
}

/// @brief Compute the next traversal-stack capacity (double, or 64 from
///        empty); traps on size overflow. Shared by the explicit-stack walks.
static size_t grow_stack_capacity(size_t cap, size_t elem_size) {
    size_t new_cap = cap ? cap * 2 : 64;
    if (cap && cap > SIZE_MAX / 2) {
        rt_trap("rt_trie: traversal stack overflow");
        return 0;
    }
    if (new_cap > SIZE_MAX / elem_size) {
        rt_trap("rt_trie: traversal stack allocation overflow");
        return 0;
    }
    return new_cap;
}

/// @brief Push a node onto the explicit DFS walk stack, growing it if full
///        (NULL node ignored). Traps on OOM.
static void push_walk_frame(trie_walk_frame **stack, size_t *len, size_t *cap, rt_trie_node *node) {
    if (!node)
        return;
    if (*len == *cap) {
        size_t new_cap = grow_stack_capacity(*cap, sizeof(**stack));
        if (new_cap == 0)
            return;
        trie_walk_frame *new_stack = (trie_walk_frame *)realloc(*stack, new_cap * sizeof(**stack));
        if (!new_stack) {
            rt_trap("rt_trie: memory allocation failed");
            return;
        }
        *stack = new_stack;
        *cap = new_cap;
    }
    (*stack)[(*len)++] = (trie_walk_frame){node, 0};
}

/// @brief Push a node (with key depth) onto the key-collection stack,
///        growing it if full. Traps on OOM.
static void push_collect_frame(
    trie_collect_frame **stack, size_t *len, size_t *cap, rt_trie_node *node, size_t depth) {
    if (!node)
        return;
    if (*len == *cap) {
        size_t new_cap = grow_stack_capacity(*cap, sizeof(**stack));
        if (new_cap == 0)
            return;
        trie_collect_frame *new_stack =
            (trie_collect_frame *)realloc(*stack, new_cap * sizeof(**stack));
        if (!new_stack) {
            rt_trap("rt_trie: memory allocation failed");
            return;
        }
        *stack = new_stack;
        *cap = new_cap;
    }
    (*stack)[(*len)++] = (trie_collect_frame){node, 0, depth, 0};
}

/// @brief Push a (src,dst) node pair onto the deep-clone stack, growing it
///        if full (ignored unless both nodes are non-NULL). Traps on OOM.
static void push_clone_pair(
    trie_clone_pair **stack, size_t *len, size_t *cap, rt_trie_node *src, rt_trie_node *dst) {
    if (!src || !dst)
        return;
    if (*len == *cap) {
        size_t new_cap = grow_stack_capacity(*cap, sizeof(**stack));
        if (new_cap == 0)
            return;
        trie_clone_pair *new_stack = (trie_clone_pair *)realloc(*stack, new_cap * sizeof(**stack));
        if (!new_stack) {
            rt_trap("rt_trie: memory allocation failed");
            return;
        }
        *stack = new_stack;
        *cap = new_cap;
    }
    (*stack)[(*len)++] = (trie_clone_pair){src, dst};
}

/// @brief Grow the reusable key-reconstruction buffer to >= @p needed bytes
///        (doubling). Traps on overflow/OOM.
static int ensure_key_buf(char **buf, size_t *buf_cap, size_t needed) {
    if (needed <= *buf_cap)
        return 1;
    size_t new_cap = *buf_cap ? *buf_cap : 64;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            rt_trap("rt_trie: key buffer overflow");
            return 0;
        }
        new_cap *= 2;
    }
    char *new_buf = (char *)realloc(*buf, new_cap);
    if (!new_buf) {
        rt_trap("rt_trie: memory allocation failed");
        return 0;
    }
    *buf = new_buf;
    *buf_cap = new_cap;
    return 1;
}

/// @brief Free a subtree iteratively (explicit stack, no recursion) and
///        release each terminal node's stored value.
static void free_node(rt_trie_node *node) {
    if (!node)
        return;
    trie_walk_frame *stack = NULL;
    size_t len = 0;
    size_t cap = 0;
    push_walk_frame(&stack, &len, &cap, node);
    while (len > 0) {
        trie_walk_frame *frame = &stack[len - 1];
        if (!frame->node) {
            len--;
            continue;
        }
        if (frame->next_child < TRIE_ALPHABET_SIZE) {
            rt_trie_node *child = frame->node->children[frame->next_child++];
            if (child)
                push_walk_frame(&stack, &len, &cap, child);
            continue;
        }
        rt_trie_node *done = frame->node;
#ifdef _MSC_VER
#pragma warning(suppress : 6001)
#endif
        if (done->value && rt_obj_release_check0(done->value))
            rt_obj_free(done->value);
        free(done);
        len--;
    }
    free(stack);
}

/// @brief Iteratively visit every terminal node's value under @p node
///        (explicit-stack DFS, GC tracing helper).
static void traverse_node(rt_trie_node *node, rt_gc_visitor_t visitor, void *ctx) {
    if (!node || !visitor)
        return;
    trie_walk_frame *stack = NULL;
    size_t len = 0;
    size_t cap = 0;
    push_walk_frame(&stack, &len, &cap, node);
    while (len > 0) {
        trie_walk_frame *frame = &stack[len - 1];
        if (frame->next_child == 0 && frame->node->is_terminal)
            visitor(frame->node->value, ctx);
        if (frame->next_child < TRIE_ALPHABET_SIZE) {
            rt_trie_node *child = frame->node->children[frame->next_child++];
            if (child)
                push_walk_frame(&stack, &len, &cap, child);
            continue;
        }
        len--;
    }
    free(stack);
}

/// @brief GC traversal entry point: visit every stored value in the trie.
static void rt_trie_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;
    rt_trie_impl *trie = as_trie(obj, "Trie: invalid Trie object");
    if (!trie)
        return;
    traverse_node(trie->root, visitor, ctx);
}

/// Collect all keys under a node into a Seq.
/// buf/buf_cap are passed by pointer so the buffer can grow as needed.
static void collect_keys(rt_trie_node *node, char **buf, size_t *buf_cap, size_t depth, void *seq) {
    if (!node)
        return;
    trie_collect_frame *stack = NULL;
    size_t len = 0;
    size_t cap = 0;
    rt_string volatile key = NULL;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        trie_save_trap_error(saved_error, sizeof(saved_error), "rt_trie: failed to collect keys");
        rt_trap_clear_recovery();
        if (key)
            rt_str_release_maybe((rt_string)key);
        free(stack);
        free(*buf);
        *buf = NULL;
        *buf_cap = 0;
        trie_release_object(seq);
        rt_trap(saved_error);
        return;
    }

    push_collect_frame(&stack, &len, &cap, node, depth);
    while (len > 0) {
        trie_collect_frame *frame = &stack[len - 1];
        if (!frame->emitted) {
            frame->emitted = 1;
            if (frame->node->is_terminal) {
                key = rt_string_from_bytes(*buf, frame->depth);
                rt_seq_push(seq, (void *)key);
                rt_str_release_maybe((rt_string)key);
                key = NULL;
            }
        }
        if (frame->next_child < TRIE_ALPHABET_SIZE) {
            size_t child_index = frame->next_child++;
            rt_trie_node *child = frame->node->children[child_index];
            if (child) {
                if (!ensure_key_buf(buf, buf_cap, frame->depth + 1)) {
                    free(stack);
                    return;
                }
                (*buf)[frame->depth] = (char)child_index;
                push_collect_frame(&stack, &len, &cap, child, frame->depth + 1);
            }
            continue;
        }
        len--;
    }
    rt_trap_clear_recovery();
    free(stack);
}

/// Check if any descendant (or node itself) is a terminal.
static int has_any_key(rt_trie_node *node) {
    if (!node)
        return 0;
    trie_walk_frame *stack = NULL;
    size_t len = 0;
    size_t cap = 0;
    push_walk_frame(&stack, &len, &cap, node);
    while (len > 0) {
        trie_walk_frame *frame = &stack[len - 1];
        if (frame->next_child == 0 && frame->node->is_terminal) {
            free(stack);
            return 1;
        }
        if (frame->next_child < TRIE_ALPHABET_SIZE) {
            rt_trie_node *child = frame->node->children[frame->next_child++];
            if (child)
                push_walk_frame(&stack, &len, &cap, child);
            continue;
        }
        len--;
    }
    free(stack);
    return 0;
}

/// @brief GC finalizer: free the entire node tree and reset the trie.
static void rt_trie_finalize(void *obj) {
    if (!obj)
        return;
    rt_trie_impl *trie = as_trie(obj, "Trie: invalid Trie object");
    if (!trie)
        return;
    free_node(trie->root);
    trie->root = NULL;
    trie->count = 0;
}

/// @brief Construct an empty trie. Each node has a 256-way child array (one slot per byte
/// value), so memory cost is high per node but lookups are O(key length).
void *rt_trie_new(void) {
    rt_trie_impl *trie =
        (rt_trie_impl *)rt_obj_new_i64(RT_TRIE_CLASS_ID, (int64_t)sizeof(rt_trie_impl));
    if (!trie) {
        rt_trap("rt_trie: memory allocation failed");
        return NULL;
    }
    trie->vptr = NULL;
    trie->root = NULL;
    trie->count = 0;
    rt_obj_set_finalizer(trie, rt_trie_finalize);
    rt_gc_track(trie, rt_trie_traverse);
    trie->root = (rt_trie_node *)calloc(1, sizeof(rt_trie_node));
    if (!trie->root) {
        if (rt_obj_release_check0(trie))
            rt_obj_free(trie);
        rt_trap("rt_trie: memory allocation failed");
        return NULL;
    }
    return trie;
}

/// @brief Number of keys (terminal nodes) currently stored in the trie.
int64_t rt_trie_len(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_trie(obj, "Trie.Len: invalid Trie object")->count;
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
    rt_trie_impl *trie = as_trie(obj, "Trie.Set: invalid Trie object");
    if (!trie)
        return;
    if (!trie->root)
        return;

    size_t len = 0;
    const char *cstr = trie_string_data(key, &len);

    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c]) {
            node->children[c] = new_node();
            if (!node->children[c])
                return;
        }
        node = node->children[c];
    }

    void *old = node->value;
    rt_obj_retain_maybe(value);
    node->value = value;
    if (!node->is_terminal)
        trie->count++;
    node->is_terminal = 1;
    if (old && rt_obj_release_check0(old))
        rt_obj_free(old);
}

/// @brief Look up `key`. Returns the borrowed value or NULL if not present. O(|key|).
void *rt_trie_get(void *obj, rt_string key) {
    if (!obj)
        return NULL;
    rt_trie_impl *trie = as_trie(obj, "Trie.Get: invalid Trie object");
    if (!trie->root)
        return NULL;

    size_t len = 0;
    const char *cstr = trie_string_data(key, &len);

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
    rt_trie_impl *trie = as_trie(obj, "Trie.Has: invalid Trie object");
    if (!trie->root)
        return 0;

    size_t len = 0;
    const char *cstr = trie_string_data(key, &len);

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
    rt_trie_impl *trie = as_trie(obj, "Trie.HasPrefix: invalid Trie object");
    if (!trie->root)
        return 0;

    size_t len = 0;
    const char *cstr = trie_string_data(prefix, &len);

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
    rt_seq_set_owns_elements(result, 1);
    if (!obj)
        return result;
    rt_trie_impl *trie = as_trie(obj, "Trie.WithPrefix: invalid Trie object");
    if (!trie->root)
        return result;

    size_t plen = 0;
    const char *cstr = trie_string_data(prefix, &plen);

    // Navigate to prefix node
    rt_trie_node *node = trie->root;
    for (size_t i = 0; i < plen; ++i) {
        unsigned char c = (unsigned char)cstr[i];
        if (!node->children[c])
            return result;
        node = node->children[c];
    }

    // Collect all keys under this node; buffer grows as needed
    size_t buf_cap = plen + 1 > 4096 ? plen + 1 : 4096;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) {
        rt_trap("rt_trie: memory allocation failed");
        return result;
    }
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
    rt_trie_impl *trie = as_trie(obj, "Trie.LongestPrefix: invalid Trie object");
    if (!trie->root)
        return rt_string_from_bytes("", 0);

    size_t len = 0;
    const char *cstr = trie_string_data(str, &len);

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
    rt_trie_impl *trie = as_trie(obj, "Trie.Remove: invalid Trie object");
    if (!trie->root)
        return 0;

    size_t len = 0;
    const char *cstr = trie_string_data(key, &len);

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
    rt_trie_impl *trie = as_trie(obj, "Trie.Clear: invalid Trie object");
    if (!trie)
        return;
    rt_trie_node *replacement = new_node();
    if (!replacement)
        return;
    free_node(trie->root);
    trie->root = replacement;
    trie->count = 0;
}

/// @brief Return a Seq of every stored key in lexicographic order. Owned-element Seq (releases
/// the strings on its own destruction). Implemented via DFS over the trie.
void *rt_trie_keys(void *obj) {
    void *result = rt_seq_new();
    if (!result)
        return NULL;
    rt_seq_set_owns_elements(result, 1);
    if (!obj)
        return result;
    rt_trie_impl *trie = as_trie(obj, "Trie.Keys: invalid Trie object");
    if (!trie)
        return result;
    if (!trie->root)
        return result;

    size_t buf_cap = 4096;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) {
        rt_trap("rt_trie: memory allocation failed");
        return result;
    }
    collect_keys(trie->root, &buf, &buf_cap, 0, result);
    free(buf);
    return result;
}

/// @brief Iteratively clone a trie node and all its descendants.
static rt_trie_node *clone_node(rt_trie_node *src) {
    if (!src)
        return NULL;

    rt_trie_node *dst = new_node();
    if (!dst)
        return NULL;

    trie_clone_pair *stack = NULL;
    size_t len = 0;
    size_t cap = 0;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        trie_save_trap_error(saved_error, sizeof(saved_error), "rt_trie: clone failed");
        rt_trap_clear_recovery();
        free_node(dst);
        free(stack);
        rt_trap(saved_error);
        return NULL;
    }

    if (src->value)
        rt_obj_retain_maybe(src->value);
    dst->is_terminal = src->is_terminal;
    dst->value = src->value;

    push_clone_pair(&stack, &len, &cap, src, dst);
    while (len > 0) {
        trie_clone_pair pair = stack[--len];
        for (int i = 0; i < TRIE_ALPHABET_SIZE; ++i) {
            rt_trie_node *child_src = pair.src->children[i];
            if (!child_src)
                continue;
            rt_trie_node *child_dst = new_node();
            if (!child_dst) {
                free_node(dst);
                free(stack);
                rt_trap_clear_recovery();
                return NULL;
            }
            if (child_src->value)
                rt_obj_retain_maybe(child_src->value);
            child_dst->is_terminal = child_src->is_terminal;
            child_dst->value = child_src->value;
            pair.dst->children[i] = child_dst;
            push_clone_pair(&stack, &len, &cap, child_src, child_dst);
        }
    }
    rt_trap_clear_recovery();
    free(stack);

    return dst;
}

/// @brief Deep-copy the trie structure (all nodes); values are shared via reference-count
/// retain. Modifying the clone's structure (insertions/removals) doesn't affect the source.
void *rt_trie_clone(void *obj) {
    if (!obj)
        return rt_trie_new();

    rt_trie_impl *src = as_trie(obj, "Trie.Clone: invalid Trie object");
    rt_trie_impl *dst = (rt_trie_impl *)rt_trie_new();
    if (!dst)
        return NULL;

    rt_trie_node *cloned_root = clone_node(src->root);
    if (!cloned_root) {
        trie_release_object(dst);
        return NULL;
    }

    // Free the default empty root from rt_trie_new, replace with deep copy
    free_node(dst->root);
    dst->root = cloned_root;
    dst->count = src->count;

    return dst;
}
