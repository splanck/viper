//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_string_intern.h
// Purpose: Global string interning table — O(1) equality via pointer identity (P2-3.8).
// Key invariants: Each unique byte sequence maps to exactly one canonical rt_string.
//                 Interned strings are retained by the table and never freed during
//                 normal operation (immortal once interned).
//                 Thread-safe: concurrent intern calls are serialised by a mutex.
// Ownership/Lifetime: rt_string_intern() returns a retained pointer; caller must
//                     call rt_string_unref() when done (same as any rt_string).
// Links: rt_string.h (rt_string type and retain/release)
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
