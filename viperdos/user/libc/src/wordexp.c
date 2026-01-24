//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/wordexp.c
// Purpose: Shell word expansion functions for ViperDOS libc.
// Key invariants: No command substitution; supports ~, $VAR, quotes.
// Ownership/Lifetime: Library; caller frees via wordfree().
// Links: user/libc/include/wordexp.h
//
//===----------------------------------------------------------------------===//

/**
 * @file wordexp.c
 * @brief Shell word expansion functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX word expansion:
 *
 * - wordexp: Perform shell-like word expansion
 * - wordfree: Free word expansion results
 *
 * Supported expansion features:
 * - Tilde expansion (~, ~/path)
 * - Variable expansion ($VAR, ${VAR})
 * - Quote handling (single and double quotes)
 * - Escape sequences with backslash
 *
 * Not supported:
 * - Command substitution (`cmd` or $(cmd)) - returns WRDE_CMDSUB
 * - Arithmetic expansion
 * - Glob/pathname expansion
 *
 * Flags: WRDE_APPEND, WRDE_DOOFFS, WRDE_NOCMD, WRDE_UNDEF.
 */

#include "../include/wordexp.h"
#include "../include/ctype.h"
#include "../include/errno.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Initial allocation size for word list */
#define INITIAL_WORDS 8

/*
 * Helper: Check if character is special shell character
 */
static int is_special_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')' ||
           c == '{' || c == '}';
}

/*
 * Helper: Add a word to the word list
 */
static int add_word(wordexp_t *we, const char *word, size_t len, size_t *capacity) {
    /* Check if we need to grow the array */
    if (we->we_wordc + we->we_offs >= *capacity) {
        size_t new_cap = *capacity * 2;
        char **new_wordv = (char **)realloc(we->we_wordv, new_cap * sizeof(char *));
        if (!new_wordv) {
            return WRDE_NOSPACE;
        }
        we->we_wordv = new_wordv;
        *capacity = new_cap;
    }

    /* Allocate and copy the word */
    char *word_copy = (char *)malloc(len + 1);
    if (!word_copy) {
        return WRDE_NOSPACE;
    }
    memcpy(word_copy, word, len);
    word_copy[len] = '\0';

    we->we_wordv[we->we_offs + we->we_wordc] = word_copy;
    we->we_wordc++;

    return 0;
}

/*
 * Helper: Expand tilde in path
 */
static char *expand_tilde(const char *str, size_t *len) {
    if (str[0] != '~') {
        return NULL;
    }

    /* Get home directory from environment */
    const char *home = getenv("HOME");
    if (!home) {
        home = "/";
    }

    size_t home_len = strlen(home);
    size_t rest_start = 1;

    /* Find end of username (if any) */
    while (str[rest_start] && str[rest_start] != '/') {
        rest_start++;
    }

    /* For now, only support plain ~ (no ~user) */
    if (rest_start > 1) {
        return NULL;
    }

    size_t rest_len = strlen(str + rest_start);
    size_t total_len = home_len + rest_len;

    char *result = (char *)malloc(total_len + 1);
    if (!result) {
        return NULL;
    }

    memcpy(result, home, home_len);
    memcpy(result + home_len, str + rest_start, rest_len);
    result[total_len] = '\0';

    *len = total_len;
    return result;
}

/*
 * Helper: Expand environment variable
 */
static char *expand_variable(const char *str, size_t *consumed) {
    if (str[0] != '$') {
        *consumed = 0;
        return NULL;
    }

    const char *start;
    size_t name_len;

    if (str[1] == '{') {
        start = str + 2;
        const char *end = strchr(start, '}');
        if (!end) {
            *consumed = 0;
            return NULL;
        }
        name_len = end - start;
        *consumed = name_len + 3; /* ${ + name + } */
    } else if (isalpha((unsigned char)str[1]) || str[1] == '_') {
        start = str + 1;
        name_len = 0;
        while (isalnum((unsigned char)start[name_len]) || start[name_len] == '_') {
            name_len++;
        }
        *consumed = name_len + 1; /* $ + name */
    } else {
        *consumed = 0;
        return NULL;
    }

    /* Extract variable name */
    char name[256];
    if (name_len >= sizeof(name)) {
        name_len = sizeof(name) - 1;
    }
    memcpy(name, start, name_len);
    name[name_len] = '\0';

    /* Look up value */
    const char *value = getenv(name);
    if (!value) {
        value = "";
    }

    return strdup(value);
}

/*
 * wordexp - Perform word expansion
 */
