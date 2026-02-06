//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/stdlib.c
// Purpose: Standard C library general utilities for ViperDOS.
// Key invariants: Standard C semantics; syscall-based memory allocation.
// Ownership/Lifetime: Library; allocations managed via sbrk heap.
// Links: user/libc/include/stdlib.h
//
//===----------------------------------------------------------------------===//

/**
 * @file stdlib.c
 * @brief Standard C library general utilities for ViperDOS.
 *
 * @details
 * This file implements the standard C library general utility functions:
 *
 * - Memory allocation: malloc, free, calloc, realloc
 * - Program termination: exit, _Exit, abort, atexit
 * - String conversion: atoi, atol, atoll, strtol, strtoul, strtod, strtof
 * - Integer arithmetic: abs, labs, llabs, div, ldiv, lldiv
 * - Searching/sorting: qsort, bsearch
 * - Random numbers: rand, srand
 * - Environment: getenv, setenv, unsetenv, putenv
 * - Integer-to-string: itoa, ltoa, ultoa
 *
 * Memory allocation uses a simple linked-list free list with sbrk() for
 * heap expansion. Blocks are 16-byte aligned for performance.
 */

#include "../include/stdlib.h"
#include "../include/string.h"
#include "syscall_internal.h"

/* Syscall numbers from viperdos/syscall_nums.hpp */
#define SYS_TASK_EXIT 0x01
#define SYS_SBRK 0x0A

/**
 * @brief Extend the program's data segment.
 *
 * @details
 * Wrapper around the SYS_SBRK syscall. Increases (or decreases, if negative)
 * the program break by the specified increment. The program break is the
 * first address past the end of the uninitialized data segment (heap).
 *
 * @param increment Number of bytes to add to the data segment.
 * @return Previous program break on success, (void*)-1 on failure.
 */
static void *sbrk(long increment) {
    long result = __syscall1(SYS_SBRK, increment);
    if (result < 0) {
        return (void *)-1;
    }
    return (void *)result;
}

/**
 * @brief Block header for malloc free list.
 *
 * @details
 * Each allocated block is preceded by this header. The header tracks
 * the usable size (excluding header), a pointer to the next block in
 * the free list, and whether this block is currently free.
 */
struct block_header {
    size_t size;               /**< Usable size of this block (excluding header). */
    struct block_header *next; /**< Next block in the free list chain. */
    int free;                  /**< Non-zero if block is free, 0 if allocated. */
};

/** @brief Head of the malloc free list. */
static struct block_header *free_list = NULL;

/**
 * @brief Allocate memory from the heap.
 *
 * @details
 * Allocates size bytes of uninitialized memory. The returned pointer is
 * 16-byte aligned for performance on modern architectures. Memory is
 * obtained from a free list of previously freed blocks or by extending
 * the heap via sbrk().
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL if allocation fails.
 */
void *malloc(size_t size) {
    if (size == 0)
        return NULL;

    /* Align size to 16 bytes */
    size = (size + 15) & ~15UL;

    /* First check free list */
    struct block_header *prev = NULL;
    struct block_header *curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            return (void *)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* Need to allocate new block from heap */
    size_t total = sizeof(struct block_header) + size;
    struct block_header *block = (struct block_header *)sbrk(total);
    if (block == (void *)-1) {
        return NULL;
    }

    block->size = size;
    block->next = NULL;
    block->free = 0;

    /* Add to list */
    if (prev) {
        prev->next = block;
    } else {
        free_list = block;
    }

    return (void *)(block + 1);
}

/**
 * @brief Free previously allocated memory.
 *
 * @details
 * Marks the block containing ptr as free, making it available for future
 * allocations. If ptr is NULL, no operation is performed. The memory is
 * not returned to the OS but is added to the free list for reuse.
 *
 * @param ptr Pointer previously returned by malloc, calloc, or realloc.
 */
