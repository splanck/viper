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

#include "rt_internal.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_string_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Extended String Utilities
//=============================================================================

/// @brief Return a copy of `str` with the first byte uppercased (rest unchanged). ASCII-only.
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

/// @brief Title-case: capitalize the first letter of every whitespace-separated word. ASCII-only.
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

/// @brief Strip `prefix` from the start of `str` if present. Returns a copy of `str` unchanged
/// if it doesn't start with `prefix`.
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

/// @brief Strip `suffix` from the end of `str` if present. Returns a copy of `str` unchanged
/// if it doesn't end with `suffix`.
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

/// @brief Find the *last* occurrence of `needle` within `haystack`. Returns the 1-based byte
/// position of the match, or 0 if not found / either operand is empty.
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

/// @brief Strip every occurrence of the first byte of `ch` from both ends of `str`. Useful for
/// quoting/dequoting. Only the first byte of `ch` is examined; pass a single character.
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

/// @brief URL-friendly slugification: lowercase ASCII alnums kept as-is, runs of other bytes
/// collapsed to a single '-'. Trailing '-' trimmed. Useful for filenames and URL paths.
rt_string rt_str_slug(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    if (len == SIZE_MAX) {
        rt_trap("String.Slug: input too large");
        return rt_string_from_bytes("", 0);
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        rt_trap("String.Slug: allocation failed");
        return NULL;
    }

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

/// @brief Compute the Levenshtein edit distance between two strings — the minimum number of
/// single-character insertions, deletions, or substitutions to transform `a` into `b`. O(|a|*|b|)
/// time and O(min(|a|,|b|)) space (uses the rolling-row optimization).
int64_t rt_str_levenshtein(rt_string a, rt_string b) {
    if (!a && !b)
        return 0;
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    if (alen > (size_t)INT64_MAX || blen > (size_t)INT64_MAX)
        return -1;
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

    if (blen > (SIZE_MAX / sizeof(size_t)) - 1)
        return -1;
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

/// @brief Jaro string similarity, [0, 1] (1 = identical, 0 = no common characters within the
/// matching window). Window size = max(|a|, |b|)/2 - 1. Counts matches and transpositions.
double rt_str_jaro(rt_string a, rt_string b) {
    if (!a && !b)
        return 1.0;
    size_t alen = a ? rt_string_len_bytes(a) : 0;
    size_t blen = b ? rt_string_len_bytes(b) : 0;
    if (alen > (size_t)INT64_MAX || blen > (size_t)INT64_MAX)
        return 0.0;
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

/// @brief Jaro-Winkler similarity: Jaro plus a bonus for matching prefixes (up to 4 chars).
/// Better suited than Jaro for short strings and proper-noun matching where shared prefixes
/// are highly informative.
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

/// @brief Hamming distance: number of byte positions at which `a` and `b` differ. Both strings
/// must have the same length; returns -1 if they don't.
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

/// @brief Test whether @p c is one of the conventional word-separator characters.
/// @details Recognises space, underscore, hyphen, and tab. Used by camelCase
///          and snake_case splitter heuristics that need a uniform definition
///          of "boundary".
/// @param c Byte to classify.
/// @return Non-zero if @p c is a separator.
static int is_separator(char c) {
    return c == ' ' || c == '_' || c == '-' || c == '\t';
}

/// @brief Split @p src into words, recognising both explicit separators and camelCase.
/// @details Copies each detected word into @p buf with NUL terminators packed
///          end-to-end, and writes a pointer to each word into @p words.
///          Word boundaries are explicit separators (@ref is_separator) or a
///          lowercase→uppercase transition (camelCase). Stops at
///          @p max_words even if @p src has more.
/// @param src       Input bytes.
/// @param len       Input length in bytes.
/// @param buf       Output buffer that the @p words pointers index into.
/// @param buf_cap   Capacity of @p buf in bytes.
/// @param words     Receives a pointer to each word (NUL-terminated within @p buf).
/// @param max_words Capacity of @p words; further words are dropped.
/// @return Number of words written (≤ @p max_words).
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

/// @brief Append bytes to a casing builder and trap on failure.
/// @details The case-conversion helpers build their result through
///          @ref rt_string_builder.  Ignoring append status can return a
///          silently truncated string after allocation failure or size
///          overflow.  This helper centralises the status check and emits a
///          contextual trap while leaving cleanup to the caller.
/// @param sb Builder receiving bytes.
/// @param bytes Source byte range; may be NULL only when @p len is zero.
/// @param len Number of bytes to append.
/// @param context Operation name used in the trap message.
/// @return 1 on success, 0 after a trapped append failure.
static int append_case_bytes(rt_string_builder *sb, const char *bytes, size_t len, const char *context) {
    rt_sb_status_t status = rt_sb_append_bytes(sb, bytes, len);
    if (status == RT_SB_OK)
        return 1;
    rt_trap(context ? context : "string case conversion: append failed");
    return 0;
}

/// @brief Append a single byte to a casing builder.
/// @param sb Builder receiving the byte.
/// @param ch Byte value to append.
/// @param context Operation name used in the trap message.
/// @return 1 on success, 0 after a trapped append failure.
static int append_case_char(rt_string_builder *sb, char ch, const char *context) {
    return append_case_bytes(sb, &ch, 1, context);
}

/// @brief Return the length of the next UTF-8 codepoint at @p data.
/// @details Used by SQL LIKE `_` wildcard handling so `_` consumes one
///          user-visible UTF-8 codepoint rather than one raw byte.  Invalid or
///          truncated sequences trap and return zero.
/// @param data Pointer to the current byte.
/// @param remaining Number of bytes available from @p data.
/// @return Number of bytes in the next codepoint, or zero on invalid input.
static size_t like_utf8_step(const char *data, size_t remaining) {
    if (!data || remaining == 0)
        return 0;
    unsigned char c = (unsigned char)data[0];
    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0 && remaining >= 2 && ((unsigned char)data[1] & 0xC0) == 0x80)
        return 2;
    if ((c & 0xF0) == 0xE0 && remaining >= 3 && ((unsigned char)data[1] & 0xC0) == 0x80 &&
        ((unsigned char)data[2] & 0xC0) == 0x80)
        return 3;
    if ((c & 0xF8) == 0xF0 && remaining >= 4 && ((unsigned char)data[1] & 0xC0) == 0x80 &&
        ((unsigned char)data[2] & 0xC0) == 0x80 && ((unsigned char)data[3] & 0xC0) == 0x80)
        return 4;
    rt_trap("String.Like: invalid UTF-8 sequence");
    return 0;
}

static int split_words_dynamic(const char *src,
                               size_t len,
                               char **buf_out,
                               const char ***words_out) {
    if (buf_out)
        *buf_out = NULL;
    if (words_out)
        *words_out = NULL;
    if (len > (size_t)INT_MAX) {
        rt_trap("string_ops: input too large");
        return 0;
    }

    size_t word_cap = len > 0 ? len : 1;
    if (len > SIZE_MAX - word_cap - 1) {
        rt_trap("string_ops: input too large");
        return 0;
    }
    size_t buf_cap = len + word_cap + 1;

    char *wbuf = (char *)malloc(buf_cap);
    if (!wbuf) {
        rt_trap("string_ops: memory allocation failed");
        return 0;
    }

    const char **words = (const char **)malloc(word_cap * sizeof(*words));
    if (!words) {
        free(wbuf);
        rt_trap("string_ops: memory allocation failed");
        return 0;
    }

    int wc = split_words(src, len, wbuf, buf_cap, words, (int)word_cap);
    *buf_out = wbuf;
    *words_out = words;
    return wc;
}

/// @brief Convert "hello world" / "hello-world" / "hello_world" to "helloWorld". First word
/// stays lowercase; subsequent words have their first letter capitalized. Word separators
/// (whitespace, '-', '_') are dropped.
rt_string rt_str_camel_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = NULL;
    const char **words = NULL;
    int wc = split_words_dynamic(src, len, &wbuf, &words);
    if (!wbuf || !words)
        return NULL;

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        const char *word = words[w];
        size_t wlen = strlen(word);
        if (wlen == 0)
            continue;

        char first = (w == 0) ? (char)tolower((unsigned char)word[0])
                              : (char)toupper((unsigned char)word[0]);
        if (!append_case_char(&sb, first, "String.CamelCase: append failed"))
            goto camel_fail;
        for (size_t j = 1; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            if (!append_case_char(&sb, c, "String.CamelCase: append failed"))
                goto camel_fail;
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return result;

camel_fail:
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return NULL;
}

/// @brief Convert to "HelloWorld" — like camelCase but the first letter is also capitalized.
rt_string rt_str_pascal_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = NULL;
    const char **words = NULL;
    int wc = split_words_dynamic(src, len, &wbuf, &words);
    if (!wbuf || !words)
        return NULL;

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        const char *word = words[w];
        size_t wlen = strlen(word);
        if (wlen == 0)
            continue;

        char first = (char)toupper((unsigned char)word[0]);
        if (!append_case_char(&sb, first, "String.PascalCase: append failed"))
            goto pascal_fail;
        for (size_t j = 1; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            if (!append_case_char(&sb, c, "String.PascalCase: append failed"))
                goto pascal_fail;
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return result;

pascal_fail:
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return NULL;
}

/// @brief Convert to "hello_world": insert '_' before each capital letter (after the first),
/// then lowercase. Word separators (whitespace, '-') become '_'.
rt_string rt_str_snake_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = NULL;
    const char **words = NULL;
    int wc = split_words_dynamic(src, len, &wbuf, &words);
    if (!wbuf || !words)
        return NULL;

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0 && !append_case_bytes(&sb, "_", 1, "String.SnakeCase: append failed"))
            goto snake_fail;
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            if (!append_case_char(&sb, c, "String.SnakeCase: append failed"))
                goto snake_fail;
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return result;

snake_fail:
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return NULL;
}

