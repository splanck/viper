//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_advanced.c
// Purpose: Extended string operations for the Viper runtime. Contains replace,
//   starts_with/ends_with/has/count, padding, split/join, repeat, UTF-8 string
//   reversal, and lexicographic comparison functions.
//
// Key invariants:
//   - All functions returning rt_string allocate new strings (no mutation).
//   - Split returns a Seq of string pointers; join consumes a Seq.
//   - UTF-8 reversal (rt_str_flip) is codepoint-aware (handles 1-4 byte sequences).
//   - Comparison functions (cmp, cmp_nocase) return negative/zero/positive.
//
// Ownership/Lifetime:
//   - Returned strings are heap-allocated with reference counting.
//   - Caller must unref when done.
//
// Links: src/runtime/core/rt_string_internal.h (shared helpers),
//        src/runtime/core/rt_string_ops.c (core operations),
//        src/runtime/core/rt_string.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_string_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Extended String Functions (Viper.String expansion)
//===----------------------------------------------------------------------===//

/// @brief Replace all occurrences of needle with replacement.
/// @param haystack Source string.
/// @param needle String to find.
/// @param replacement String to substitute.
/// @return Newly allocated string with replacements.
rt_string rt_str_replace(rt_string haystack, rt_string needle, rt_string replacement) {
    if (!haystack)
        return rt_empty_string();
    if (!needle || !replacement)
        return rt_string_ref(haystack);

    size_t hay_len = rt_string_len_bytes(haystack);
    size_t needle_len = rt_string_len_bytes(needle);
    size_t repl_len = rt_string_len_bytes(replacement);

    // Empty needle: return original string
    if (needle_len == 0)
        return rt_string_ref(haystack);

    // Single-pass algorithm using string builder.
    // This eliminates the double-scan (count + build) that was O(2*n*m).
    // Instead we scan once, building the result as we go.
    rt_string_builder sb;
    rt_sb_init(&sb);

    const char *p = haystack->data;
    const char *end = p + hay_len;
    const char *prev = p;
    const char first = needle->data[0];
    int found_any = 0;

    // Use memchr for fast first-character scanning (SIMD-optimized)
    while (p <= end - needle_len) {
        // Fast scan for first character of needle
        const char *match = memchr(p, first, (size_t)(end - needle_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, needle->data, needle_len) == 0) {
            found_any = 1;
            // Append chunk before match
            size_t chunk = (size_t)(p - prev);
            if (chunk > 0) {
                if (rt_sb_append_bytes(&sb, prev, chunk) != RT_SB_OK) {
                    rt_sb_free(&sb);
                    rt_trap("rt_str_replace: allocation failed");
                    return NULL;
                }
            }
            // Append replacement
            if (repl_len > 0) {
                if (rt_sb_append_bytes(&sb, replacement->data, repl_len) != RT_SB_OK) {
                    rt_sb_free(&sb);
                    rt_trap("rt_str_replace: allocation failed");
                    return NULL;
                }
            }
            p += needle_len;
            prev = p;
        } else {
            p++;
        }
    }

    // No matches found - return original string (avoid allocation)
    if (!found_any) {
        rt_sb_free(&sb);
        return rt_string_ref(haystack);
    }

    // Append remainder after last match
    size_t remainder = (size_t)(end - prev);
    if (remainder > 0) {
        if (rt_sb_append_bytes(&sb, prev, remainder) != RT_SB_OK) {
            rt_sb_free(&sb);
            rt_trap("rt_str_replace: allocation failed");
            return NULL;
        }
    }

    // Create result string from builder
    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);

    return result ? result : rt_empty_string();
}

/// @brief Check if string starts with prefix.
/// @param str Source string.
/// @param prefix Prefix to check.
/// @return 1 if str starts with prefix, 0 otherwise.
int64_t rt_str_starts_with(rt_string str, rt_string prefix) {
    if (!str || !prefix)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t prefix_len = rt_string_len_bytes(prefix);

    if (prefix_len > str_len)
        return 0;
    if (prefix_len == 0)
        return 1;

    return memcmp(str->data, prefix->data, prefix_len) == 0;
}

