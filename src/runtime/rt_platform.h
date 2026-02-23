//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_platform.h
// Purpose: Cross-platform preprocessor abstractions for the Viper runtime, providing portable macros for thread-local storage, atomic operations, weak symbol linkage, and platform detection.
//
// Key invariants:
//   - RT_THREAD_LOCAL expands to the correct TLS keyword for each compiler/platform.
//   - RT_ATOMIC_* macros use C11 _Atomic on GCC/Clang and MSVC intrinsics on Windows.
//   - RT_WEAK uses __attribute__((weak)) on ELF targets and is empty on Mach-O/MSVC.
//   - Platform detection macros (RT_PLATFORM_WINDOWS etc.) are mutually exclusive.
//
// Ownership/Lifetime:
//   - All macros are pure preprocessor definitions; no runtime state is introduced.
//   - Including this header has no link-time side effects.
//
// Links: src/runtime/core/ (included by most runtime .c files)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

//===----------------------------------------------------------------------===//
// Platform Detection
//===----------------------------------------------------------------------===//

#if defined(_WIN32) || defined(_WIN64)
#define RT_PLATFORM_WINDOWS 1
#else
#define RT_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__)
#define RT_PLATFORM_MACOS 1
#else
#define RT_PLATFORM_MACOS 0
#endif

#if defined(__linux__)
#define RT_PLATFORM_LINUX 1
#else
#define RT_PLATFORM_LINUX 0
#endif

#if defined(__viperdos__)
#define RT_PLATFORM_VIPERDOS 1
#else
#define RT_PLATFORM_VIPERDOS 0
#endif

//===----------------------------------------------------------------------===//
// Compiler Detection
//===----------------------------------------------------------------------===//

#if defined(_MSC_VER) && !defined(__clang__)
#define RT_COMPILER_MSVC 1
#else
#define RT_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RT_COMPILER_GCC_LIKE 1
#else
#define RT_COMPILER_GCC_LIKE 0
#endif

//===----------------------------------------------------------------------===//
// Thread-Local Storage
//===----------------------------------------------------------------------===//

#if RT_COMPILER_MSVC
#define RT_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#define RT_THREAD_LOCAL _Thread_local
#else
#define RT_THREAD_LOCAL __thread
#endif

//===----------------------------------------------------------------------===//
// Weak Symbol Linkage
//===----------------------------------------------------------------------===//

#if RT_COMPILER_MSVC
// MSVC doesn't support weak linkage for functions. For data, use selectany.
// For functions, we define RT_WEAK as empty - test overrides work via link order.
#define RT_WEAK
#define RT_WEAK_DATA __declspec(selectany)
#elif RT_COMPILER_GCC_LIKE
#define RT_WEAK __attribute__((weak))
#define RT_WEAK_DATA __attribute__((weak))
#else
#define RT_WEAK
#define RT_WEAK_DATA
#endif

//===----------------------------------------------------------------------===//
// Atomic Operations
//===----------------------------------------------------------------------===//

#if RT_COMPILER_MSVC
#include <immintrin.h>
#include <intrin.h>

// Memory ordering constants (matching GCC values for compatibility)
#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

// Atomic load (32-bit)
static inline int rt_atomic_load_i32(const volatile int *ptr, int order)
{
    (void)order;
    int value = *ptr;
    _ReadWriteBarrier();
    return value;
}

// Atomic store (32-bit)
static inline void rt_atomic_store_i32(volatile int *ptr, int value, int order)
{
    (void)order;
    _ReadWriteBarrier();
    *ptr = value;
    _ReadWriteBarrier();
}

// Atomic exchange (32-bit)
static inline int rt_atomic_exchange_i32(volatile int *ptr, int value, int order)
{
    (void)order;
    return _InterlockedExchange((volatile long *)ptr, value);
}

// Atomic compare-exchange (32-bit)
static inline int rt_atomic_compare_exchange_i32(
    volatile int *ptr, int *expected, int desired, int success_order, int fail_order)
{
    (void)success_order;
    (void)fail_order;
    int old = _InterlockedCompareExchange((volatile long *)ptr, desired, *expected);
    if (old == *expected)
    {
        return 1;
    }
    *expected = old;
    return 0;
}

// Atomic fetch-add (32-bit)
static inline int rt_atomic_fetch_add_i32(volatile int *ptr, int value, int order)
{
    (void)order;
    return _InterlockedExchangeAdd((volatile long *)ptr, value);
}

