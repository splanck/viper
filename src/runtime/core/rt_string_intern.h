//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_intern.h
// Purpose: Global string interning table providing O(1) equality comparison via pointer identity
// after interning, using FNV-1a hashing and a mutex for thread safety.
//
// Key invariants:
//   - Each unique byte sequence maps to exactly one canonical rt_string pointer.
//   - Interned strings are retained by the table and treated as immortal during normal operation.
//   - rt_string_interned_eq is O(1) pointer comparison; only valid for interned strings.
//   - Table uses open addressing with 5/8 load factor and power-of-two capacity.
//
// Ownership/Lifetime:
//   - rt_string_intern returns a retained pointer; caller must call rt_string_unref when done.
//   - The intern table retains its own reference to canonical strings; they are freed only at
//   shutdown.
//
// Links: src/runtime/core/rt_string_intern.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Intern @p s, returning the canonical rt_string for its byte content.
    /// @details Looks up the table by byte content.  On hit, increments the
    ///          canonical string's refcount and returns it.  On miss, inserts
    ///          @p s itself (retaining it for the table) and returns a new retained
    ///          reference to the caller.
    ///
    ///          After interning, two strings with equal content will share the same
    ///          pointer, enabling O(1) equality via @ref rt_string_interned_eq instead
    ///          of O(n) memcmp.
    ///
    /// @param s String to intern; must be non-NULL.
    /// @return Retained canonical rt_string; caller must call rt_string_unref() when done.
    rt_string rt_string_intern(rt_string s);

    /// @brief Test pointer equality for two interned strings.
    /// @details Both @p a and @p b must have been obtained from @ref rt_string_intern.
    ///          Returns 1 when they are the same canonical string (equal content),
    ///          0 otherwise — O(1) regardless of string length.
    /// @param a First interned string.
    /// @param b Second interned string.
    /// @return 1 if equal, 0 if not.
    static inline int rt_string_interned_eq(rt_string a, rt_string b)
    {
        return a == b;
    }

    /// @brief Release all interned strings and reset the table.
    /// @details Decrements the refcount of every string held by the table and frees
    ///          the table storage.  Primarily useful in tests to reset global state
    ///          between runs.  Must not be called concurrently with rt_string_intern().
    void rt_string_intern_drain(void);

#ifdef __cplusplus
}
#endif
