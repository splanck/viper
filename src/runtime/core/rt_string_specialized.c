//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_specialized.c
// Purpose: Specialized string operations for the Viper runtime. Contains case
//   conversion utilities (capitalize, title, camelCase, PascalCase, snake_case,
//   kebab-case, screaming_snake), string distance metrics (Levenshtein, Jaro,
//   Jaro-Winkler, Hamming), prefix/suffix manipulation, slug generation, and
//   SQL LIKE pattern matching.
//
// Key invariants:
//   - Case conversions use a shared split_words() helper that handles camelCase
//     word boundaries, separators, and digit transitions.
//   - Distance metrics return int64_t values (edit distance or similarity scaled
//     to 0-10000 for Jaro/Jaro-Winkler).
//   - LIKE matching supports % (any sequence) and _ (single char) wildcards.
//
// Ownership/Lifetime:
//   - Returned strings are heap-allocated with reference counting.
//   - Temporary buffers (word arrays, DP tables) are stack or malloc'd and freed.
//
// Links: src/runtime/core/rt_string_internal.h (shared helpers),
//        src/runtime/core/rt_string_ops.c (core operations),
//        src/runtime/core/rt_string.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_string_internal.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Extended String Utilities
//=============================================================================

rt_string rt_str_capitalize(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_alloc(len, len + 1);
    if (!result)
        return NULL;
    memcpy(result->data, str->data, len);
    result->data[len] = '\0';
    result->data[0] = (char)toupper((unsigned char)result->data[0]);
    return result;
}

rt_string rt_str_title(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    rt_string result = rt_string_alloc(len, len + 1);
    if (!result)
        return NULL;
    memcpy(result->data, str->data, len);
    result->data[len] = '\0';

    int capitalize_next = 1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)result->data[i];
        if (isspace(c)) {
            capitalize_next = 1;
        } else if (capitalize_next) {
            result->data[i] = (char)toupper(c);
            capitalize_next = 0;
        }
    }
    return result;
}

rt_string rt_str_remove_prefix(rt_string str, rt_string prefix) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t slen = rt_string_len_bytes(str);
    if (!prefix)
        return rt_string_from_bytes(str->data, slen);

    size_t plen = rt_string_len_bytes(prefix);
    if (plen == 0 || plen > slen)
        return rt_string_from_bytes(str->data, slen);

    if (memcmp(str->data, prefix->data, plen) == 0)
        return rt_string_from_bytes(str->data + plen, slen - plen);

    return rt_string_from_bytes(str->data, slen);
}

rt_string rt_str_remove_suffix(rt_string str, rt_string suffix) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t slen = rt_string_len_bytes(str);
    if (!suffix)
        return rt_string_from_bytes(str->data, slen);

    size_t xlen = rt_string_len_bytes(suffix);
    if (xlen == 0 || xlen > slen)
        return rt_string_from_bytes(str->data, slen);

    if (memcmp(str->data + slen - xlen, suffix->data, xlen) == 0)
        return rt_string_from_bytes(str->data, slen - xlen);

    return rt_string_from_bytes(str->data, slen);
}

int64_t rt_str_last_index_of(rt_string haystack, rt_string needle) {
    if (!haystack || !needle)
        return 0;
    size_t hlen = rt_string_len_bytes(haystack);
    size_t nlen = rt_string_len_bytes(needle);
    if (nlen == 0 || nlen > hlen)
        return 0;

    for (size_t i = hlen - nlen + 1; i > 0; i--) {
        if (memcmp(haystack->data + i - 1, needle->data, nlen) == 0)
            return (int64_t)i; // 1-based
    }
    return 0;
}

rt_string rt_str_trim_char(rt_string str, rt_string ch) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0 || !ch)
        return rt_string_from_bytes(str->data, len);

    size_t chlen = rt_string_len_bytes(ch);
    if (chlen == 0)
        return rt_string_from_bytes(str->data, len);

    char trim_ch = ch->data[0];

    size_t start = 0;
    while (start < len && str->data[start] == trim_ch)
        start++;

    size_t end = len;
    while (end > start && str->data[end - 1] == trim_ch)
        end--;

    return rt_string_from_bytes(str->data + start, end - start);
}