/// @brief Check if string ends with suffix.
/// @param str Source string.
/// @param suffix Suffix to check.
/// @return 1 if str ends with suffix, 0 otherwise.
int64_t rt_str_ends_with(rt_string str, rt_string suffix) {
    if (!str || !suffix)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t suffix_len = rt_string_len_bytes(suffix);

    if (suffix_len > str_len)
        return 0;
    if (suffix_len == 0)
        return 1;

    return memcmp(str->data + str_len - suffix_len, suffix->data, suffix_len) == 0;
}

/// @brief Check if string contains needle.
/// @param str Source string.
/// @param needle Substring to find.
/// @return 1 if str contains needle, 0 otherwise.
int64_t rt_str_has(rt_string str, rt_string needle) {
    if (!str || !needle)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t needle_len = rt_string_len_bytes(needle);

    if (needle_len == 0)
        return 1;
    if (needle_len > str_len)
        return 0;

    // Simple substring search
    for (size_t i = 0; i + needle_len <= str_len; i++) {
        if (memcmp(str->data + i, needle->data, needle_len) == 0)
            return 1;
    }
    return 0;
}

/// @brief Count non-overlapping occurrences of needle in str.
/// @param str Source string.
/// @param needle Substring to count.
/// @return Number of non-overlapping occurrences.
int64_t rt_str_count(rt_string str, rt_string needle) {
    if (!str || !needle)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t needle_len = rt_string_len_bytes(needle);

    if (needle_len == 0)
        return 0;
    if (needle_len > str_len)
        return 0;

    int64_t count = 0;
    const char *p = str->data;
    const char *end = p + str_len;

    while (p <= end - needle_len) {
        if (memcmp(p, needle->data, needle_len) == 0) {
            count++;
            p += needle_len; // Non-overlapping
        } else {
            p++;
        }
    }

    return count;
}

/// @brief Pad string on the left to reach specified width.
/// @param str Source string.
/// @param width Target width.
/// @param pad_str Padding character (first char used).
/// @return Newly allocated padded string.
rt_string rt_str_pad_left(rt_string str, int64_t width, rt_string pad_str) {
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= (int64_t)str_len || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);

    char pad_char = pad_str->data[0];
    size_t target = (size_t)width;
    size_t pad_count = target - str_len;

    rt_string result = rt_string_alloc(target, target + 1);
    if (!result)
        return NULL;

    memset(result->data, pad_char, pad_count);
    memcpy(result->data + pad_count, str->data, str_len);
    result->data[target] = '\0';

    return result;
}

/// @brief Pad string on the right to reach specified width.
/// @param str Source string.
/// @param width Target width.
/// @param pad_str Padding character (first char used).
/// @return Newly allocated padded string.
rt_string rt_str_pad_right(rt_string str, int64_t width, rt_string pad_str) {
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= (int64_t)str_len || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);

    char pad_char = pad_str->data[0];
    size_t target = (size_t)width;
    size_t pad_count = target - str_len;

    rt_string result = rt_string_alloc(target, target + 1);
    if (!result)
        return NULL;

    memcpy(result->data, str->data, str_len);
    memset(result->data + str_len, pad_char, pad_count);
    result->data[target] = '\0';

    return result;
}

