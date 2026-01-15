/*
 * ViperDOS C Library - regex.h
 * POSIX regular expressions
 */

#ifndef _REGEX_H
#define _REGEX_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Offset type for regex matches */
    typedef ssize_t regoff_t;

    /* Size of compiled regex (opaque) */
    typedef struct
    {
        size_t re_nsub; /* Number of parenthesized subexpressions */
        /* Implementation-specific fields */
        void *__re_comp;  /* Compiled pattern */
        size_t __re_size; /* Size of compiled pattern */
        int __re_cflags;  /* Compilation flags */
    } regex_t;

    /* Subexpression match */
    typedef struct
    {
        regoff_t rm_so; /* Start offset of match */
        regoff_t rm_eo; /* End offset of match */
    } regmatch_t;

/*
 * Compilation flags (cflags)
 */
#define REG_EXTENDED 0x001 /* Use Extended Regular Expression syntax */
#define REG_ICASE 0x002    /* Ignore case in match */
#define REG_NOSUB 0x004    /* Report only success/fail in regexec() */
#define REG_NEWLINE 0x008  /* Treat newline as special */

/*
 * Execution flags (eflags)
 */
#define REG_NOTBOL 0x010 /* Start of string is not beginning of line */
#define REG_NOTEOL 0x020 /* End of string is not end of line */

/*
 * Error codes
 */
#define REG_NOMATCH 1  /* Pattern did not match */
#define REG_BADPAT 2   /* Invalid regular expression */
#define REG_ECOLLATE 3 /* Invalid collating element */
#define REG_ECTYPE 4   /* Invalid character class */
#define REG_EESCAPE 5  /* Trailing backslash */
#define REG_ESUBREG 6  /* Invalid backreference number */
#define REG_EBRACK 7   /* Unmatched '[' or '[^' */
#define REG_EPAREN 8   /* Unmatched '(' or '\\(' */
#define REG_EBRACE 9   /* Unmatched '{' or '\\{' */
#define REG_BADBR 10   /* Invalid content of \\{\\} */
#define REG_ERANGE 11  /* Invalid endpoint in range expression */
#define REG_ESPACE 12  /* Out of memory */
#define REG_BADRPT 13  /* Invalid use of repetition operators */

    /*
     * Compile a regular expression.
     * Returns 0 on success, error code on failure.
     */
    int regcomp(regex_t *preg, const char *regex, int cflags);

    /*
     * Execute a compiled regular expression.
     * Returns 0 if match found, REG_NOMATCH if not, error code on failure.
     */
    int regexec(
        const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags);

    /*
     * Free memory allocated by regcomp().
     */
    void regfree(regex_t *preg);

    /*
     * Get error message for error code.
     * Returns number of bytes required for full message.
     */
    size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size);

#ifdef __cplusplus
}
#endif

#endif /* _REGEX_H */