rt_string rt_str_slug(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);

    char *buf = (char *)malloc(len + 1);
    if (!buf)
        return rt_string_from_bytes("", 0);

    size_t out = 0;
    int last_was_sep = 1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str->data[i];
        if (isalnum(c)) {
            buf[out++] = (char)tolower(c);
            last_was_sep = 0;
        } else if (!last_was_sep) {
            buf[out++] = '-';
            last_was_sep = 1;
        }
    }
    if (out > 0 && buf[out - 1] == '-')
        out--;

    rt_string result = rt_string_from_bytes(buf, out);
    free(buf);
    return result;
}

// ---------------------------------------------------------------------------
// String Similarity / Distance
// ---------------------------------------------------------------------------

int64_t rt_str_levenshtein(rt_string a, rt_string b) {
    if (!a && !b)
        return 0;
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    if (alen == 0)
        return (int64_t)blen;
    if (blen == 0)
        return (int64_t)alen;

    const char *sa = rt_string_cstr(a);
    const char *sb = rt_string_cstr(b);

    // Use single-row DP to save memory: O(min(m,n)) space
    // Ensure blen is the smaller dimension
    if (alen < blen) {
        const char *tmp_s = sa;
        sa = sb;
        sb = tmp_s;
        size_t tmp_n = alen;
        alen = blen;
        blen = tmp_n;
    }

    size_t *row = (size_t *)malloc((blen + 1) * sizeof(size_t));
    if (!row)
        return -1;

    for (size_t j = 0; j <= blen; ++j)
        row[j] = j;

    for (size_t i = 1; i <= alen; ++i) {
        size_t prev = row[0];
        row[0] = i;
        for (size_t j = 1; j <= blen; ++j) {
            size_t cost = (sa[i - 1] == sb[j - 1]) ? 0 : 1;
            size_t del = row[j] + 1;
            size_t ins = row[j - 1] + 1;
            size_t sub = prev + cost;

            size_t min = del < ins ? del : ins;
            if (sub < min)
                min = sub;

            prev = row[j];
            row[j] = min;
        }
    }

    int64_t result = (int64_t)row[blen];
    free(row);
    return result;
}

double rt_str_jaro(rt_string a, rt_string b) {
    if (!a && !b)
        return 1.0;
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    if (alen == 0 && blen == 0)
        return 1.0;
    if (alen == 0 || blen == 0)
        return 0.0;

    const char *sa = rt_string_cstr(a);
    const char *sb = rt_string_cstr(b);

    size_t max_len = alen > blen ? alen : blen;
    size_t match_dist = (max_len / 2) > 0 ? (max_len / 2) - 1 : 0;

    int8_t *a_matched = (int8_t *)calloc(alen, sizeof(int8_t));
    int8_t *b_matched = (int8_t *)calloc(blen, sizeof(int8_t));
    if (!a_matched || !b_matched) {
        free(a_matched);
        free(b_matched);
        return 0.0;
    }

    double matches = 0;
    double transpositions = 0;

    for (size_t i = 0; i < alen; ++i) {
        size_t start = (i > match_dist) ? i - match_dist : 0;
        size_t end = i + match_dist + 1;
        if (end > blen)
            end = blen;

        for (size_t j = start; j < end; ++j) {
            if (b_matched[j] || sa[i] != sb[j])
                continue;
            a_matched[i] = 1;
            b_matched[j] = 1;
            matches++;
            break;
        }
    }

    if (matches == 0.0) {
        free(a_matched);
        free(b_matched);
        return 0.0;
    }

    // Count transpositions
    size_t k = 0;
    for (size_t i = 0; i < alen; ++i) {
        if (!a_matched[i])
            continue;
        while (!b_matched[k])
            ++k;
        if (sa[i] != sb[k])
            transpositions++;
        ++k;
    }

    free(a_matched);
    free(b_matched);

    return (matches / (double)alen + matches / (double)blen +
            (matches - transpositions / 2.0) / matches) /
           3.0;
}