void free(void *ptr) {
    if (!ptr)
        return;

    struct block_header *block = ((struct block_header *)ptr) - 1;
    block->free = 1;

    /* TODO: coalesce adjacent free blocks */
}

/**
 * @brief Allocate and zero-initialize an array.
 *
 * @details
 * Allocates memory for an array of nmemb elements of size bytes each
 * and initializes all bytes to zero. The total allocation is nmemb * size.
 *
 * @param nmemb Number of elements in the array.
 * @param size Size of each element in bytes.
 * @return Pointer to zero-initialized memory, or NULL on failure.
 */
void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

/**
 * @brief Reallocate memory block to new size.
 *
 * @details
 * Changes the size of the memory block pointed to by ptr to size bytes.
 * The contents are preserved up to the minimum of old and new sizes.
 * If ptr is NULL, behaves like malloc(size). If size is 0 and ptr is
 * not NULL, behaves like free(ptr) and returns NULL.
 *
 * If the block cannot be resized in place, a new block is allocated,
 * contents are copied, and the old block is freed.
 *
 * @param ptr Pointer to memory block to resize (may be NULL).
 * @param size New size in bytes.
 * @return Pointer to resized block, or NULL on failure.
 */
void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    struct block_header *block = ((struct block_header *)ptr) - 1;
    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

/* atexit handlers */
#define ATEXIT_MAX 32
static void (*atexit_handlers[ATEXIT_MAX])(void);
static int atexit_count = 0;

/**
 * @brief Register a function to be called at program exit.
 *
 * @details
 * Registers function to be called when the program terminates normally
 * via exit() or return from main(). Functions are called in reverse order
 * of registration (LIFO). Up to ATEXIT_MAX handlers can be registered.
 *
 * @param function Function pointer to register.
 * @return 0 on success, -1 if registration fails.
 */
int atexit(void (*function)(void)) {
    if (atexit_count >= ATEXIT_MAX || !function)
        return -1;

    atexit_handlers[atexit_count++] = function;
    return 0;
}

/**
 * @brief Terminate the program normally.
 *
 * @details
 * Performs cleanup and terminates the program:
 * 1. Calls atexit handlers in reverse order of registration.
 * 2. Flushes all stdio buffers (when implemented).
 * 3. Calls the SYS_TASK_EXIT syscall with the status code.
 *
 * This function does not return.
 *
 * @param status Exit status code (0 indicates success).
 */
void exit(int status) {
    /* Call atexit handlers in reverse order of registration */
    while (atexit_count > 0) {
        atexit_count--;
        if (atexit_handlers[atexit_count])
            atexit_handlers[atexit_count]();
    }

    /* Flush stdio buffers */
    /* (Note: fflush(NULL) would flush all streams if we had proper stdio) */

    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ; /* Should never reach here */
}

/**
 * @brief Terminate the program immediately without cleanup.
 *
 * @details
 * Terminates the program immediately without calling atexit handlers,
 * flushing stdio buffers, or any other cleanup. This is the C99/C11
 * version of immediate termination.
 *
 * This function does not return.
 *
 * @param status Exit status code.
 */
void _Exit(int status) {
    /* Exit immediately without cleanup */
    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ;
}

/**
 * @brief Terminate the program immediately (POSIX).
 *
 * @details
 * POSIX version of immediate program termination. Identical to _Exit()
 * in this implementation. Does not call atexit handlers or flush buffers.
 *
 * This function does not return.
 *
 * @param status Exit status code.
 */
void _exit(int status) {
    /* POSIX _exit - exit immediately without cleanup */
    __syscall1(SYS_TASK_EXIT, status);
    while (1)
        ;
}

/**
 * @brief Abort program execution abnormally.
 *
 * @details
 * Causes abnormal program termination. In a full implementation, this
 * would raise SIGABRT. Here it exits with code 134 (128 + SIGABRT).
 *
 * This function does not return.
 */
