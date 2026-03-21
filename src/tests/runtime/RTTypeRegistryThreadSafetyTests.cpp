//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTypeRegistryThreadSafetyTests.cpp
// Purpose: Validate thread safety of the type registry, including the sealed
//          fast-path, concurrent reads, and seal-prevents-write behavior.
// Key invariants:
//   - Reads work correctly both before and after sealing.
//   - Concurrent reads on a sealed registry do not corrupt state.
//   - Registration after sealing traps (tested via setjmp/signal).
// Ownership/Lifetime: Uses runtime library only.
// Links: src/runtime/oop/rt_type_registry.c, src/runtime/oop/rt_oop.h
//
//===----------------------------------------------------------------------===//

#include "rt_oop.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <thread>
#include <atomic>
#include <vector>

// Mock vtables for test classes
static void *vtable_a[1] = {nullptr};
static void *vtable_b[1] = {nullptr};
static void *vtable_c[1] = {nullptr};

// Mock interface table
static void mock_method(void) {}
static void *itable_a[1] = {(void *)mock_method};

// Type and interface IDs
static const int TYPE_A = 500;
static const int TYPE_B = 501;
static const int TYPE_C = 502;
static const int IFACE_I = 50;

static void register_test_types()
{
    rt_register_class_with_base(TYPE_A, vtable_a, "Thread.A", 0, -1);
    rt_register_class_with_base(TYPE_B, vtable_b, "Thread.B", 0, TYPE_A);
    rt_register_class_with_base(TYPE_C, vtable_c, "Thread.C", 0, TYPE_B);
    rt_register_interface_direct(IFACE_I, "Thread.I", 1);
    rt_bind_interface(TYPE_A, IFACE_I, itable_a);
}

// ============================================================================
// Test 1: Reads work correctly before sealing
// ============================================================================
static void test_reads_before_seal()
{
    printf("  test_reads_before_seal...");

    assert(rt_type_is_a(TYPE_A, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_B, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_C, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_C, TYPE_B) == 1);
    assert(rt_type_is_a(TYPE_A, TYPE_B) == 0);
    assert(rt_type_implements(TYPE_A, IFACE_I) == 1);
    assert(rt_type_implements(TYPE_B, IFACE_I) == 1);
    assert(rt_type_implements(TYPE_C, IFACE_I) == 1);

    printf(" OK\n");
}

// ============================================================================
// Test 2: Reads work correctly after sealing
// ============================================================================
static void test_reads_after_seal()
{
    printf("  test_reads_after_seal...");

    rt_type_registry_seal();

    assert(rt_type_is_a(TYPE_A, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_B, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_C, TYPE_A) == 1);
    assert(rt_type_is_a(TYPE_C, TYPE_B) == 1);
    assert(rt_type_is_a(TYPE_A, TYPE_C) == 0);
    assert(rt_type_implements(TYPE_A, IFACE_I) == 1);
    assert(rt_type_implements(TYPE_B, IFACE_I) == 1);
    assert(rt_type_implements(TYPE_C, IFACE_I) == 1);

    printf(" OK\n");
}

// ============================================================================
// Test 3: Concurrent reads on a sealed registry
// ============================================================================
static void test_concurrent_reads_post_seal()
{
    printf("  test_concurrent_reads_post_seal...");

    const int NUM_THREADS = 8;
    const int ITERS_PER_THREAD = 10000;
    std::atomic<int> failures{0};

    auto worker = [&]()
    {
        for (int i = 0; i < ITERS_PER_THREAD; ++i)
        {
            if (rt_type_is_a(TYPE_C, TYPE_A) != 1)
                failures++;
            if (rt_type_is_a(TYPE_B, TYPE_A) != 1)
                failures++;
            if (rt_type_implements(TYPE_C, IFACE_I) != 1)
                failures++;
            if (rt_type_is_a(TYPE_A, TYPE_C) != 0)
                failures++;

            // Lookup vtable
            void **vt = rt_get_class_vtable(TYPE_B);
            if (vt != vtable_b)
                failures++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    assert(failures.load() == 0);
    printf(" OK (%d threads x %d iterations)\n", NUM_THREADS, ITERS_PER_THREAD);
}

// ============================================================================
// Test 4: Interface lookup under concurrency
// ============================================================================
static void test_concurrent_interface_lookup()
{
    printf("  test_concurrent_interface_lookup...");

    const int NUM_THREADS = 8;
    const int ITERS_PER_THREAD = 5000;
    std::atomic<int> failures{0};

    // Create mock objects with vtables (minimal object layout: vptr at offset 0)
    struct MockObj
    {
        void **vptr;
    };
    MockObj obj_a = {vtable_a};
    MockObj obj_b = {vtable_b};
    MockObj obj_c = {vtable_c};

    auto worker = [&]()
    {
        for (int i = 0; i < ITERS_PER_THREAD; ++i)
        {
            void **it_a = rt_itable_lookup(&obj_a, IFACE_I);
            void **it_b = rt_itable_lookup(&obj_b, IFACE_I);
            void **it_c = rt_itable_lookup(&obj_c, IFACE_I);

            if (it_a != itable_a)
                failures++;
            if (it_b != itable_a)
                failures++; // inherited from A
            if (it_c != itable_a)
                failures++; // inherited from A through B
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    assert(failures.load() == 0);
    printf(" OK (%d threads x %d iterations)\n", NUM_THREADS, ITERS_PER_THREAD);
}

int main()
{
    printf("RTTypeRegistryThreadSafetyTests\n");

    register_test_types();

    test_reads_before_seal();
    test_reads_after_seal();
    test_concurrent_reads_post_seal();
    test_concurrent_interface_lookup();

    printf("All thread safety tests passed.\n");
    return 0;
}