// Atomic fetch-sub (32-bit)
static inline int rt_atomic_fetch_sub_i32(volatile int *ptr, int value, int order)
{
    (void)order;
    return _InterlockedExchangeAdd((volatile long *)ptr, -value);
}

// Atomic load (64-bit)
static inline int64_t rt_atomic_load_i64(const volatile int64_t *ptr, int order)
{
    (void)order;
#if defined(_M_X64) || defined(_M_ARM64)
    int64_t value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    // 32-bit x86 needs interlocked read for 64-bit atomics
    return _InterlockedCompareExchange64((volatile long long *)ptr, 0, 0);
#endif
}

// Atomic store (64-bit)
static inline void rt_atomic_store_i64(volatile int64_t *ptr, int64_t value, int order)
{
    (void)order;
#if defined(_M_X64) || defined(_M_ARM64)
    _ReadWriteBarrier();
    *ptr = value;
    _ReadWriteBarrier();
#else
    _InterlockedExchange64((volatile long long *)ptr, value);
#endif
}

// Atomic fetch-add (64-bit)
static inline int64_t rt_atomic_fetch_add_i64(volatile int64_t *ptr, int64_t value, int order)
{
    (void)order;
    return _InterlockedExchangeAdd64((volatile long long *)ptr, value);
}

// Atomic fetch-sub (64-bit)
static inline int64_t rt_atomic_fetch_sub_i64(volatile int64_t *ptr, int64_t value, int order)
{
    (void)order;
    return _InterlockedExchangeAdd64((volatile long long *)ptr, -value);
}

// Atomic load (size_t) - needed because size_t is unsigned and may differ from int64_t
static inline size_t rt_atomic_load_size(const volatile size_t *ptr, int order)
{
    (void)order;
#if defined(_M_X64) || defined(_M_ARM64)
    size_t value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    return (size_t)_InterlockedCompareExchange64((volatile long long *)ptr, 0, 0);
#endif
}

// Atomic store (size_t)
static inline void rt_atomic_store_size(volatile size_t *ptr, size_t value, int order)
{
    (void)order;
#if defined(_M_X64) || defined(_M_ARM64)
    _ReadWriteBarrier();
    *ptr = value;
    _ReadWriteBarrier();
#else
    _InterlockedExchange64((volatile long long *)ptr, (long long)value);
#endif
}

// Atomic fetch-add (size_t)
static inline size_t rt_atomic_fetch_add_size(volatile size_t *ptr, size_t value, int order)
{
    (void)order;
    return (size_t)_InterlockedExchangeAdd64((volatile long long *)ptr, (long long)value);
}

// Atomic fetch-sub (size_t)
static inline size_t rt_atomic_fetch_sub_size(volatile size_t *ptr, size_t value, int order)
{
    (void)order;
    return (size_t)_InterlockedExchangeAdd64((volatile long long *)ptr, -(long long)value);
}

// Atomic load (pointer)
static inline void *rt_atomic_load_ptr(void *const volatile *ptr, int order)
{
    (void)order;
    void *value = *ptr;
    _ReadWriteBarrier();
    return value;
}

// Atomic store (pointer)
static inline void rt_atomic_store_ptr(void *volatile *ptr, void *value, int order)
{
    (void)order;
    _ReadWriteBarrier();
    *ptr = value;
    _ReadWriteBarrier();
}

// Atomic exchange (pointer)
static inline void *rt_atomic_exchange_ptr(void *volatile *ptr, void *value, int order)
{
    (void)order;
#if defined(_M_X64) || defined(_M_ARM64)
    return _InterlockedExchangePointer(ptr, value);
#else
    return (void *)_InterlockedExchange((volatile long *)ptr, (long)value);
#endif
}

// Atomic compare-exchange (pointer)
static inline int rt_atomic_compare_exchange_ptr(
    void *volatile *ptr, void **expected, void *desired, int success_order, int fail_order)
{
    (void)success_order;
    (void)fail_order;
#if defined(_M_X64) || defined(_M_ARM64)
    void *old = _InterlockedCompareExchangePointer(ptr, desired, *expected);
#else
    void *old =
        (void *)_InterlockedCompareExchange((volatile long *)ptr, (long)desired, (long)*expected);
#endif
    if (old == *expected)
    {
        return 1;
    }
    *expected = old;
    return 0;
}