void abort(void) {
    exit(134); /* SIGABRT-like exit code */
}

/**
 * @brief Convert string to integer.
 *
 * @details
 * Parses the initial portion of nptr as a decimal integer. Skips leading
 * whitespace, handles optional sign, and converts digits until a non-digit
 * is encountered.
 *
 * @param nptr String to convert.
 * @return Converted integer value.
 */
int atoi(const char *nptr) {
    return (int)atol(nptr);
}

/**
 * @brief Convert string to long integer.
 *
 * @details
 * Parses the initial portion of nptr as a decimal long integer.
 * Skips leading whitespace, handles optional sign, and converts
 * digits until a non-digit is encountered.
 *
 * @param nptr String to convert.
 * @return Converted long integer value.
 */
long atol(const char *nptr) {
    long result = 0;
    int neg = 0;

    while (*nptr == ' ' || *nptr == '\t')
        nptr++;

    if (*nptr == '-') {
        neg = 1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return neg ? -result : result;
}

/**
 * @brief Convert string to long long integer.
 *
 * @details
 * Parses the initial portion of nptr as a decimal long long integer.
 * Skips leading whitespace, handles optional sign, and converts
 * digits until a non-digit is encountered.
 *
 * @param nptr String to convert.
 * @return Converted long long integer value.
 */
long long atoll(const char *nptr) {
    long long result = 0;
    int neg = 0;

    while (*nptr == ' ' || *nptr == '\t')
        nptr++;

    if (*nptr == '-') {
        neg = 1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return neg ? -result : result;
}

/**
 * @brief Convert character to digit value in given base.
 *
 * @details
 * Helper function for strtol/strtoul. Converts a character to its
 * numeric value in the specified base. Handles digits 0-9 and
 * letters a-z/A-Z for bases up to 36.
 *
 * @param c Character to convert.
 * @param base Numeric base (2-36).
 * @return Digit value if valid, -1 if character is not a valid digit.
 */
static int char_to_digit(char c, int base) {
    int val;
    if (c >= '0' && c <= '9') {
        val = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        val = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        val = c - 'A' + 10;
    } else {
        return -1;
    }
    return (val < base) ? val : -1;
}

/**
 * @brief Convert string to long integer with base detection.
 *
 * @details
 * Converts the initial portion of nptr to a long integer. Handles:
 * - Leading whitespace (skipped)
 * - Optional sign (+/-)
 * - Base prefixes (0x/0X for hex, 0 for octal when base is 0)
 * - Digits in the specified base (2-36)
 *
 * If endptr is not NULL, stores a pointer to the first unconverted
 * character in *endptr.
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @param base Numeric base (0 for auto-detect, or 2-36).
 * @return Converted long integer value.
 */
long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    long result = 0;
    int neg = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* Handle sign */
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    /* Handle base prefix */
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    /* Convert */
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0)
            break;
        result = result * base + digit;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;

    return neg ? -result : result;
}

/**
 * @brief Convert string to unsigned long integer with base detection.
 *
 * @details
 * Converts the initial portion of nptr to an unsigned long integer.
 * Similar to strtol() but for unsigned values. Handles base prefixes
 * and stores pointer to first unconverted character if endptr is provided.
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @param base Numeric base (0 for auto-detect, or 2-36).
 * @return Converted unsigned long integer value.
 */
unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long result = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;

    /* Skip optional + */
    if (*s == '+')
        s++;

    /* Handle base prefix */
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    /* Convert */
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0)
            break;
        result = result * base + digit;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;

    return result;
}

/**
 * @brief Convert string to long long integer.
 *
 * @details
 * Wrapper around strtol() that returns a long long. For this implementation,
 * long and long long have the same size (64-bit).
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @param base Numeric base (0 for auto-detect, or 2-36).
 * @return Converted long long integer value.
 */
long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

