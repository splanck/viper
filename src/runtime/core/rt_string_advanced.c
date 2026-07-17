//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_advanced.c
// Purpose: Extended string operations for the Zanna runtime. Contains replace,
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
#include "rt_ascii.h"
#include "rt_string_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Extended String Functions (Zanna.String expansion)
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
    if (needle_len > hay_len)
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

    if (!result) {
        rt_trap("rt_str_replace: allocation failed");
        return NULL;
    }
    return result;
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
            if (count == INT64_MAX) {
                rt_trap("String.Count: result too large");
                return INT64_MAX;
            }
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
/// @param pad_str Padding byte (must be exactly one byte).
/// @return Newly allocated padded string.
rt_string rt_str_pad_left(rt_string str, int64_t width, rt_string pad_str) {
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= 0 || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);
    // Width is a BYTE width, so the padding must be exactly one byte:
    // repeating the first byte of a multibyte character would emit
    // malformed UTF-8 (VDOC-167).
    if (rt_string_len_bytes(pad_str) != 1) {
        rt_trap("String.PadLeft: padding must be a single byte");
        return NULL;
    }
    uint64_t requested_width = (uint64_t)width;
    if (requested_width <= (uint64_t)str_len)
        return rt_string_ref(str);
    if (requested_width > (uint64_t)(SIZE_MAX - 1)) {
        rt_trap("String.PadLeft: width too large");
        return NULL;
    }

    char pad_char = pad_str->data[0];
    size_t target = (size_t)requested_width;
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
/// @param pad_str Padding byte (must be exactly one byte).
/// @return Newly allocated padded string.
rt_string rt_str_pad_right(rt_string str, int64_t width, rt_string pad_str) {
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= 0 || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);
    if (rt_string_len_bytes(pad_str) != 1) {
        rt_trap("String.PadRight: padding must be a single byte");
        return NULL;
    }
    uint64_t requested_width = (uint64_t)width;
    if (requested_width <= (uint64_t)str_len)
        return rt_string_ref(str);
    if (requested_width > (uint64_t)(SIZE_MAX - 1)) {
        rt_trap("String.PadRight: width too large");
        return NULL;
    }

    char pad_char = pad_str->data[0];
    size_t target = (size_t)requested_width;
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
        void *result = rt_seq_with_capacity_owned(1);
        rt_seq_push(result, (void *)rt_empty_string());
        return result;
    }

    size_t str_len = rt_string_len_bytes(str);
    size_t delim_len = delim ? rt_string_len_bytes(delim) : 0;

    // Empty delimiter: return single element with original string
    if (delim_len == 0 || delim_len > str_len) {
        void *result = rt_seq_with_capacity_owned(1);
        rt_seq_push(result, (void *)str);
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
            if (count == (size_t)INT64_MAX) {
                rt_trap("String.Split: result too large");
                return NULL;
            }
            count++;
            p += delim_len;
        } else {
            p++;
        }
    }

    // Pre-allocate sequence with exact capacity
    void *result = rt_seq_with_capacity_owned((int64_t)count);

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
            rt_string_unref(chunk);
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
    rt_string_unref(final_str);

    return result;
}

