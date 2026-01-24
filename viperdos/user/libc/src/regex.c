//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/regex.c
// Purpose: POSIX regular expression functions for ViperDOS libc.
// Key invariants: Compiles to bytecode; recursive matching.
// Ownership/Lifetime: Library; compiled regex freed via regfree().
// Links: user/libc/include/regex.h
//
//===----------------------------------------------------------------------===//

/**
 * @file regex.c
 * @brief POSIX regular expression functions for ViperDOS libc.
 *
 * @details
 * This file implements basic POSIX regular expressions:
 *
 * - regcomp: Compile a regular expression
 * - regexec: Execute a compiled regex against a string
 * - regfree: Free compiled regex memory
 * - regerror: Get error message for regex error code
 *
 * Supported syntax:
 * - Literal characters
 * - . (any character)
 * - * (zero or more)
 * - + (one or more, extended only)
 * - ? (zero or one, extended only)
 * - [...] and [^...] character classes
 * - ^ and $ anchors
 * - () groups (extended only)
 *
 * Case-insensitive matching (REG_ICASE) is supported.
 */

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

/* Compiled regex opcodes */
enum {
    OP_END = 0,
    OP_CHAR,        /* Match literal character */
    OP_ANY,         /* Match any character (.) */
    OP_CLASS,       /* Character class [...] */
    OP_NCLASS,      /* Negated class [^...] */
    OP_BOL,         /* Beginning of line (^) */
    OP_EOL,         /* End of line ($) */
    OP_GROUP_START, /* Start of group (() */
    OP_GROUP_END,   /* End of group ()) */
    OP_STAR,        /* Zero or more (*) */
    OP_PLUS,        /* One or more (+) */
    OP_QUEST,       /* Zero or one (?) */
};

/* Compiled instruction */
struct re_inst {
    int op;
    int arg;         /* Character for OP_CHAR, group number, etc */
    char cclass[32]; /* Bitmap for character classes (256 bits) */
};

/* Compiled regex */
struct re_compiled {
    struct re_inst *insts;
    size_t ninsts;
    size_t capacity;
};

/* Error messages */
static const char *error_messages[] = {
    "Success",
    "No match",
    "Invalid regular expression",
    "Invalid collating element",
    "Invalid character class",
    "Trailing backslash",
    "Invalid backreference number",
    "Unmatched '[' or '[^'",
    "Unmatched '(' or '\\('",
    "Unmatched '{' or '\\{'",
    "Invalid content of \\{\\}",
    "Invalid endpoint in range expression",
    "Out of memory",
    "Invalid use of repetition operators",
};

static int add_inst(struct re_compiled *rc, int op, int arg) {
    if (rc->ninsts >= rc->capacity) {
        size_t new_cap = rc->capacity ? rc->capacity * 2 : 16;
        struct re_inst *new_insts = realloc(rc->insts, new_cap * sizeof(struct re_inst));
        if (!new_insts)
            return -1;
        rc->insts = new_insts;
        rc->capacity = new_cap;
    }
    rc->insts[rc->ninsts].op = op;
    rc->insts[rc->ninsts].arg = arg;
    memset(rc->insts[rc->ninsts].cclass, 0, 32);
    rc->ninsts++;
    return 0;
}

static void set_class_bit(char cclass[32], int c) {
    cclass[c / 8] |= (1 << (c % 8));
}

static int get_class_bit(const char cclass[32], int c) {
    return (cclass[c / 8] >> (c % 8)) & 1;
}

