//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/fnmatch.c
// Purpose: Filename pattern matching for ViperDOS libc.
// Key invariants: POSIX fnmatch semantics; recursive implementation.
// Ownership/Lifetime: Library; stateless matching.
// Links: user/libc/include/fnmatch.h
//
//===----------------------------------------------------------------------===//

/**
 * @file fnmatch.c
 * @brief Filename pattern matching for ViperDOS libc.
 *
 * @details
 * This file implements POSIX filename pattern matching:
 *
 * - fnmatch: Match a filename against a shell wildcard pattern
 *
 * Supports the following pattern syntax:
 * - '*': Match zero or more characters
 * - '?': Match exactly one character
 * - '[...]': Match one character from a set or range
 * - '[!...]' or '[^...]': Match one character not in set
 * - '\\': Escape special characters (unless FNM_NOESCAPE)
 *
 * Flags control matching behavior: FNM_PATHNAME (/ handling),
 * FNM_PERIOD (leading . handling), FNM_NOESCAPE, FNM_CASEFOLD,
 * and FNM_LEADING_DIR.
 */

#include "../include/fnmatch.h"
#include "../include/ctype.h"
#include "../include/string.h"

/* Helper to fold case if FNM_CASEFOLD is set */
static inline int fold_case(int c, int flags)
{
    if (flags & FNM_CASEFOLD)
        return tolower(c);
    return c;
}

/* Helper to check if a character matches a bracket expression */
static int match_bracket(const char **pattern, int c, int flags)
{
    const char *p = *pattern;
    int negated = 0;
    int matched = 0;

    /* Check for negation */
    if (*p == '!' || *p == '^')
    {
        negated = 1;
        p++;
    }

    /* Fold case for comparison */
    c = fold_case(c, flags);

    /* Empty bracket expression is invalid, treat ] as literal */
    if (*p == ']')
    {
        if (c == ']')
            matched = 1;
        p++;
    }

    while (*p && *p != ']')
    {
        int start = (unsigned char)*p++;

        /* Handle escape */
        if (start == '\\' && !(flags & FNM_NOESCAPE) && *p)
        {
            start = (unsigned char)*p++;
        }

        /* Handle range */
        if (*p == '-' && *(p + 1) && *(p + 1) != ']')
        {
            p++; /* Skip '-' */
            int end = (unsigned char)*p++;

            if (end == '\\' && !(flags & FNM_NOESCAPE) && *p)
            {
                end = (unsigned char)*p++;
            }

            start = fold_case(start, flags);
            end = fold_case(end, flags);

            if (c >= start && c <= end)
                matched = 1;
        }
        else
        {
            start = fold_case(start, flags);
            if (c == start)
                matched = 1;
        }
    }

    /* Skip closing bracket */
    if (*p == ']')
        p++;

    *pattern = p;
    return negated ? !matched : matched;
}

/* Recursive fnmatch implementation */
static int fnmatch_internal(const char *pattern, const char *string, int flags, int at_start)
{
    while (*pattern)
    {
        char c = *pattern++;

        switch (c)
        {
            case '?':
                /* Match any single character */
                if (*string == '\0')
                    return FNM_NOMATCH;
                if ((flags & FNM_PATHNAME) && *string == '/')
                    return FNM_NOMATCH;
                if ((flags & FNM_PERIOD) && *string == '.' && at_start)
                    return FNM_NOMATCH;
                string++;
                at_start = 0;
                break;

            case '*':
                /* Skip consecutive stars */
                while (*pattern == '*')
                    pattern++;

                /* Check for leading period restriction */
                if ((flags & FNM_PERIOD) && *string == '.' && at_start)
                    return FNM_NOMATCH;

                /* Empty pattern after * matches everything */
                if (*pattern == '\0')
                {
                    if (flags & FNM_PATHNAME)
                    {
                        /* Must not match across '/' */
                        return strchr(string, '/') ? FNM_NOMATCH : 0;
                    }
                    return 0;
                }

                /* Try matching * against increasing prefixes */
                while (*string)
                {
                    if (fnmatch_internal(pattern, string, flags, 0) == 0)
                        return 0;

                    if ((flags & FNM_PATHNAME) && *string == '/')
                        break;

                    string++;
                }
                return FNM_NOMATCH;

            case '[':
                /* Bracket expression */
                if (*string == '\0')
                    return FNM_NOMATCH;
                if ((flags & FNM_PATHNAME) && *string == '/')
                    return FNM_NOMATCH;
                if ((flags & FNM_PERIOD) && *string == '.' && at_start)
                    return FNM_NOMATCH;

                if (!match_bracket(&pattern, (unsigned char)*string, flags))
                    return FNM_NOMATCH;

                string++;
                at_start = 0;
                break;

            case '\\':
                /* Escape character */
                if (!(flags & FNM_NOESCAPE))
                {
                    if (*pattern == '\0')
                        return FNM_NOMATCH;
                    c = *pattern++;
                }
                /* Fall through to literal match */
                /* fallthrough */

            default:
                /* Literal match */
                if (*string == '\0')
                    return FNM_NOMATCH;

                if (fold_case(c, flags) != fold_case((unsigned char)*string, flags))
                    return FNM_NOMATCH;

                /* Track '/' for FNM_PATHNAME and period matching */
                if (*string == '/')
                    at_start = 1;
                else
                    at_start = 0;

                string++;
                break;
        }
    }

    /* Pattern exhausted, check if string is exhausted too */
    if (*string == '\0')
        return 0;

    /* FNM_LEADING_DIR: trailing string after '/' is ignored */
    if ((flags & FNM_LEADING_DIR) && *string == '/')
        return 0;

    return FNM_NOMATCH;
}

/*
 * fnmatch - Match filename or pathname against pattern
 */
int fnmatch(const char *pattern, const char *string, int flags)
{
    if (!pattern || !string)
        return FNM_NOMATCH;

    return fnmatch_internal(pattern, string, flags, 1);
}