/// @brief Split a string into logical lines, normalizing CRLF to LF.
/// @details Splits on "\n" and drops a single trailing "\r" from each segment,
///          so CRLF and LF inputs yield identical logical lines. The segment
///          count is identical to @ref rt_str_split with a "\n" delimiter, which
///          makes this a drop-in replacement for the common
///          "split on newline, then strip the carriage return" idiom.
/// @param str Source string (null yields one empty line).
/// @return Seq of line strings with no trailing carriage returns.
void *rt_str_lines(rt_string str) {
    if (!str) {
        // Mirror rt_str_split: a null source yields a single empty segment.
        void *result = rt_seq_with_capacity_owned(1);
        rt_seq_push(result, (void *)rt_empty_string());
        return result;
    }

    size_t str_len = rt_string_len_bytes(str);
    const char *data = str->data;

    // Pass 1: count newlines so the result has exactly newlines + 1 segments.
    size_t count = 1;
    for (size_t i = 0; i < str_len; i++) {
        if (data[i] == '\n') {
            if (count == (size_t)INT64_MAX) {
                rt_trap("String.Lines: result too large");
                return NULL;
            }
            count++;
        }
    }

    void *result = rt_seq_with_capacity_owned((int64_t)count);

    // Pass 2: emit each segment, dropping one trailing '\r' (CRLF -> LF).
    size_t start = 0;
    for (size_t i = 0; i <= str_len; i++) {
        if (i == str_len || data[i] == '\n') {
            size_t seg_len = i - start;
            if (seg_len > 0 && data[start + seg_len - 1] == '\r')
                seg_len--;
            rt_string seg = rt_string_from_bytes(data + start, seg_len);
            rt_seq_push(result, (void *)seg);
            rt_string_unref(seg);
            start = i + 1;
        }
    }

    return result;
}

//=============================================================================
// Zanna.Text.Char — ASCII character classification (identifier rules)
//=============================================================================

/// @brief True for an ASCII letter a-z or A-Z.
static int rt_char_is_ascii_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// @brief True for an ASCII digit 0-9.
static int rt_char_is_ascii_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

/// @brief First byte of @p s, or -1 when empty/NULL. (Identifier rules are ASCII, so a
///        multibyte UTF-8 leading byte (>= 0x80) is correctly treated as a non-identifier char.)
static int rt_char_first_byte(rt_string s) {
    if (!s)
        return -1;
    const char *d = rt_string_cstr(s);
    int64_t n = rt_str_len(s);
    if (!d || n <= 0)
        return -1;
    return (unsigned char)d[0];
}

/// @brief 1 if the first character of @p s may start an identifier (ASCII letter or '_').
int8_t rt_text_char_is_identifier_start(rt_string s) {
    int c = rt_char_first_byte(s);
    if (c < 0)
        return 0;
    return (rt_char_is_ascii_alpha((unsigned char)c) || c == '_') ? 1 : 0;
}

/// @brief 1 if the first character of @p s may continue an identifier (ASCII letter, digit, '_').
int8_t rt_text_char_is_identifier_part(rt_string s) {
    int c = rt_char_first_byte(s);
    if (c < 0)
        return 0;
    unsigned char ch = (unsigned char)c;
    return (rt_char_is_ascii_alpha(ch) || rt_char_is_ascii_digit(ch) || c == '_') ? 1 : 0;
}