/**
 * @brief Convert string to unsigned long long integer.
 *
 * @details
 * Wrapper around strtoul() that returns an unsigned long long.
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @param base Numeric base (0 for auto-detect, or 2-36).
 * @return Converted unsigned long long integer value.
 */
unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

/**
 * @brief Compute absolute value of integer.
 *
 * @param n Integer value.
 * @return Absolute value of n.
 */
int abs(int n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Compute absolute value of long integer.
 *
 * @param n Long integer value.
 * @return Absolute value of n.
 */
long labs(long n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Compute absolute value of long long integer.
 *
 * @param n Long long integer value.
 * @return Absolute value of n.
 */
long long llabs(long long n) {
    return (n < 0) ? -n : n;
}

/**
 * @brief Compute quotient and remainder of integer division.
 *
 * @details
 * Computes both the quotient and remainder of numer/denom in a single
 * operation, which may be more efficient than separate / and % operations.
 *
 * @param numer Numerator (dividend).
 * @param denom Denominator (divisor).
 * @return div_t structure containing quot (quotient) and rem (remainder).
 */
div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/**
 * @brief Compute quotient and remainder of long integer division.
 *
 * @param numer Numerator (dividend).
 * @param denom Denominator (divisor).
 * @return ldiv_t structure containing quot and rem.
 */
ldiv_t ldiv(long numer, long denom) {
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/**
 * @brief Compute quotient and remainder of long long integer division.
 *
 * @param numer Numerator (dividend).
 * @param denom Denominator (divisor).
 * @return lldiv_t structure containing quot and rem.
 */
lldiv_t lldiv(long long numer, long long denom) {
    lldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

/**
 * @brief Swap bytes between two memory locations.
 *
 * @details
 * Helper function for qsort(). Exchanges size bytes between
 * memory locations a and b.
 *
 * @param a First memory location.
 * @param b Second memory location.
 * @param size Number of bytes to swap.
 */
static void swap_bytes(void *a, void *b, size_t size) {
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (size--) {
        unsigned char tmp = *pa;
        *pa++ = *pb;
        *pb++ = tmp;
    }
}

/**
 * @brief Sort an array using quicksort algorithm.
 *
 * @details
 * Sorts nmemb elements of size bytes each, starting at base, using the
 * comparison function compar. Currently implemented as insertion sort
 * for simplicity (works well for small arrays).
 *
 * The comparison function should return:
 * - Negative value if first argument is less than second
 * - Zero if arguments are equal
 * - Positive value if first argument is greater than second
 *
 * @param base Pointer to first element of array.
 * @param nmemb Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compar Comparison function.
 */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    /* Simple insertion sort for now - works well for small arrays */
    unsigned char *arr = (unsigned char *)base;

    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, arr + j * size) > 0) {
            swap_bytes(arr + (j - 1) * size, arr + j * size, size);
            j--;
        }
    }
}

/**
 * @brief Binary search a sorted array.
 *
 * @details
 * Searches a sorted array of nmemb elements for key using binary search.
 * The array must be sorted in ascending order according to the comparison
 * function. If found, returns a pointer to the matching element.
 *
 * @param key Pointer to the key to search for.
 * @param base Pointer to first element of sorted array.
 * @param nmemb Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compar Comparison function (same semantics as qsort).
 * @return Pointer to matching element, or NULL if not found.
 */
void *bsearch(const void *key,
              const void *base,
              size_t nmemb,
              size_t size,
              int (*compar)(const void *, const void *)) {
    const unsigned char *arr = (const unsigned char *)base;
    size_t lo = 0;
    size_t hi = nmemb;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            return (void *)(arr + mid * size);
        }
    }

    return NULL;
}

/* Simple linear congruential generator for rand() */
static unsigned int rand_seed = 1;

