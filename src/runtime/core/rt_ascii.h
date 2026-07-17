//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_ascii.h
// Purpose: Locale-independent ASCII character classification and folding.
//   Drop-in replacements for the <ctype.h> functions used by runtime text
//   helpers, guaranteeing identical results regardless of the embedding
//   process's LC_CTYPE (VDOC-063). Bytes above 0x7F are never letters,
//   digits, or whitespace and fold to themselves.
//
// Key invariants:
//   - Pure functions of the byte value; no locale, no global state.
//   - Safe for any int input; only [0, 127] can classify true.
//   - Header-only (static inline); usable from both C and C++.
//
// Ownership/Lifetime:
//   - Stateless; nothing to own.
//
// Links: src/runtime/core/rt_format.h (locale-independent number formatting)
//
//===----------------------------------------------------------------------===//
#pragma once

/// @brief Fold an ASCII uppercase letter to lowercase; other values unchanged.
static inline int rt_ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/// @brief Fold an ASCII lowercase letter to uppercase; other values unchanged.
static inline int rt_ascii_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

/// @brief True for ASCII 'a'-'z'.
static inline int rt_ascii_islower(int c)
{
    return c >= 'a' && c <= 'z';
}

/// @brief True for ASCII 'A'-'Z'.
static inline int rt_ascii_isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

/// @brief True for ASCII '0'-'9'.
static inline int rt_ascii_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

/// @brief True for ASCII letters.
static inline int rt_ascii_isalpha(int c)
{
    return rt_ascii_islower(c) || rt_ascii_isupper(c);
}

/// @brief True for ASCII letters and digits.
static inline int rt_ascii_isalnum(int c)
{
    return rt_ascii_isalpha(c) || rt_ascii_isdigit(c);
}

/// @brief True for the six ASCII whitespace bytes (space, \t, \n, \r, \f, \v).
static inline int rt_ascii_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
