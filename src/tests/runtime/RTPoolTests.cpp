//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPoolTests.cpp
// Purpose: Unit tests for rt_pool slab allocator.
// Key invariants:
//   - Pool allocates from size classes 64, 128, 256, 512
//   - Allocations > 512 fall back to malloc
//   - Freed blocks are recycled via freelist
//   - Allocated memory is zeroed
// Ownership/Lifetime:
//   - Test-only file; no production ownership
// Links: src/runtime/core/rt_pool.h, src/runtime/core/rt_pool.c
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_pool.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

// ============================================================================
// test_alloc_64 — Allocate 64-byte block, write to it, free
// ============================================================================

static void test_alloc_64(void) {
    void *p = rt_pool_alloc(64);
    assert(p != nullptr);
    memset(p, 0xAB, 64);
    rt_pool_free(p, 64);
    printf("test_alloc_64: PASSED\n");
}

// ============================================================================
// test_alloc_128 — Allocate 128-byte block, write to it, free
// ============================================================================

static void test_alloc_128(void) {
    void *p = rt_pool_alloc(128);
    assert(p != nullptr);
    memset(p, 0xCD, 128);
    rt_pool_free(p, 128);
    printf("test_alloc_128: PASSED\n");
}

// ============================================================================
// test_alloc_256 — Allocate 256-byte block, write to it, free
// ============================================================================

static void test_alloc_256(void) {
    void *p = rt_pool_alloc(256);
    assert(p != nullptr);
    memset(p, 0xEF, 256);
    rt_pool_free(p, 256);
    printf("test_alloc_256: PASSED\n");
}

// ============================================================================
// test_alloc_512 — Allocate 512-byte block, write to it, free
// ============================================================================

static void test_alloc_512(void) {
    void *p = rt_pool_alloc(512);
    assert(p != nullptr);
    memset(p, 0x42, 512);
    rt_pool_free(p, 512);
    printf("test_alloc_512: PASSED\n");
}

// ============================================================================
// test_alloc_large — Allocate >512 bytes (falls through to malloc)
// ============================================================================

static void test_alloc_large(void) {
    void *p = rt_pool_alloc(1024);
    assert(p != nullptr);
    memset(p, 0xFF, 1024);
    rt_pool_free(p, 1024);
    printf("test_alloc_large: PASSED\n");
}

// ============================================================================
// test_zeroed — Allocated memory must be zeroed
// ============================================================================

static void test_zeroed(void) {
    void *p = rt_pool_alloc(64);
    assert(p != nullptr);

    const unsigned char *bytes = static_cast<const unsigned char *>(p);
    for (size_t i = 0; i < 64; i++) {
        assert(bytes[i] == 0 && "pool-allocated memory must be zeroed");
    }

    rt_pool_free(p, 64);
    printf("test_zeroed: PASSED\n");
}

// ============================================================================
// test_reuse — Alloc, free, alloc same size should recycle the block
// ============================================================================

static void test_reuse(void) {
    void *first = rt_pool_alloc(64);
    assert(first != nullptr);
    void *saved = first;
    rt_pool_free(first, 64);

    void *second = rt_pool_alloc(64);
    assert(second != nullptr);
    // In a single-threaded scenario the freed block should be reused.
    assert(second == saved && "pool should recycle freed block");

    // Recycled block must still be zeroed.
    const unsigned char *bytes = static_cast<const unsigned char *>(second);
    for (size_t i = 0; i < 64; i++) {
        assert(bytes[i] == 0 && "recycled block must be zeroed");
    }

    rt_pool_free(second, 64);
    printf("test_reuse: PASSED\n");
}

// ============================================================================
// test_stats — Verify rt_pool_stats reports correct counts
// ============================================================================

static void test_stats(void) {
    // Shut down pools to get a clean baseline.
    rt_pool_shutdown();

    size_t allocated = 0, free_count = 0;

    // Before any allocation, both should be zero.
    rt_pool_stats(RT_POOL_128, &allocated, &free_count);
    assert(allocated == 0);
    assert(free_count == 0);

    // Allocate one 128-byte block.
    void *p = rt_pool_alloc(128);
    assert(p != nullptr);

    rt_pool_stats(RT_POOL_128, &allocated, &free_count);
    assert(allocated == 1);
    // A slab was created with 64 blocks; one was taken, so 63 remain free.
    assert(free_count == 63);

    // Free it — allocated drops to 0, free goes up to 64.
    rt_pool_free(p, 128);
    rt_pool_stats(RT_POOL_128, &allocated, &free_count);
    assert(allocated == 0);
    assert(free_count == 64);

    printf("test_stats: PASSED\n");
}