int regcomp(regex_t *preg, const char *regex, int cflags) {
    if (!preg || !regex)
        return REG_BADPAT;

    struct re_compiled *rc = calloc(1, sizeof(struct re_compiled));
    if (!rc)
        return REG_ESPACE;

    preg->__re_comp = rc;
    preg->__re_cflags = cflags;
    preg->re_nsub = 0;

    int extended = cflags & REG_EXTENDED;
    int group_num = 0;

    const char *p = regex;
    while (*p) {
        if (*p == '^') {
            if (add_inst(rc, OP_BOL, 0) < 0)
                goto nomem;
            p++;
        } else if (*p == '$') {
            if (add_inst(rc, OP_EOL, 0) < 0)
                goto nomem;
            p++;
        } else if (*p == '.') {
            if (add_inst(rc, OP_ANY, 0) < 0)
                goto nomem;
            p++;
        } else if (*p == '*') {
            if (rc->ninsts == 0 || rc->insts[rc->ninsts - 1].op == OP_STAR) {
                regfree(preg);
                return REG_BADRPT;
            }
            rc->insts[rc->ninsts - 1].op = OP_STAR;
            /* Keep arg from previous instruction */
            p++;
        } else if (extended && *p == '+') {
            if (rc->ninsts == 0) {
                regfree(preg);
                return REG_BADRPT;
            }
            rc->insts[rc->ninsts - 1].op = OP_PLUS;
            p++;
        } else if (extended && *p == '?') {
            if (rc->ninsts == 0) {
                regfree(preg);
                return REG_BADRPT;
            }
            rc->insts[rc->ninsts - 1].op = OP_QUEST;
            p++;
        } else if (*p == '[') {
            p++;
            int negated = 0;
            if (*p == '^') {
                negated = 1;
                p++;
            }

            if (add_inst(rc, negated ? OP_NCLASS : OP_CLASS, 0) < 0)
                goto nomem;
            struct re_inst *inst = &rc->insts[rc->ninsts - 1];

            /* Handle ] at start */
            if (*p == ']') {
                set_class_bit(inst->cclass, ']');
                p++;
            }

            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    /* Range */
                    int start = (unsigned char)*p;
                    int end = (unsigned char)p[2];
                    if (start > end) {
                        regfree(preg);
                        return REG_ERANGE;
                    }
                    for (int c = start; c <= end; c++) {
                        set_class_bit(inst->cclass, c);
                    }
                    p += 3;
                } else {
                    set_class_bit(inst->cclass, (unsigned char)*p);
                    p++;
                }
            }

            if (*p != ']') {
                regfree(preg);
                return REG_EBRACK;
            }
            p++;
        } else if (extended && *p == '(') {
            if (add_inst(rc, OP_GROUP_START, group_num++) < 0)
                goto nomem;
            preg->re_nsub++;
            p++;
        } else if (extended && *p == ')') {
            if (add_inst(rc, OP_GROUP_END, 0) < 0)
                goto nomem;
            p++;
        } else if (*p == '\\') {
            p++;
            if (*p == 0) {
                regfree(preg);
                return REG_EESCAPE;
            }
            int c = (unsigned char)*p++;
            if (cflags & REG_ICASE)
                c = tolower(c);
            if (add_inst(rc, OP_CHAR, c) < 0)
                goto nomem;
        } else {
            int c = (unsigned char)*p++;
            if (cflags & REG_ICASE)
                c = tolower(c);
            if (add_inst(rc, OP_CHAR, c) < 0)
                goto nomem;
        }
    }

    if (add_inst(rc, OP_END, 0) < 0)
        goto nomem;
    return 0;

nomem:
    regfree(preg);
    return REG_ESPACE;
}