/**
 * @brief Generate pseudo-random integer.
 *
 * @details
 * Returns a pseudo-random integer in the range 0 to RAND_MAX (32767).
 * Uses a linear congruential generator algorithm. The sequence is
 * deterministic based on the seed set by srand().
 *
 * @warning This function is not thread-safe. Use rand_r() for thread-safe
 *          random number generation.
 *
 * @return Pseudo-random integer between 0 and RAND_MAX.
 *
 * @see rand_r, srand
 */
int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed / 65536) % 32768);
}

/**
 * @brief Seed the pseudo-random number generator.
 *
 * @details
 * Sets the seed for the sequence of pseudo-random numbers returned
 * by rand(). Calling srand() with the same seed will produce the
 * same sequence.
 *
 * @warning This function is not thread-safe. For multi-threaded programs,
 *          use rand_r() with per-thread seed storage.
 *
 * @param seed Seed value for the random number generator.
 *
 * @see rand, rand_r
 */
void srand(unsigned int seed) {
    rand_seed = seed;
}

/**
 * @brief Thread-safe pseudo-random number generator.
 *
 * @details
 * Returns a pseudo-random integer in the range 0 to RAND_MAX (32767).
 * Uses a linear congruential generator with caller-provided seed storage,
 * making it safe for use in multi-threaded programs.
 *
 * @param seedp Pointer to the seed value (updated on each call).
 *
 * @return Pseudo-random integer between 0 and RAND_MAX.
 *
 * @note POSIX.1-2001 thread-safe alternative to rand().
 *
 * @see rand, srand
 */
int rand_r(unsigned int *seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (int)((*seedp / 65536) % 32768);
}

/*
 * Environment variables
 *
 * Simple implementation using a static array.
 * Each entry is "NAME=value" format.
 */
#define ENV_MAX 64
#define ENV_ENTRY_MAX 256

static char env_storage[ENV_MAX][ENV_ENTRY_MAX];
static char *environ_ptrs[ENV_MAX + 1];
static int env_count = 0;
static int env_initialized = 0;

/* Global environ pointer (required by POSIX) */
char **environ = environ_ptrs;

static void init_environ(void) {
    if (!env_initialized) {
        for (int i = 0; i <= ENV_MAX; i++)
            environ_ptrs[i] = NULL;
        env_initialized = 1;
    }
}

static int env_find(const char *name) {
    size_t len = 0;
    while (name[len] && name[len] != '=')
        len++;

    for (int i = 0; i < env_count; i++) {
        if (environ_ptrs[i] && strncmp(environ_ptrs[i], name, len) == 0 &&
            environ_ptrs[i][len] == '=') {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Get value of environment variable.
 *
 * @details
 * Searches the environment list for a variable named name and returns
 * a pointer to the value string. The returned pointer should not be
 * modified by the caller.
 *
 * @param name Name of the environment variable.
 * @return Pointer to value string, or NULL if not found.
 */
char *getenv(const char *name) {
    if (!name)
        return NULL;

    init_environ();

    int idx = env_find(name);
    if (idx < 0)
        return NULL;

    /* Return pointer to the value part (after '=') */
    char *p = environ_ptrs[idx];
    while (*p && *p != '=')
        p++;
    if (*p == '=')
        p++;
    return p;
}

/**
 * @brief Set environment variable.
 *
 * @details
 * Adds or modifies an environment variable. If the variable already
 * exists and overwrite is non-zero, the value is replaced. If overwrite
 * is zero and the variable exists, the call succeeds without modification.
 *
 * @param name Variable name (must not contain '=').
 * @param value Value to set.
 * @param overwrite If non-zero, replace existing value.
 * @return 0 on success, -1 on failure.
 */
int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '='))
        return -1;

    init_environ();

    int idx = env_find(name);
    if (idx >= 0) {
        if (!overwrite)
            return 0;
    } else {
        if (env_count >= ENV_MAX)
            return -1;
        idx = env_count++;
    }

    /* Build "NAME=value" string */
    size_t name_len = strlen(name);
    size_t value_len = value ? strlen(value) : 0;
    if (name_len + 1 + value_len + 1 > ENV_ENTRY_MAX)
        return -1;

    char *entry = env_storage[idx];
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    if (value)
        memcpy(entry + name_len + 1, value, value_len + 1);
    else
        entry[name_len + 1] = '\0';

    environ_ptrs[idx] = entry;
    return 0;
}