/// @brief 1 if the first character of @p s is ASCII alphanumeric (letter or digit).
int8_t rt_text_char_is_alnum(rt_string s) {
    int c = rt_char_first_byte(s);
    if (c < 0)
        return 0;
    unsigned char ch = (unsigned char)c;
    return (rt_char_is_ascii_alpha(ch) || rt_char_is_ascii_digit(ch)) ? 1 : 0;
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

/// @brief Strictly measure the UTF-8 codepoint starting at @p data.
/// @details Shared validator for codepoint-aware String operations
///          (VDOC-166): rejects invalid lead bytes, missing/invalid
///          continuation bytes, overlong encodings, UTF-16 surrogates, and
///          code points above U+10FFFF. Non-trapping so each caller can
///          raise its own contextual trap.
/// @param data Pointer to the current byte (may be NULL only if remaining==0).
/// @param remaining Bytes available from @p data.
/// @return Sequence length in bytes (1-4), or 0 when the sequence is invalid.
size_t rt_utf8_strict_step(const char *data, size_t remaining) {
    if (!data || remaining == 0)
        return 0;
    unsigned char lead = (unsigned char)data[0];
    if (lead < 0x80)
        return 1;
    size_t extra;
    uint32_t cp;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
    } else {
        return 0; // 0x80..0xC1 (continuation/overlong lead) or 0xF5..0xFF
    }
    if (remaining - 1 < extra)
        return 0;
    for (size_t k = 1; k <= extra; k++) {
        unsigned char ch = (unsigned char)data[k];
        if ((ch & 0xC0u) != 0x80u)
            return 0;
        cp = (cp << 6) | (uint32_t)(ch & 0x3Fu);
    }
    if ((extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u) ||
        (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu)
        return 0;
    return extra + 1;
}

/// @brief Convert a 1-based codepoint offset to a byte offset in a UTF-8 string.
/// @param data Pointer to the string data.
/// @param byte_len Total byte length of the string.
/// @param char_pos 1-based codepoint position (1 = first character).
/// @return Byte offset corresponding to the start of the codepoint, or byte_len
///         if char_pos is past the end.
size_t utf8_char_to_byte_offset(const char *data, size_t byte_len, int64_t char_pos) {
    if (!data || char_pos <= 1)
        return 0;
    size_t byte_off = 0;
    int64_t cp = 1;
    while (byte_off < byte_len && cp < char_pos) {
        // Strict decoding (VDOC-166): overlong encodings, surrogates, and
        // out-of-range scalars are malformed, not one-byte "characters".
        size_t clen = rt_utf8_strict_step(data + byte_off, byte_len - byte_off);
        if (clen == 0) {
            rt_trap("String: invalid UTF-8 sequence");
            return byte_len;
        }
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
    rt_trap("String: invalid UTF-8 lead byte");
    return 0;
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
    if (len == SIZE_MAX) {
        rt_trap("String.Flip: string too large");
        return NULL;
    }

    const char *data = str->data;

    // First pass: count characters and find their start positions
    size_t char_count = 0;
    for (size_t i = 0; i < len;) {
        size_t clen = rt_utf8_strict_step(data + i, len - i);
        if (clen == 0) {
            rt_trap("String.Flip: invalid UTF-8 sequence");
            return NULL;
        }
        i += clen;
        char_count++;
    }

    // Allocate positions array (offsets of each character start)
    if (char_count > (SIZE_MAX / sizeof(size_t)) - 1) {
        rt_trap("String.Flip: string too large");
        return NULL;
    }
    size_t *positions = (size_t *)malloc((char_count + 1) * sizeof(size_t));
    if (!positions) {
        rt_trap("String.Flip: allocation failed");
        return NULL;
    }

    // Second pass: record character positions
    size_t idx = 0;
    for (size_t i = 0; i < len;) {
        positions[idx++] = i;
        size_t clen = rt_utf8_strict_step(data + i, len - i);
        if (clen == 0) {
            free(positions);
            rt_trap("String.Flip: invalid UTF-8 sequence");
            return NULL;
        }
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
        unsigned char ca = (unsigned char)rt_ascii_tolower((unsigned char)a->data[i]);
        unsigned char cb = (unsigned char)rt_ascii_tolower((unsigned char)b->data[i]);
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

/// @brief Validate that a byte span is well-formed UTF-8 (see header contract).
int rt_utf8_span_valid(const char *data, size_t len) {
    if (!data)
        return len == 0;
    size_t i = 0;
    while (i < len) {
        unsigned char lead = (unsigned char)data[i];
        if (lead < 0x80) {
            i++;
            continue;
        }
        size_t extra;
        uint32_t cp;
        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
            cp = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            cp = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            cp = lead & 0x07u;
        } else {
            return 0; // 0x80-0xC1 (bare continuation / overlong lead) or 0xF5+.
        }
        if (len - i - 1 < extra)
            return 0;
        for (size_t k = 1; k <= extra; k++) {
            unsigned char ch = (unsigned char)data[i + k];
            if ((ch & 0xC0u) != 0x80u)
                return 0;
            cp = (cp << 6) | (uint32_t)(ch & 0x3Fu);
        }
        if ((extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u))
            return 0;
        if (cp >= 0xD800u && cp <= 0xDFFFu)
            return 0;
        if (cp > 0x10FFFFu)
            return 0;
        i += extra + 1;
    }
    return 1;
}