double rt_str_jaro_winkler(rt_string a, rt_string b) {
    double jaro = rt_str_jaro(a, b);

    // Compute common prefix length (up to 4)
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    size_t max_prefix = 4;
    if (alen < max_prefix)
        max_prefix = alen;
    if (blen < max_prefix)
        max_prefix = blen;

    const char *sa = a ? rt_string_cstr(a) : "";
    const char *sb = b ? rt_string_cstr(b) : "";

    size_t prefix = 0;
    for (size_t i = 0; i < max_prefix; ++i) {
        if (sa[i] == sb[i])
            prefix++;
        else
            break;
    }

    double p = 0.1; // Winkler scaling factor
    return jaro + (double)prefix * p * (1.0 - jaro);
}

int64_t rt_str_hamming(rt_string a, rt_string b) {
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    if (alen != blen)
        return -1;
    if (alen == 0)
        return 0;

    const char *sa = rt_string_cstr(a);
    const char *sb = rt_string_cstr(b);
    int64_t dist = 0;
    for (size_t i = 0; i < alen; ++i) {
        if (sa[i] != sb[i])
            dist++;
    }
    return dist;
}

// ---------------------------------------------------------------------------
// Case conversion utilities
// ---------------------------------------------------------------------------

// Helper: check if char is a word separator
static int is_separator(char c) {
    return c == ' ' || c == '_' || c == '-' || c == '\t';
}

// Helper: split a string into words, handling camelCase boundaries too.
// Writes words into a flat buffer with null terminators, returns word count.
// words[] points into buf.
static int split_words(
    const char *src, size_t len, char *buf, size_t buf_cap, const char **words, int max_words) {
    int wcount = 0;
    size_t bpos = 0;

    size_t i = 0;
    while (i < len && wcount < max_words) {
        // Skip separators
        while (i < len && is_separator(src[i]))
            ++i;
        if (i >= len)
            break;

        // Start of a word
        words[wcount] = buf + bpos;

        // Collect word characters
        while (i < len && !is_separator(src[i])) {
            // Detect camelCase boundary: lowercase followed by uppercase
            if (i + 1 < len && islower((unsigned char)src[i]) &&
                isupper((unsigned char)src[i + 1])) {
                if (bpos < buf_cap)
                    buf[bpos++] = src[i];
                ++i;
                break; // End this word, next word starts with uppercase
            }
            // Detect ACRONYM boundary: multiple uppercase followed by lowercase
            if (i + 2 < len && isupper((unsigned char)src[i]) &&
                isupper((unsigned char)src[i + 1]) && islower((unsigned char)src[i + 2])) {
                if (bpos < buf_cap)
                    buf[bpos++] = src[i];
                ++i;
                break;
            }
            if (bpos < buf_cap)
                buf[bpos++] = src[i];
            ++i;
        }
        if (bpos < buf_cap)
            buf[bpos++] = '\0';
        ++wcount;
    }

    return wcount;
}

rt_string rt_str_camel_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = (char *)malloc(len + 256);
    if (!wbuf)
        rt_trap("string_ops: memory allocation failed");
    const char *words[128];
    int wc = split_words(src, len, wbuf, len + 256, words, 128);

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        const char *word = words[w];
        size_t wlen = strlen(word);
        if (wlen == 0)
            continue;

        char first = (w == 0) ? (char)tolower((unsigned char)word[0])
                              : (char)toupper((unsigned char)word[0]);
        rt_sb_append_bytes(&sb, &first, 1);
        for (size_t j = 1; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            rt_sb_append_bytes(&sb, &c, 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(wbuf);
    return result;
}

rt_string rt_str_pascal_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = (char *)malloc(len + 256);
    if (!wbuf)
        rt_trap("string_ops: memory allocation failed");
    const char *words[128];
    int wc = split_words(src, len, wbuf, len + 256, words, 128);

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        const char *word = words[w];
        size_t wlen = strlen(word);
        if (wlen == 0)
            continue;

        char first = (char)toupper((unsigned char)word[0]);
        rt_sb_append_bytes(&sb, &first, 1);
        for (size_t j = 1; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            rt_sb_append_bytes(&sb, &c, 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(wbuf);
    return result;
}

rt_string rt_str_snake_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = (char *)malloc(len + 256);
    if (!wbuf)
        rt_trap("string_ops: memory allocation failed");
    const char *words[128];
    int wc = split_words(src, len, wbuf, len + 256, words, 128);

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0)
            rt_sb_append_bytes(&sb, "_", 1);
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            rt_sb_append_bytes(&sb, &c, 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(wbuf);
    return result;
}

rt_string rt_str_kebab_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = (char *)malloc(len + 256);
    if (!wbuf)
        rt_trap("string_ops: memory allocation failed");
    const char *words[128];
    int wc = split_words(src, len, wbuf, len + 256, words, 128);

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0)
            rt_sb_append_bytes(&sb, "-", 1);
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            rt_sb_append_bytes(&sb, &c, 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(wbuf);
    return result;
}