int wordexp(const char *words, wordexp_t *pwordexp, int flags) {
    if (!words || !pwordexp) {
        return WRDE_BADCHAR;
    }

    int append = flags & WRDE_APPEND;
    int use_offs = flags & WRDE_DOOFFS;
    int nocmd = flags & WRDE_NOCMD;
    int undef = flags & WRDE_UNDEF;

    /* Initialize or prepare for append */
    if (!append) {
        pwordexp->we_wordc = 0;
        pwordexp->we_wordv = NULL;
        if (!use_offs) {
            pwordexp->we_offs = 0;
        }
    }

    /* Allocate initial word array */
    size_t capacity = INITIAL_WORDS + pwordexp->we_offs;
    if (append && pwordexp->we_wordv) {
        capacity = pwordexp->we_wordc + pwordexp->we_offs + INITIAL_WORDS;
    }

    if (!append || !pwordexp->we_wordv) {
        pwordexp->we_wordv = (char **)malloc(capacity * sizeof(char *));
        if (!pwordexp->we_wordv) {
            return WRDE_NOSPACE;
        }

        /* Initialize offset slots to NULL */
        for (size_t i = 0; i < pwordexp->we_offs; i++) {
            pwordexp->we_wordv[i] = NULL;
        }
    }

    /* Parse and expand words */
    const char *p = words;
    char word_buf[4096];
    size_t word_len = 0;
    int in_single_quote = 0;
    int in_double_quote = 0;

    while (*p) {
        /* Skip whitespace between words */
        if (!in_single_quote && !in_double_quote && isspace((unsigned char)*p)) {
            if (word_len > 0) {
                int err = add_word(pwordexp, word_buf, word_len, &capacity);
                if (err) {
                    wordfree(pwordexp);
                    return err;
                }
                word_len = 0;
            }
            p++;
            continue;
        }

        /* Check for command substitution */
        if (!in_single_quote && (*p == '`' || (*p == '$' && p[1] == '('))) {
            if (nocmd) {
                wordfree(pwordexp);
                return WRDE_CMDSUB;
            }
            /* Command substitution not supported in ViperDOS */
            wordfree(pwordexp);
            return WRDE_CMDSUB;
        }

        /* Check for special characters */
        if (!in_single_quote && !in_double_quote && is_special_char(*p)) {
            wordfree(pwordexp);
            return WRDE_BADCHAR;
        }

        /* Handle quotes */
        if (*p == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            p++;
            continue;
        }

        if (*p == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            p++;
            continue;
        }

        /* Handle escape */
        if (*p == '\\' && !in_single_quote) {
            p++;
            if (*p) {
                if (word_len < sizeof(word_buf) - 1) {
                    word_buf[word_len++] = *p;
                }
                p++;
            }
            continue;
        }

        /* Tilde expansion (only at start of word) */
        if (*p == '~' && word_len == 0 && !in_single_quote) {
            size_t expanded_len;
            char *expanded = expand_tilde(p, &expanded_len);
            if (expanded) {
                size_t copy_len = expanded_len;
                if (word_len + copy_len > sizeof(word_buf) - 1) {
                    copy_len = sizeof(word_buf) - 1 - word_len;
                }
                memcpy(word_buf + word_len, expanded, copy_len);
                word_len += copy_len;

                /* Skip the tilde portion in input */
                p++;
                while (*p && *p != '/' && !isspace((unsigned char)*p)) {
                    p++;
                }

                free(expanded);
                continue;
            }
        }

        /* Variable expansion */
        if (*p == '$' && !in_single_quote) {
            size_t consumed;
            char *expanded = expand_variable(p, &consumed);
            if (consumed > 0) {
                if (expanded) {
                    size_t expanded_len = strlen(expanded);
                    size_t copy_len = expanded_len;
                    if (word_len + copy_len > sizeof(word_buf) - 1) {
                        copy_len = sizeof(word_buf) - 1 - word_len;
                    }
                    memcpy(word_buf + word_len, expanded, copy_len);
                    word_len += copy_len;
                    free(expanded);
                } else if (undef) {
                    wordfree(pwordexp);
                    return WRDE_BADVAL;
                }
                p += consumed;
                continue;
            }
        }

        /* Regular character */
        if (word_len < sizeof(word_buf) - 1) {
            word_buf[word_len++] = *p;
        }
        p++;
    }

    /* Check for unterminated quotes */
    if (in_single_quote || in_double_quote) {
        wordfree(pwordexp);
        return WRDE_SYNTAX;
    }

    /* Add final word if any */
    if (word_len > 0) {
        int err = add_word(pwordexp, word_buf, word_len, &capacity);
        if (err) {
            wordfree(pwordexp);
            return err;
        }
    }

    /* NULL-terminate the word list */
    if (pwordexp->we_wordc + pwordexp->we_offs >= capacity) {
        char **new_wordv = (char **)realloc(pwordexp->we_wordv, (capacity + 1) * sizeof(char *));
        if (!new_wordv) {
            wordfree(pwordexp);
            return WRDE_NOSPACE;
        }
        pwordexp->we_wordv = new_wordv;
    }
    pwordexp->we_wordv[pwordexp->we_offs + pwordexp->we_wordc] = NULL;

    return 0;
}

/*
 * wordfree - Free word expansion results
 */
void wordfree(wordexp_t *pwordexp) {
    if (!pwordexp || !pwordexp->we_wordv) {
        return;
    }

    /* Free each word (skip the offset slots) */
    for (size_t i = pwordexp->we_offs; i < pwordexp->we_offs + pwordexp->we_wordc; i++) {
        free(pwordexp->we_wordv[i]);
    }

    free(pwordexp->we_wordv);
    pwordexp->we_wordv = NULL;
    pwordexp->we_wordc = 0;
}