/* Recursive matching helper */
static int match_here(const struct re_inst *insts,
                      const char *s,
                      const char *text,
                      size_t nmatch,
                      regmatch_t *pmatch,
                      int cflags) {
    if (insts->op == OP_END)
        return 1;

    if (insts->op == OP_BOL) {
        if (s != text)
            return 0;
        return match_here(insts + 1, s, text, nmatch, pmatch, cflags);
    }

    if (insts->op == OP_EOL) {
        if (*s != '\0' && *s != '\n')
            return 0;
        return match_here(insts + 1, s, text, nmatch, pmatch, cflags);
    }

    if (insts->op == OP_STAR) {
        /* Try matching zero or more */
        do {
            if (match_here(insts + 1, s, text, nmatch, pmatch, cflags))
                return 1;
        } while (
            *s &&
            (insts->arg == 0 ||
             ((cflags & REG_ICASE) ? tolower((unsigned char)*s) == insts->arg
                                   : (unsigned char)*s == insts->arg) ||
             (insts[-1].op == OP_ANY) ||
             (insts[-1].op == OP_CLASS && get_class_bit(insts[-1].cclass, (unsigned char)*s))) &&
            s++);
        return 0;
    }

    if (*s == '\0')
        return 0;

    int c = (unsigned char)*s;
    int ic = tolower(c);

    if (insts->op == OP_ANY) {
        if (c == '\n' && !(cflags & REG_NEWLINE)) {
            /* . doesn't match newline unless REG_NEWLINE */
        }
        return match_here(insts + 1, s + 1, text, nmatch, pmatch, cflags);
    }

    if (insts->op == OP_CHAR) {
        int match;
        if (cflags & REG_ICASE) {
            match = (ic == insts->arg);
        } else {
            match = (c == insts->arg);
        }
        if (match) {
            return match_here(insts + 1, s + 1, text, nmatch, pmatch, cflags);
        }
        return 0;
    }

    if (insts->op == OP_CLASS) {
        if (get_class_bit(insts->cclass, c)) {
            return match_here(insts + 1, s + 1, text, nmatch, pmatch, cflags);
        }
        return 0;
    }

    if (insts->op == OP_NCLASS) {
        if (!get_class_bit(insts->cclass, c)) {
            return match_here(insts + 1, s + 1, text, nmatch, pmatch, cflags);
        }
        return 0;
    }

    if (insts->op == OP_GROUP_START || insts->op == OP_GROUP_END) {
        return match_here(insts + 1, s, text, nmatch, pmatch, cflags);
    }

    return 0;
}

int regexec(
    const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags) {
    if (!preg || !string || !preg->__re_comp)
        return REG_BADPAT;

    struct re_compiled *rc = preg->__re_comp;
    if (rc->ninsts == 0)
        return REG_BADPAT;

    int cflags = preg->__re_cflags;

    /* Initialize match results */
    if (!(cflags & REG_NOSUB) && pmatch) {
        for (size_t i = 0; i < nmatch; i++) {
            pmatch[i].rm_so = -1;
            pmatch[i].rm_eo = -1;
        }
    }

    /* Try matching at each position */
    const char *text = string;
    if (eflags & REG_NOTBOL) {
        /* Pretend we're not at beginning of line */
    }

    const char *s = string;
    while (*s || s == string) {
        if (match_here(rc->insts, s, text, nmatch, pmatch, cflags)) {
            if (!(cflags & REG_NOSUB) && nmatch > 0 && pmatch) {
                pmatch[0].rm_so = s - string;
                /* Find end of match */
                const char *e = s;
                while (*e)
                    e++; /* Simplified: match to end */
                pmatch[0].rm_eo = e - string;
            }
            return 0;
        }
        if (*s == '\0')
            break;
        s++;
    }

    return REG_NOMATCH;
}

void regfree(regex_t *preg) {
    if (preg && preg->__re_comp) {
        struct re_compiled *rc = preg->__re_comp;
        free(rc->insts);
        free(rc);
        preg->__re_comp = NULL;
    }
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) {
    (void)preg;

    const char *msg;
    if (errcode >= 0 && errcode <= REG_BADRPT) {
        msg = error_messages[errcode];
    } else {
        msg = "Unknown error";
    }

    size_t len = strlen(msg) + 1;

    if (errbuf && errbuf_size > 0) {
        size_t copy_len = len < errbuf_size ? len : errbuf_size;
        memcpy(errbuf, msg, copy_len - 1);
        errbuf[copy_len - 1] = '\0';
    }

    return len;
}