// Map GCC-style atomic builtins to our functions
#define __atomic_load_n(ptr, order)                                                                \
    _Generic((ptr),                                                                                \
        volatile int *: rt_atomic_load_i32,                                                        \
        const volatile int *: rt_atomic_load_i32,                                                  \
        int *: rt_atomic_load_i32,                                                                 \
        const int *: rt_atomic_load_i32,                                                           \
        volatile int64_t *: rt_atomic_load_i64,                                                    \
        const volatile int64_t *: rt_atomic_load_i64,                                              \
        int64_t *: rt_atomic_load_i64,                                                             \
        const int64_t *: rt_atomic_load_i64,                                                       \
        volatile size_t *: rt_atomic_load_size,                                                    \
        const volatile size_t *: rt_atomic_load_size,                                              \
        size_t *: rt_atomic_load_size,                                                             \
        const size_t *: rt_atomic_load_size)((ptr), (order))

#define __atomic_store_n(ptr, val, order)                                                          \
    _Generic((ptr),                                                                                \
        volatile int *: rt_atomic_store_i32,                                                       \
        int *: rt_atomic_store_i32,                                                                \
        volatile int64_t *: rt_atomic_store_i64,                                                   \
        int64_t *: rt_atomic_store_i64,                                                            \
        volatile size_t *: rt_atomic_store_size,                                                   \
        size_t *: rt_atomic_store_size)((ptr), (val), (order))

#define __atomic_exchange_n(ptr, val, order)                                                       \
    rt_atomic_exchange_i32((volatile int *)(ptr), (val), (order))

#define __atomic_compare_exchange_n(ptr, expected, desired, weak, success, fail)                   \
    rt_atomic_compare_exchange_i32((volatile int *)(ptr), (expected), (desired), (success), (fail))

#define __atomic_fetch_add(ptr, val, order)                                                        \
    _Generic((ptr),                                                                                \
        volatile int *: rt_atomic_fetch_add_i32,                                                   \
        int *: rt_atomic_fetch_add_i32,                                                            \
        volatile int64_t *: rt_atomic_fetch_add_i64,                                               \
        int64_t *: rt_atomic_fetch_add_i64,                                                        \
        volatile size_t *: rt_atomic_fetch_add_size,                                               \
        size_t *: rt_atomic_fetch_add_size)((ptr), (val), (order))

#define __atomic_fetch_sub(ptr, val, order)                                                        \
    _Generic((ptr),                                                                                \
        volatile int *: rt_atomic_fetch_sub_i32,                                                   \
        int *: rt_atomic_fetch_sub_i32,                                                            \
        volatile int64_t *: rt_atomic_fetch_sub_i64,                                               \
        int64_t *: rt_atomic_fetch_sub_i64,                                                        \
        volatile size_t *: rt_atomic_fetch_sub_size,                                               \
        size_t *: rt_atomic_fetch_sub_size)((ptr), (val), (order))

// Atomic test-and-set (spinlock primitive)
static inline int rt_atomic_test_and_set(volatile int *ptr, int order)
{
    (void)order;
    return _InterlockedExchange((volatile long *)ptr, 1) != 0;
}

// Atomic clear (spinlock release)
static inline void rt_atomic_clear(volatile int *ptr, int order)
{
    (void)order;
    _ReadWriteBarrier();
    *ptr = 0;
    _ReadWriteBarrier();
}

#define __atomic_test_and_set(ptr, order) rt_atomic_test_and_set((volatile int *)(ptr), (order))
#define __atomic_clear(ptr, order) rt_atomic_clear((volatile int *)(ptr), (order))

// Atomic thread fence
static inline void rt_atomic_thread_fence(int order)
{
    (void)order;
    // Use _mm_mfence for full memory barrier on x86/x64
    // This is more portable than MemoryBarrier() which requires windows.h
#if defined(_M_X64) || defined(_M_IX86)
    _mm_mfence();
#elif defined(_M_ARM64)
    __dmb(_ARM64_BARRIER_SY);
#else
    _ReadWriteBarrier();
#endif
}

#define __atomic_thread_fence(order) rt_atomic_thread_fence(order)

#endif // RT_COMPILER_MSVC

//===----------------------------------------------------------------------===//
// Windows POSIX Compatibility
//===----------------------------------------------------------------------===//

#if RT_PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Avoid conflicts with Windows headers
#ifdef Type
#undef Type
#endif