/// @brief Convert to "hello-world": like snake_case but with '-' separators.
rt_string rt_str_kebab_case(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = NULL;
    const char **words = NULL;
    int wc = split_words_dynamic(src, len, &wbuf, &words);
    if (!wbuf || !words)
        return NULL;

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0 && !append_case_bytes(&sb, "-", 1, "String.KebabCase: append failed"))
            goto kebab_fail;
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)tolower((unsigned char)word[j]);
            if (!append_case_char(&sb, c, "String.KebabCase: append failed"))
                goto kebab_fail;
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return result;

kebab_fail:
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return NULL;
}

/// @brief Convert to "HELLO_WORLD": uppercase snake_case (constant-style identifier).
rt_string rt_str_screaming_snake(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);
    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_string_from_bytes("", 0);
    const char *src = str->data;

    char *wbuf = NULL;
    const char **words = NULL;
    int wc = split_words_dynamic(src, len, &wbuf, &words);
    if (!wbuf || !words)
        return NULL;

    rt_string_builder sb;
    rt_sb_init(&sb);

    for (int w = 0; w < wc; ++w) {
        if (w > 0 && !append_case_bytes(&sb, "_", 1, "String.ScreamingSnake: append failed"))
            goto screaming_fail;
        const char *word = words[w];
        size_t wlen = strlen(word);
        for (size_t j = 0; j < wlen; ++j) {
            char c = (char)toupper((unsigned char)word[j]);
            if (!append_case_char(&sb, c, "String.ScreamingSnake: append failed"))
                goto screaming_fail;
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return result;

screaming_fail:
    rt_sb_free(&sb);
    free(words);
    free(wbuf);
    return NULL;
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
            // Single UTF-8 codepoint wildcard
            size_t step = like_utf8_step(text + ti, tlen - ti);
            if (step == 0)
                return 0;
            ti += step;
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
            size_t step = like_utf8_step(text + star_ti, tlen - star_ti);
            if (step == 0)
                return 0;
            star_ti += step;
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

/// @brief SQL LIKE-style pattern match (case-sensitive). `%` matches any byte sequence,
/// `_` matches a single byte. Returns 1 on match, 0 otherwise.
int8_t rt_str_like(rt_string text, rt_string pattern) {
    size_t tlen = rt_string_len_bytes(text);
    size_t plen = rt_string_len_bytes(pattern);
    const char *t = tlen ? text->data : "";
    const char *p = plen ? pattern->data : "";
    return like_match(t, tlen, p, plen, 0);
}

/// @brief Case-insensitive variant of `_like`. ASCII-only for case folding (a-z ↔ A-Z).
int8_t rt_str_like_ci(rt_string text, rt_string pattern) {
    size_t tlen = rt_string_len_bytes(text);
    size_t plen = rt_string_len_bytes(pattern);
    const char *t = tlen ? text->data : "";
    const char *p = plen ? pattern->data : "";
    return like_match(t, tlen, p, plen, 1);
}