/**
 * @brief Remove environment variable.
 *
 * @details
 * Removes the environment variable named name from the environment.
 * If the variable doesn't exist, the function succeeds without error.
 *
 * @param name Variable name to remove (must not contain '=').
 * @return 0 on success, -1 on failure.
 */
int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '='))
        return -1;

    init_environ();

    int idx = env_find(name);
    if (idx < 0)
        return 0; /* Not found is not an error */

    /* Shift remaining entries down */
    for (int i = idx; i < env_count - 1; i++) {
        memcpy(env_storage[i], env_storage[i + 1], ENV_ENTRY_MAX);
        environ_ptrs[i] = env_storage[i];
    }
    env_count--;
    environ_ptrs[env_count] = NULL;

    return 0;
}

/**
 * @brief Add or modify environment variable from string.
 *
 * @details
 * Parses a "NAME=value" string and adds or modifies the corresponding
 * environment variable. The string must contain an '=' character.
 *
 * @param string String in "NAME=value" format.
 * @return 0 on success, -1 on failure.
 */
int putenv(char *string) {
    if (!string)
        return -1;

    char *eq = strchr(string, '=');
    if (!eq)
        return -1;

    /* Extract name */
    size_t name_len = eq - string;
    char name[256];
    if (name_len >= sizeof(name))
        return -1;
    memcpy(name, string, name_len);
    name[name_len] = '\0';

    return setenv(name, eq + 1, 1);
}

/*
 * Floating-point string conversion
 */

/* Helper: check if character is whitespace */
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/**
 * @brief Convert string to double-precision floating-point.
 *
 * @details
 * Converts the initial portion of nptr to a double value. Handles:
 * - Leading whitespace (skipped)
 * - Optional sign (+/-)
 * - Integer part
 * - Fractional part (after '.')
 * - Exponent (e/E followed by optional sign and digits)
 * - Special values: INF, INFINITY, NAN
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @return Converted double value.
 */
double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    double result = 0.0;
    int sign = 1;
    int exp_sign = 1;
    int exponent = 0;
    int has_digits = 0;

    /* Skip leading whitespace */
    while (is_space(*s))
        s++;

    /* Handle sign */
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    /* Handle special values */
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F')) {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') && (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y')) {
            s += 5;
        }
        if (endptr)
            *endptr = (char *)s;
        return sign > 0 ? __builtin_inf() : -__builtin_inf();
    }

    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        if (endptr)
            *endptr = (char *)s;
        return __builtin_nan("");
    }

    /* Parse integer part */
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        s++;
        has_digits = 1;
    }

    /* Parse fractional part */
    if (*s == '.') {
        s++;
        double fraction = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s - '0') * fraction;
            fraction *= 0.1;
            s++;
            has_digits = 1;
        }
    }

    /* Parse exponent */
    if (has_digits && (*s == 'e' || *s == 'E')) {
        s++;
        if (*s == '-') {
            exp_sign = -1;
            s++;
        } else if (*s == '+') {
            s++;
        }

        while (*s >= '0' && *s <= '9') {
            exponent = exponent * 10 + (*s - '0');
            s++;
        }

        /* Apply exponent */
        double exp_mult = 1.0;
        while (exponent > 0) {
            exp_mult *= 10.0;
            exponent--;
        }
        if (exp_sign > 0)
            result *= exp_mult;
        else
            result /= exp_mult;
    }

    if (endptr)
        *endptr = has_digits ? (char *)s : (char *)nptr;

    return sign * result;
}