rt_string rt_str_screaming_snake(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = (char *)malloc(len + 256);
    if (!wbuf)
        rt_trap("string_ops: memory allocation failed");
    const char *words[128];
    int wc = split_words(src, len, wbuf, len + 256, words, 128);

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0)
            rt_sb_append_bytes(&sb, "_", 1);
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)toupper((unsigned char)word[j]);
            rt_sb_append_bytes(&sb, &c, 1);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(wbuf);
    return result;
}

//=============================================================================
// SQL LIKE Pattern Matching
//=============================================================================

/// @brief Internal SQL LIKE pattern matching (case-sensitive).
/// @param text Text string to match.
/// @param tlen Text length.
/// @param pat Pattern string (% = any chars, _ = one char, \ = escape).
/// @param plen Pattern length.
/// @return 1 if matched, 0 otherwise.
static int8_t like_match(
    const char *text, size_t tlen, const char *pat, size_t plen, int case_insensitive) {
    size_t ti = 0, pi = 0;
    size_t star_pi = (size_t)-1, star_ti = 0;

    while (ti < tlen) {
        if (pi < plen && pat[pi] == '%') {
            // Wildcard: remember this position for backtracking
            star_pi = pi;
            star_ti = ti;
            pi++;
            continue;
        }

        if (pi < plen && pat[pi] == '\\' && pi + 1 < plen) {
            // Escaped character — match literally
            pi++;
            char tc = text[ti];
            char pc = pat[pi];
            if (case_insensitive) {
                tc = (char)tolower((unsigned char)tc);
                pc = (char)tolower((unsigned char)pc);
            }
            if (tc == pc) {
                ti++;
                pi++;
                continue;
            }
        } else if (pi < plen && pat[pi] == '_') {
            // Single character wildcard
            ti++;
            pi++;
            continue;
        } else if (pi < plen) {
            char tc = text[ti];
            char pc = pat[pi];
            if (case_insensitive) {
                tc = (char)tolower((unsigned char)tc);
                pc = (char)tolower((unsigned char)pc);
            }
            if (tc == pc) {
                ti++;
                pi++;
                continue;
            }
        }

        // No match — backtrack to last %
        if (star_pi != (size_t)-1) {
            pi = star_pi + 1;
            star_ti++;
            ti = star_ti;
            continue;
        }

        return 0;
    }

    // Consume trailing % in pattern
    while (pi < plen && pat[pi] == '%')
        pi++;

    return pi == plen ? 1 : 0;
}

int8_t rt_str_like(rt_string text, rt_string pattern) {
    size_t tlen = rt_string_len_bytes(text);
    size_t plen = rt_string_len_bytes(pattern);
    const char *t = tlen ? text->data : "";
    const char *p = plen ? pattern->data : "";
    return like_match(t, tlen, p, plen, 0);
}

int8_t rt_str_like_ci(rt_string text, rt_string pattern) {
    size_t tlen = rt_string_len_bytes(text);
    size_t plen = rt_string_len_bytes(pattern);
    const char *t = tlen ? text->data : "";
    const char *p = plen ? pattern->data : "";
    return like_match(t, tlen, p, plen, 1);
}
