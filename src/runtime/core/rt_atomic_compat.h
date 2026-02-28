//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_atomic_compat.h
// Purpose: Provides GCC/Clang __atomic_* builtin compatibility for MSVC.
//          Maps __atomic_load_n, __atomic_store_n, __atomic_compare_exchange_n,
//          __atomic_fetch_add, and __atomic_fetch_sub to MSVC Interlocked
//          intrinsics via C11 _Generic dispatch.
//
// Key invariants:
//   - On GCC/Clang this header is a no-op (builtins are native).
//   - On MSVC, dispatches to 32-bit or 64-bit intrinsics based on operand size.
//   - Memory ordering constants (__ATOMIC_RELAXED, etc.) are defined but MSVC
//     intrinsics provide full barrier semantics regardless of ordering.
//
// Ownership/Lifetime:
//   - Header-only, no state.
//
// Links: src/runtime/core/rt_crc32.c, rt_output.c, rt_gc.c (consumers)
//
//===----------------------------------------------------------------------===//

#ifndef RT_ATOMIC_COMPAT_H
#define RT_ATOMIC_COMPAT_H

#ifdef _MSC_VER

#include <intrin.h>

/* Memory ordering constants (values don't matter for MSVC intrinsics,
   which always provide full barriers, but needed for API compatibility). */
#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

/* -- 32-bit helpers (int / long) ----------------------------------------- */

static __forceinline long rt__atomic_load_32(volatile long *p)
{
    return _InterlockedOr(p, 0);
}

static __forceinline void rt__atomic_store_32(volatile long *p, long v)
{
    _InterlockedExchange(p, v);
}

static __forceinline long rt__atomic_fetch_add_32(volatile long *p, long v)
{
    return _InterlockedExchangeAdd(p, v);
}

static __forceinline int rt__atomic_cas_32(volatile long *p, long *expected, long desired)
{
    long old = _InterlockedCompareExchange(p, desired, *expected);
    if (old == *expected)
        return 1;
    *expected = old;
    return 0;
}

/* -- 64-bit helpers (long long / int64_t) -------------------------------- */

static __forceinline long long rt__atomic_load_64(volatile long long *p)
{
    return _InterlockedOr64(p, 0);
}

static __forceinline void rt__atomic_store_64(volatile long long *p, long long v)
{
    _InterlockedExchange64(p, v);
}

static __forceinline long long rt__atomic_fetch_add_64(volatile long long *p, long long v)
{
    return _InterlockedExchangeAdd64(p, v);
}

/* -- Type-generic macros via C11 _Generic -------------------------------- */

#define __atomic_load_n(ptr, order)                          \
    _Generic(*(ptr),                                         \
        int:       (int)rt__atomic_load_32((volatile long *)(ptr)),       \
        long:      (long)rt__atomic_load_32((volatile long *)(ptr)),      \
        long long: (long long)rt__atomic_load_64((volatile long long *)(ptr)))

#define __atomic_store_n(ptr, val, order)                    \
    _Generic(*(ptr),                                         \
        int:       rt__atomic_store_32((volatile long *)(ptr), (long)(val)),       \
        long:      rt__atomic_store_32((volatile long *)(ptr), (long)(val)),      \
        long long: rt__atomic_store_64((volatile long long *)(ptr), (long long)(val)))

#define __atomic_fetch_add(ptr, val, order)                  \
    _Generic(*(ptr),                                         \
        int:       (int)rt__atomic_fetch_add_32((volatile long *)(ptr), (long)(val)),       \
        long:      (long)rt__atomic_fetch_add_32((volatile long *)(ptr), (long)(val)),      \
        long long: (long long)rt__atomic_fetch_add_64((volatile long long *)(ptr), (long long)(val)))

#define __atomic_fetch_sub(ptr, val, order)                  \
    _Generic(*(ptr),                                         \
        int:       (int)rt__atomic_fetch_add_32((volatile long *)(ptr), -(long)(val)),       \
        long:      (long)rt__atomic_fetch_add_32((volatile long *)(ptr), -(long)(val)),      \
        long long: (long long)rt__atomic_fetch_add_64((volatile long long *)(ptr), -(long long)(val)))

#define __atomic_compare_exchange_n(ptr, expected, desired, weak, success_order, fail_order) \
    rt__atomic_cas_32((volatile long *)(ptr), (long *)(expected), (long)(desired))

#endif /* _MSC_VER */
#endif /* RT_ATOMIC_COMPAT_H */