// ============================================================================
// test_many_allocs — Allocate 200+ small blocks, verify all non-NULL, free all
// ============================================================================

static void test_many_allocs(void) {
    static const int kCount = 200;
    void *ptrs[kCount];

    for (int i = 0; i < kCount; i++) {
        ptrs[i] = rt_pool_alloc(64);
        assert(ptrs[i] != nullptr);
    }

    // All pointers must be distinct.
    for (int i = 0; i < kCount; i++) {
        for (int j = i + 1; j < kCount; j++) {
            assert(ptrs[i] != ptrs[j] && "all allocations must be distinct");
        }
    }

    for (int i = 0; i < kCount; i++) {
        rt_pool_free(ptrs[i], 64);
    }

    printf("test_many_allocs: PASSED\n");
}

// ============================================================================
// test_null_free — rt_pool_free(NULL, size) must not crash
// ============================================================================

static void test_null_free(void) {
    rt_pool_free(nullptr, 64);
    rt_pool_free(nullptr, 128);
    rt_pool_free(nullptr, 256);
    rt_pool_free(nullptr, 512);
    rt_pool_free(nullptr, 1024);
    printf("test_null_free: PASSED\n");
}

// ============================================================================
// test_mixed_sizes — Allocate blocks of different sizes interleaved
// ============================================================================

static void test_mixed_sizes(void) {
    void *a = rt_pool_alloc(32);  // -> 64-byte class
    void *b = rt_pool_alloc(100); // -> 128-byte class
    void *c = rt_pool_alloc(200); // -> 256-byte class
    void *d = rt_pool_alloc(400); // -> 512-byte class
    void *e = rt_pool_alloc(64);  // -> 64-byte class

    assert(a != nullptr);
    assert(b != nullptr);
    assert(c != nullptr);
    assert(d != nullptr);
    assert(e != nullptr);

    // All must be distinct.
    assert(a != b);
    assert(a != c);
    assert(a != d);
    assert(b != c);
    assert(b != d);
    assert(c != d);
    assert(a != e);

    // Write different patterns to verify no overlap.
    memset(a, 0x11, 32);
    memset(b, 0x22, 100);
    memset(c, 0x33, 200);
    memset(d, 0x44, 400);
    memset(e, 0x55, 64);

    // Verify patterns are intact.
    const unsigned char *pa = static_cast<const unsigned char *>(a);
    for (size_t i = 0; i < 32; i++)
        assert(pa[i] == 0x11);

    const unsigned char *pb = static_cast<const unsigned char *>(b);
    for (size_t i = 0; i < 100; i++)
        assert(pb[i] == 0x22);

    const unsigned char *pc = static_cast<const unsigned char *>(c);
    for (size_t i = 0; i < 200; i++)
        assert(pc[i] == 0x33);

    const unsigned char *pd = static_cast<const unsigned char *>(d);
    for (size_t i = 0; i < 400; i++)
        assert(pd[i] == 0x44);

    const unsigned char *pe = static_cast<const unsigned char *>(e);
    for (size_t i = 0; i < 64; i++)
        assert(pe[i] == 0x55);

    rt_pool_free(a, 32);
    rt_pool_free(b, 100);
    rt_pool_free(c, 200);
    rt_pool_free(d, 400);
    rt_pool_free(e, 64);

    printf("test_mixed_sizes: PASSED\n");
}

// ============================================================================
// Entry point
// ============================================================================

int main(void) {
    printf("=== RT Pool Tests ===\n\n");

    test_alloc_64();
    test_alloc_128();
    test_alloc_256();
    test_alloc_512();
    test_alloc_large();
    test_zeroed();
    test_reuse();
    test_stats();
    test_many_allocs();
    test_null_free();
    test_mixed_sizes();

    printf("\nAll RT pool tests passed!\n");
    return 0;
}
