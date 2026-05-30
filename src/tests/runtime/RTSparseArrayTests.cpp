//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_sparsearray.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
static int g_finalizer_calls = 0;

struct SparseProbe {
    void *vptr;
    int64_t count;
    int64_t capacity;
    void *slots;
};
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void count_finalizer(void *) {
    ++g_finalizer_calls;
}

static void test_new() {
    void *sa = rt_sparse_new();
    assert(sa != NULL);
    assert(rt_sparse_len(sa) == 0);
}

static void test_set_get() {
    void *sa = rt_sparse_new();
    rt_string v1 = make_str("hello");
    rt_string v2 = make_str("world");

    rt_sparse_set(sa, 0, v1);
    rt_sparse_set(sa, 1000, v2);

    assert(rt_sparse_len(sa) == 2);
    assert(rt_sparse_get(sa, 0) == v1);
    assert(rt_sparse_get(sa, 1000) == v2);
    assert(rt_sparse_get(sa, 500) == NULL);
}

static void test_has() {
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 42, make_str("val"));

    assert(rt_sparse_has(sa, 42) == 1);
    assert(rt_sparse_has(sa, 43) == 0);
}

static void test_remove() {
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 10, make_str("ten"));

    assert(rt_sparse_remove(sa, 10) == 1);
    assert(rt_sparse_len(sa) == 0);
    assert(rt_sparse_get(sa, 10) == NULL);
    assert(rt_sparse_remove(sa, 10) == 0);
}

static void test_negative_indices() {
    void *sa = rt_sparse_new();
    rt_string v = make_str("neg");
    rt_sparse_set(sa, -5, v);
    assert(rt_sparse_get(sa, -5) == v);
    assert(rt_sparse_has(sa, -5) == 1);
}

static void test_large_indices() {
    void *sa = rt_sparse_new();
    rt_string v = make_str("big");
    rt_sparse_set(sa, 1000000, v);
    assert(rt_sparse_get(sa, 1000000) == v);
    assert(rt_sparse_len(sa) == 1);
}

static void test_overwrite() {
    void *sa = rt_sparse_new();
    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_sparse_set(sa, 5, v1);
    rt_sparse_set(sa, 5, v2);

    assert(rt_sparse_len(sa) == 1);
    assert(rt_sparse_get(sa, 5) == v2);
}

static void test_indices() {
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 10, make_str("a"));
    rt_sparse_set(sa, -20, make_str("b"));

    void *idx = rt_sparse_indices(sa);
    assert(rt_seq_len(idx) == 2);

    bool found_10 = false;
    bool found_neg20 = false;
    for (int64_t i = 0; i < rt_seq_len(idx); ++i) {
        void *boxed = rt_seq_get(idx, i);
        assert(rt_box_type(boxed) == RT_BOX_I64);
        int64_t value = rt_unbox_i64(boxed);
        found_10 = found_10 || value == 10;
        found_neg20 = found_neg20 || value == -20;
    }
    assert(found_10);
    assert(found_neg20);
}

static void test_values() {
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 1, make_str("x"));
    rt_sparse_set(sa, 2, make_str("y"));

    void *vals = rt_sparse_values(sa);
    assert(rt_seq_len(vals) == 2);
}

static void test_clear() {
    void *sa = rt_sparse_new();
    rt_sparse_set(sa, 0, make_str("a"));
    rt_sparse_set(sa, 1, make_str("b"));

    rt_sparse_clear(sa);
    assert(rt_sparse_len(sa) == 0);
}

static void test_set_null_removes_entry() {
    void *sa = rt_sparse_new();
    rt_string value = make_str("value");

    rt_sparse_set(sa, 7, value);
    assert(rt_sparse_len(sa) == 1);
    assert(rt_sparse_has(sa, 7) == 1);

    rt_sparse_set(sa, 7, NULL);
    assert(rt_sparse_len(sa) == 0);
    assert(rt_sparse_has(sa, 7) == 0);
    assert(rt_sparse_get(sa, 7) == NULL);

    rt_string_unref(value);
}

static void test_set_null_releases_value() {
    void *sa = rt_sparse_new();
    void *value = new_obj();

    g_finalizer_calls = 0;
    rt_obj_set_finalizer(value, count_finalizer);

    rt_sparse_set(sa, 11, value);
    release_obj(value); // SparseArray now owns the only reference.
    assert(g_finalizer_calls == 0);

    rt_sparse_set(sa, 11, NULL);
    assert(g_finalizer_calls == 1);
    assert(rt_sparse_len(sa) == 0);

    release_obj(sa);
}

static void test_set_null_missing_does_not_grow() {
    void *sa = rt_sparse_new();
    for (int64_t i = 0; i < 12; ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "v%lld", (long long)i);
        rt_sparse_set(sa, i, make_str(buf));
    }

    SparseProbe *probe = static_cast<SparseProbe *>(sa);
    int64_t capacity_before = probe->capacity;
    assert(rt_sparse_len(sa) == 12);

    rt_sparse_set(sa, 123456789, NULL);
    assert(rt_sparse_len(sa) == 12);
    assert(probe->capacity == capacity_before);

    release_obj(sa);
}

static void test_set_retain_overflow_leaves_slot_empty() {
    void *sa = rt_sparse_new();
    void *value = new_obj();

    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    EXPECT_TRAP(rt_sparse_set(sa, 123, value));
    assert(rt_sparse_len(sa) == 0);
    assert(rt_sparse_has(sa, 123) == 0);
    assert(rt_sparse_get(sa, 123) == NULL);

    hdr->refcnt = 1;
    release_obj(value);
    release_obj(sa);
}

static void test_grow() {
    void *sa = rt_sparse_new();
    // Insert enough elements to trigger grow (>70% of 16 = 12)
    for (int i = 0; i < 20; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "v%d", i);
        rt_sparse_set(sa, (int64_t)i, make_str(buf));
    }
    assert(rt_sparse_len(sa) == 20);

    // Verify all values survived rehash
    for (int i = 0; i < 20; i++) {
        assert(rt_sparse_has(sa, (int64_t)i) == 1);
    }
}

static void test_null_safety() {
    assert(rt_sparse_len(NULL) == 0);
    assert(rt_sparse_get(NULL, 0) == NULL);
    assert(rt_sparse_has(NULL, 0) == 0);
    assert(rt_sparse_remove(NULL, 0) == 0);
}

/// @brief Main.
int main() {
    test_new();
    test_set_get();
    test_has();
    test_remove();
    test_negative_indices();
    test_large_indices();
    test_overwrite();
    test_indices();
    test_values();
    test_clear();
    test_set_null_removes_entry();
    test_set_null_releases_value();
    test_set_null_missing_does_not_grow();
    test_set_retain_overflow_leaves_slot_empty();
    test_grow();
    test_null_safety();
    return 0;
}