/// @brief Split string by delimiter into a sequence.
/// @param str Source string.
/// @param delim Delimiter string.
/// @return Seq containing string parts.
void *rt_str_split(rt_string str, rt_string delim) {
    if (!str) {
        // Push empty string for null input
        void *result = rt_seq_with_capacity(1);
        rt_seq_push(result, (void *)rt_empty_string());
        return result;
    }

    size_t str_len = rt_string_len_bytes(str);
    size_t delim_len = delim ? rt_string_len_bytes(delim) : 0;

    // Empty delimiter: return single element with original string
    if (delim_len == 0) {
        void *result = rt_seq_with_capacity(1);
        rt_seq_push(result, (void *)rt_string_ref(str));
        return result;
    }

    // Pass 1: Count delimiters to pre-allocate result sequence
    // Uses memchr for SIMD-optimized first-character scanning
    const char *p = str->data;
    const char *end = str->data + str_len;
    const char first = delim->data[0];
    size_t count = 1; // At least one segment

    while (p <= end - delim_len) {
        const char *match = memchr(p, first, (size_t)(end - delim_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, delim->data, delim_len) == 0) {
            count++;
            p += delim_len;
        } else {
            p++;
        }
    }

    // Pre-allocate sequence with exact capacity
    void *result = rt_seq_with_capacity((int64_t)count);

    // Pass 2: Build segments
    const char *start = str->data;
    p = str->data;

    while (p <= end - delim_len) {
        const char *match = memchr(p, first, (size_t)(end - delim_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, delim->data, delim_len) == 0) {
            size_t chunk_len = (size_t)(p - start);
            rt_string chunk = rt_string_from_bytes(start, chunk_len);
            rt_seq_push(result, (void *)chunk);
            p += delim_len;
            start = p;
        } else {
            p++;
        }
    }

    // Add final segment
    size_t final_len = (size_t)(end - start);
    rt_string final_str = rt_string_from_bytes(start, final_len);
    rt_seq_push(result, (void *)final_str);

    return result;
}

/// @brief Join sequence of strings with separator.
/// @param sep Separator string.
/// @param seq Sequence of strings to join.
/// @return Newly allocated joined string.
rt_string rt_str_join(rt_string sep, void *seq) {
    if (!seq)
        return rt_empty_string();

    int64_t len = rt_seq_len(seq);
    if (len == 0)
        return rt_empty_string();

    size_t sep_len = sep ? rt_string_len_bytes(sep) : 0;

    // Calculate total length
    size_t total = 0;
    for (int64_t i = 0; i < len; i++) {
        rt_string item = (rt_string)rt_seq_get(seq, i);
        size_t item_len = item ? rt_string_len_bytes(item) : 0;
        if (total > SIZE_MAX - item_len) {
            rt_trap("rt_str_join: length overflow");
            return NULL;
        }
        total += item_len;
        if (i < len - 1 && sep_len > 0) {
            if (total > SIZE_MAX - sep_len) {
                rt_trap("rt_str_join: length overflow");
                return NULL;
            }
            total += sep_len;
        }
    }

    rt_string result = rt_string_alloc(total, total + 1);
    if (!result)
        return NULL;

    char *dst = result->data;
    for (int64_t i = 0; i < len; i++) {
        rt_string item = (rt_string)rt_seq_get(seq, i);
        size_t item_len = item ? rt_string_len_bytes(item) : 0;
        if (item_len > 0) {
            memcpy(dst, item->data, item_len);
            dst += item_len;
        }

        if (i < len - 1 && sep_len > 0) {
            memcpy(dst, sep->data, sep_len);
            dst += sep_len;
        }
    }

    *dst = '\0';
    return result;
}

/// @brief Repeat string count times.
/// @param str Source string.
/// @param count Number of repetitions.
/// @return Newly allocated repeated string.
rt_string rt_str_repeat(rt_string str, int64_t count) {
    if (!str || count <= 0)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);
    if (str_len == 0)
        return rt_empty_string();

    // Check for overflow
    if ((size_t)count > SIZE_MAX / str_len) {
        rt_trap("rt_str_repeat: length overflow");
        return NULL;
    }

    size_t total = str_len * (size_t)count;
    rt_string result = rt_string_alloc(total, total + 1);
    if (!result)
        return NULL;

    char *dst = result->data;
    for (int64_t i = 0; i < count; i++) {
        memcpy(dst, str->data, str_len);
        dst += str_len;
    }

    *dst = '\0';
    return result;
}

