/*
 * ViperOS libc - wordexp.h
 * Word expansion functions
 */

#ifndef _WORDEXP_H
#define _WORDEXP_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Word expansion result structure
     */
    typedef struct
    {
        size_t we_wordc; /* Count of words matched */
        char **we_wordv; /* Pointer to list of words */
        size_t we_offs;  /* Slots to reserve at beginning of we_wordv */
    } wordexp_t;

/*
 * Flags for wordexp()
 */
#define WRDE_APPEND (1 << 0)  /* Append to existing words */
#define WRDE_DOOFFS (1 << 1)  /* Use we_offs */
#define WRDE_NOCMD (1 << 2)   /* Fail on command substitution */
#define WRDE_REUSE (1 << 3)   /* Previous result was from wordexp */
#define WRDE_SHOWERR (1 << 4) /* Don't redirect stderr to /dev/null */
#define WRDE_UNDEF (1 << 5)   /* Undefined variables error */

/*
 * Error return values
 */
#define WRDE_BADCHAR 1 /* Illegal character found */
#define WRDE_BADVAL 2  /* Undefined variable reference */
#define WRDE_CMDSUB 3  /* Command substitution not allowed */
#define WRDE_NOSPACE 4 /* Out of memory */
#define WRDE_SYNTAX 5  /* Shell syntax error */

    /*
     * wordexp - Perform word expansion
     *
     * Expands shell-style words with tilde expansion, parameter expansion,
     * command substitution (if allowed), and field splitting.
     *
     * @words: The string to expand
     * @pwordexp: Where to store expansion results
     * @flags: Expansion flags
     *
     * Returns 0 on success, error code on failure.
     */
    int wordexp(const char *words, wordexp_t *pwordexp, int flags);

    /*
     * wordfree - Free word expansion results
     *
     * Frees memory allocated by wordexp().
     *
     * @pwordexp: The wordexp_t structure to free
     */
    void wordfree(wordexp_t *pwordexp);

#ifdef __cplusplus
}
#endif

#endif /* _WORDEXP_H */