#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>

// POSIX-like type definitions
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
// LOW-1: Use 64-bit ssize_t on both 32-bit and 64-bit Windows.
// The legacy 32-bit definition (int) limited I/O return values to ±2 GB and
// caused sign-extension hazards when assigning to 64-bit targets.  Using
// long long uniformly matches POSIX semantics regardless of pointer width.
typedef long long ssize_t;
#endif

// POSIX function mappings
#define access _access
#define getcwd _getcwd
#define chdir _chdir
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#define fileno _fileno
#define isatty _isatty
#define strdup _strdup
#define getpid _getpid

// Access mode flags
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1 // Note: Windows doesn't really have execute permission
#endif

// String functions
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

// File I/O
#define lseek _lseek

// POSIX file type macros (Windows doesn't have these)
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISBLK
#define S_ISBLK(m) (0) // Windows doesn't have block devices
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#endif

// Path separator
#define RT_PATH_SEPARATOR '\\'
#define RT_PATH_SEPARATOR_STR "\\"

// High-resolution time for Windows
static inline int64_t rt_windows_time_ms(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    // Convert 100-nanosecond intervals since 1601 to milliseconds since Unix epoch
    uint64_t time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // Subtract difference between 1601 and 1970 (in 100-ns intervals)
    time -= 116444736000000000ULL;
    return (int64_t)(time / 10000);
}

static inline int64_t rt_windows_time_us(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    time -= 116444736000000000ULL;
    return (int64_t)(time / 10);
}

static inline void rt_windows_sleep_ms(int64_t ms)
{
    if (ms > 0)
        Sleep((DWORD)ms);
}

#elif RT_PLATFORM_VIPERDOS

// ViperDOS platform — provides POSIX-compatible APIs via libc.
#include <sys/time.h>
#include <unistd.h>

#define RT_PATH_SEPARATOR '/'
#define RT_PATH_SEPARATOR_STR "/"

#else // POSIX systems (macOS, Linux)

#include <sys/time.h>
#include <unistd.h>

#define RT_PATH_SEPARATOR '/'
#define RT_PATH_SEPARATOR_STR "/"

#endif // RT_PLATFORM_WINDOWS

//===----------------------------------------------------------------------===//
// Thread-Safe Time Functions
//===----------------------------------------------------------------------===//

#include <string.h>
#include <time.h>

/// @brief Thread-safe version of localtime().
/// @param timer Pointer to time_t value to convert.
/// @param result Pointer to struct tm to store the result.
/// @return Pointer to result on success, NULL on failure.
static inline struct tm *rt_localtime_r(const time_t *timer, struct tm *result)
{
#if RT_PLATFORM_WINDOWS
    // Windows localtime_s has reversed parameter order and returns errno_t
    if (localtime_s(result, timer) == 0)
        return result;
    return NULL;
#else
    return localtime_r(timer, result);
#endif
}

/// @brief Thread-safe version of gmtime().
/// @param timer Pointer to time_t value to convert.
/// @param result Pointer to struct tm to store the result.
/// @return Pointer to result on success, NULL on failure.
static inline struct tm *rt_gmtime_r(const time_t *timer, struct tm *result)
{
#if RT_PLATFORM_WINDOWS
    // Windows gmtime_s has reversed parameter order and returns errno_t
    if (gmtime_s(result, timer) == 0)
        return result;
    return NULL;
#else
    return gmtime_r(timer, result);
#endif
}

/// @brief Thread-safe version of strtok().
/// Maps to strtok_r on POSIX and strtok_s on Windows (same signature).
/// @param str   String to tokenize, or NULL to continue from last call.
/// @param delim Delimiter characters.
/// @param saveptr Caller-provided pointer used to store tokenizer state.
/// @return Pointer to next token, or NULL when no more tokens remain.
static inline char *rt_strtok_r(char *str, const char *delim, char **saveptr)
{
#if RT_PLATFORM_WINDOWS
    return strtok_s(str, delim, saveptr);
#else
    return strtok_r(str, delim, saveptr);
#endif
}

//===----------------------------------------------------------------------===//
// Format Attribute
//===----------------------------------------------------------------------===//

#if RT_COMPILER_GCC_LIKE
#define RT_PRINTF_FORMAT(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
#else
#define RT_PRINTF_FORMAT(fmt_idx, first_arg)
#endif