/// @brief Convert a 1-based codepoint offset to a byte offset in a UTF-8 string.
/// @param data Pointer to the string data.
/// @param byte_len Total byte length of the string.
/// @param char_pos 1-based codepoint position (1 = first character).
/// @return Byte offset corresponding to the start of the codepoint, or byte_len
///         if char_pos is past the end.
size_t utf8_char_to_byte_offset(const char *data, size_t byte_len, int64_t char_pos) {
    size_t byte_off = 0;
    int64_t cp = 1;
    while (byte_off < byte_len && cp < char_pos) {
        unsigned char c = (unsigned char)data[byte_off];
        size_t clen = 1;
        if ((c & 0x80) == 0)
            clen = 1;
        else if ((c & 0xE0) == 0xC0)
            clen = 2;
        else if ((c & 0xF0) == 0xE0)
            clen = 3;
        else if ((c & 0xF8) == 0xF0)
            clen = 4;
        if (byte_off + clen > byte_len)
            clen = byte_len - byte_off;
        byte_off += clen;
        cp++;
    }
    return byte_off;
}

/// @brief Get UTF-8 character byte length from leading byte.
/// @param c First byte of UTF-8 sequence.
/// @return Number of bytes in the character (1-4), or 1 for invalid.
size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0)
        return 1; // ASCII: 0xxxxxxx
    if ((c & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    if ((c & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    if ((c & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1;     // Invalid, treat as single byte
}

/// @brief Reverse string characters (UTF-8 aware).
/// @details Reverses the sequence of Unicode codepoints, not bytes.
///          For ASCII-only strings, this is equivalent to byte reversal.
///          For UTF-8 strings with multi-byte characters, this preserves
///          character integrity (e.g., "Hello, 世界!" becomes "!界世 ,olleH").
/// @param str Source string.
/// @return Newly allocated reversed string.
rt_string rt_str_flip(rt_string str) {
    if (!str)
        return rt_empty_string();

    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_empty_string();

    const char *data = str->data;

    // First pass: count characters and find their start positions
    size_t char_count = 0;
    for (size_t i = 0; i < len;) {
        size_t clen = utf8_char_len((unsigned char)data[i]);
        if (i + clen > len)
            clen = len - i; // Handle truncated sequences
        i += clen;
        char_count++;
    }

    // Allocate positions array (offsets of each character start)
    size_t *positions = (size_t *)malloc((char_count + 1) * sizeof(size_t));
    if (!positions)
        return NULL;

    // Second pass: record character positions
    size_t idx = 0;
    for (size_t i = 0; i < len;) {
        positions[idx++] = i;
        size_t clen = utf8_char_len((unsigned char)data[i]);
        if (i + clen > len)
            clen = len - i;
        i += clen;
    }
    positions[char_count] = len; // End sentinel

    // Allocate result buffer
    rt_string result = rt_string_alloc(len, len + 1);
    if (!result) {
        free(positions);
        return NULL;
    }

    // Build reversed string by copying characters in reverse order
    size_t dest = 0;
    for (size_t i = char_count; i > 0; i--) {
        size_t start = positions[i - 1];
        size_t end = positions[i];
        size_t clen = end - start;
        memcpy(result->data + dest, data + start, clen);
        dest += clen;
    }
    result->data[len] = '\0';

    free(positions);
    return result;
}

/// @brief Compare two strings, returning -1, 0, or 1.
/// @param a First string.
/// @param b Second string.
/// @return -1 if a < b, 0 if a == b, 1 if a > b.
int64_t rt_str_cmp(rt_string a, rt_string b) {
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;

    int result = memcmp(a->data, b->data, minlen);
    if (result != 0)
        return (result > 0) - (result < 0);

    if (alen < blen)
        return -1;
    if (alen > blen)
        return 1;
    return 0;
}

/// @brief Case-insensitive string comparison, returning -1, 0, or 1.
/// @param a First string.
/// @param b Second string.
/// @return -1 if a < b, 0 if a == b, 1 if a > b (case-insensitive).
int64_t rt_str_cmp_nocase(rt_string a, rt_string b) {
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;

    for (size_t i = 0; i < minlen; i++) {
        unsigned char ca = (unsigned char)tolower((unsigned char)a->data[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b->data[i]);
        if (ca < cb)
            return -1;
        if (ca > cb)
            return 1;
    }

    if (alen < blen)
        return -1;
    if (alen > blen)
        return 1;
    return 0;
}
