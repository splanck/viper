//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/ctype.c
// Purpose: Character classification and conversion functions.
// Key invariants: Standard C semantics; ASCII-only implementation.
// Ownership/Lifetime: Library; all functions are stateless and pure.
// Links: user/libc/include/ctype.h
//
//===----------------------------------------------------------------------===//

/**
 * @file ctype.c
 * @brief Character classification and conversion functions for ViperDOS libc.
 *
 * @details
 * This file implements the standard C character handling functions:
 *
 * - Classification: isalpha, isdigit, isalnum, isspace, isupper, islower, etc.
 * - Conversion: toupper, tolower
 *
 * All functions operate on ASCII characters (0-127). Characters outside
 * this range return 0 for classification functions and are returned unchanged
 * by conversion functions.
 */

#include "../include/ctype.h"

/**
 * @brief Test if character is an alphabetic letter.
 *
 * @details
 * Returns non-zero if c is an uppercase (A-Z) or lowercase (a-z) letter.
 *
 * @param c Character to test (as unsigned char cast to int, or EOF).
 * @return Non-zero if alphabetic, 0 otherwise.
 */
int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * @brief Test if character is a decimal digit.
 *
 * @details
 * Returns non-zero if c is a digit character ('0' through '9').
 *
 * @param c Character to test.
 * @return Non-zero if digit, 0 otherwise.
 */
int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief Test if character is alphanumeric.
 *
 * @details
 * Returns non-zero if c is a letter (A-Z, a-z) or digit (0-9).
 * Equivalent to (isalpha(c) || isdigit(c)).
 *
 * @param c Character to test.
 * @return Non-zero if alphanumeric, 0 otherwise.
 */
int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

/**
 * @brief Test if character is a blank (space or tab).
 *
 * @details
 * Returns non-zero if c is a space (' ') or horizontal tab ('\t').
 * This is a subset of isspace().
 *
 * @param c Character to test.
 * @return Non-zero if blank, 0 otherwise.
 */
int isblank(int c)
{
    return c == ' ' || c == '\t';
}

/**
 * @brief Test if character is a control character.
 *
 * @details
 * Returns non-zero if c is a control character (ASCII 0-31 or 127/DEL).
 * Control characters are non-printable characters used for device control.
 *
 * @param c Character to test.
 * @return Non-zero if control character, 0 otherwise.
 */
int iscntrl(int c)
{
    return (c >= 0 && c < 32) || c == 127;
}

/**
 * @brief Test if character is a printable graphic character.
 *
 * @details
 * Returns non-zero if c has a visible graphical representation.
 * This excludes the space character (use isprint() to include space).
 *
 * @param c Character to test.
 * @return Non-zero if graphic character, 0 otherwise.
 */
int isgraph(int c)
{
    return c > 32 && c < 127;
}

/**
 * @brief Test if character is a lowercase letter.
 *
 * @details
 * Returns non-zero if c is a lowercase letter ('a' through 'z').
 *
 * @param c Character to test.
 * @return Non-zero if lowercase, 0 otherwise.
 */
int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

/**
 * @brief Test if character is an uppercase letter.
 *
 * @details
 * Returns non-zero if c is an uppercase letter ('A' through 'Z').
 *
 * @param c Character to test.
 * @return Non-zero if uppercase, 0 otherwise.
 */
int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

/**
 * @brief Test if character is printable (including space).
 *
 * @details
 * Returns non-zero if c is a printable character, including space.
 * This includes all graphic characters plus the space character.
 *
 * @param c Character to test.
 * @return Non-zero if printable, 0 otherwise.
 */
int isprint(int c)
{
    return c >= 32 && c < 127;
}

/**
 * @brief Test if character is a punctuation character.
 *
 * @details
 * Returns non-zero if c is a printable character that is neither
 * alphanumeric nor a space. This includes characters like !, @, #, etc.
 *
 * @param c Character to test.
 * @return Non-zero if punctuation, 0 otherwise.
 */
int ispunct(int c)
{
    return isgraph(c) && !isalnum(c);
}

/**
 * @brief Test if character is whitespace.
 *
 * @details
 * Returns non-zero if c is a whitespace character: space (' '),
 * horizontal tab ('\t'), newline ('\n'), vertical tab ('\v'),
 * form feed ('\f'), or carriage return ('\r').
 *
 * @param c Character to test.
 * @return Non-zero if whitespace, 0 otherwise.
 */
int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/**
 * @brief Test if character is a hexadecimal digit.
 *
 * @details
 * Returns non-zero if c is a hexadecimal digit: 0-9, A-F, or a-f.
 *
 * @param c Character to test.
 * @return Non-zero if hex digit, 0 otherwise.
 */
int isxdigit(int c)
{
    return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/**
 * @brief Convert uppercase letter to lowercase.
 *
 * @details
 * If c is an uppercase letter (A-Z), returns the corresponding lowercase
 * letter (a-z). Otherwise returns c unchanged.
 *
 * @param c Character to convert.
 * @return Lowercase equivalent or original character.
 */
int tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c + 32;
    }
    return c;
}

/**
 * @brief Convert lowercase letter to uppercase.
 *
 * @details
 * If c is a lowercase letter (a-z), returns the corresponding uppercase
 * letter (A-Z). Otherwise returns c unchanged.
 *
 * @param c Character to convert.
 * @return Uppercase equivalent or original character.
 */
int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
    {
        return c - 32;
    }
    return c;
}