/**
 * @brief Convert string to single-precision floating-point.
 *
 * @details
 * Converts the initial portion of nptr to a float value.
 * Implemented as a wrapper around strtod() with cast to float.
 *
 * @param nptr String to convert.
 * @param endptr If non-NULL, receives pointer to first unconverted char.
 * @return Converted float value.
 */
float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

/*
 * Note: In freestanding environment without compiler-rt, long double
 * operations require runtime support (__extenddftf2) we don't have.
 * This implementation uses a union to avoid the compiler generating
 * a call to the soft-float conversion routine.
 */
long double strtold(const char *nptr, char **endptr) {
    /* Parse as double - same precision we'll output */
    double result = strtod(nptr, endptr);

    /*
     * Avoid implicit double->long double conversion.
     * On AArch64 without compiler-rt, we simply store the double
     * value in the low 64 bits of the long double return register.
     * This is imprecise but avoids missing symbol errors.
     */
    union {
        double d;
        long double ld;
    } u = {0};

    u.d = result;
    return u.ld;
}

/**
 * @brief Convert string to double-precision floating-point.
 *
 * @details
 * Simple wrapper around strtod() that ignores the end pointer.
 * Equivalent to strtod(nptr, NULL).
 *
 * @param nptr String to convert.
 * @return Converted double value.
 */
double atof(const char *nptr) {
    return strtod(nptr, (char **)0);
}

/*
 * Integer to string conversion
 */

static char *unsigned_to_str(unsigned long value, char *str, int base, int is_negative) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *p = str;
    char *start;

    if (base < 2 || base > 36) {
        *p = '\0';
        return str;
    }

    /* Handle zero */
    if (value == 0 && !is_negative) {
        *p++ = '0';
        *p = '\0';
        return str;
    }

    /* Build string in reverse */
    start = p;
    if (is_negative)
        p++; /* Reserve space for sign */

    char *digit_start = p;
    while (value > 0) {
        *p++ = digits[value % base];
        value /= base;
    }
    *p = '\0';

    /* Reverse the digits */
    char *end = p - 1;
    while (digit_start < end) {
        char tmp = *digit_start;
        *digit_start = *end;
        *end = tmp;
        digit_start++;
        end--;
    }

    /* Add sign if needed */
    if (is_negative)
        *start = '-';

    return str;
}

/**
 * @brief Convert integer to string.
 *
 * @details
 * Converts value to a string representation in the specified base
 * and stores it in str. For base 10, negative values are prefixed
 * with '-'. Bases 2-36 are supported.
 *
 * @param value Integer to convert.
 * @param str Buffer to store result (must be large enough).
 * @param base Numeric base (2-36).
 * @return Pointer to str.
 */
char *itoa(int value, char *str, int base) {
    if (value < 0 && base == 10) {
        return unsigned_to_str((unsigned long)(-(long)value), str, base, 1);
    }
    return unsigned_to_str((unsigned long)(unsigned int)value, str, base, 0);
}

/**
 * @brief Convert long integer to string.
 *
 * @details
 * Converts value to a string representation in the specified base.
 * Similar to itoa() but for long values.
 *
 * @param value Long integer to convert.
 * @param str Buffer to store result.
 * @param base Numeric base (2-36).
 * @return Pointer to str.
 */
char *ltoa(long value, char *str, int base) {
    if (value < 0 && base == 10) {
        return unsigned_to_str((unsigned long)(-value), str, base, 1);
    }
    return unsigned_to_str((unsigned long)value, str, base, 0);
}

/**
 * @brief Convert unsigned long integer to string.
 *
 * @details
 * Converts value to a string representation in the specified base.
 * Since value is unsigned, no sign prefix is ever added.
 *
 * @param value Unsigned long integer to convert.
 * @param str Buffer to store result.
 * @param base Numeric base (2-36).
 * @return Pointer to str.
 */
char *ultoa(unsigned long value, char *str, int base) {
    return unsigned_to_str(value, str, base, 0);
}
