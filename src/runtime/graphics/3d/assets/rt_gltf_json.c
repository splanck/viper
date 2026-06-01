//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf_json.c
// Purpose: Allocation-free in-place JSON scanner for the glTF importer. Walks a
//   (json, len) buffer with byte-offset cursors to locate object/array members
//   and extract scalars/strings, without constructing a DOM.
// Key invariants:
//   - Scanners are pure over their (json, len) argument buffer; no global state.
//   - Object/array ranges are [start, end) byte offsets into that buffer.
// Ownership/Lifetime:
//   - `*_alloc` / `*_get_string` helpers return malloc'd strings owned by the
//     caller; every other helper allocates nothing.
// Links: rt_gltf_json.h, rt_gltf.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_gltf_json.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/// @brief Advance @p pos past JSON whitespace (space/tab/CR/LF) in the raw byte scanner.
size_t gltf_json_skip_ws(const char *json, size_t len, size_t pos) {
    while (pos < len) {
        char c = json[pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        pos++;
    }
    return pos;
}

/// @brief Skip a quoted JSON string (honoring backslash escapes) starting at @p pos.
/// @return Index just past the closing quote, or SIZE_MAX if @p pos isn't a string or it's
/// unterminated.
size_t gltf_json_skip_string_raw(const char *json, size_t len, size_t pos) {
    if (!json || pos >= len || json[pos] != '"')
        return SIZE_MAX;
    pos++;
    while (pos < len) {
        char c = json[pos++];
        if (c == '"')
            return pos;
        if (c == '\\') {
            if (pos >= len)
                return SIZE_MAX;
            pos++;
        }
    }
    return SIZE_MAX;
}

/// @brief Read and unescape a quoted JSON string at @p pos into a malloc'd C string (caller frees).
/// @details Decodes the standard JSON escape set (\" \\ \/ \b \f \n \r \t); rejects any other
///          escape. Sets @p out_next past the closing quote on success.
/// @return The decoded string, or NULL on a malformed/unterminated string or allocation failure.
char *gltf_json_read_string_alloc(const char *json, size_t len, size_t pos, size_t *out_next) {
    char *out;
    size_t cap;
    size_t count = 0;
    if (out_next)
        *out_next = SIZE_MAX;
    if (!json || pos >= len || json[pos] != '"')
        return NULL;
    cap = len - pos + 1u;
    out = (char *)malloc(cap);
    if (!out)
        return NULL;
    pos++;
    while (pos < len) {
        char c = json[pos++];
        if (c == '"') {
            out[count] = '\0';
            if (out_next)
                *out_next = pos;
            return out;
        }
        if (c == '\\') {
            if (pos >= len)
                break;
            c = json[pos++];
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    free(out);
                    return NULL;
            }
        }
        out[count++] = c;
    }
    free(out);
    return NULL;
}

/// @brief Whether the JSON string at @p pos equals @p key, advancing @p out_next past it either
/// way.
int gltf_json_key_matches(
    const char *json, size_t len, size_t pos, const char *key, size_t *out_next) {
    char *decoded = gltf_json_read_string_alloc(json, len, pos, out_next);
    int matches = decoded && key && strcmp(decoded, key) == 0;
    free(decoded);
    return matches;
}

/// @brief Skip a complete JSON value (string, number, object, or array) starting at @p pos.
/// @details Strings are skipped whole; objects/arrays are skipped by tracking nesting depth so the
///          scanner lands on the value's first sibling/terminator. Primitives run to the next
///          delimiter. Returns SIZE_MAX on malformed/unbalanced input.
size_t gltf_json_skip_value(const char *json, size_t len, size_t pos) {
    int object_depth = 0;
    int array_depth = 0;
    pos = gltf_json_skip_ws(json, len, pos);
    if (pos >= len)
        return SIZE_MAX;
    if (json[pos] == '"')
        return gltf_json_skip_string_raw(json, len, pos);
    if (json[pos] != '{' && json[pos] != '[') {
        while (pos < len && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
            pos++;
        return pos;
    }
    do {
        char c = json[pos];
        if (c == '"') {
            pos = gltf_json_skip_string_raw(json, len, pos);
            if (pos == SIZE_MAX)
                return SIZE_MAX;
            continue;
        }
        if (c == '{')
            object_depth++;
        else if (c == '}')
            object_depth--;
        else if (c == '[')
            array_depth++;
        else if (c == ']')
            array_depth--;
        pos++;
    } while (pos < len && (object_depth > 0 || array_depth > 0));
    return object_depth == 0 && array_depth == 0 ? pos : SIZE_MAX;
}

/// @brief Find the index just past the bracket that closes the @p open_ch at @p pos.
/// @details Tracks nesting and skips quoted strings so brackets inside strings don't miscount.
/// @return Index after the matching @p close_ch, or SIZE_MAX if unbalanced.
size_t gltf_json_find_matching(
    const char *json, size_t len, size_t pos, char open_ch, char close_ch) {
    int depth = 0;
    if (!json || pos >= len || json[pos] != open_ch)
        return SIZE_MAX;
    while (pos < len) {
        char c = json[pos];
        if (c == '"') {
            pos = gltf_json_skip_string_raw(json, len, pos);
            if (pos == SIZE_MAX)
                return SIZE_MAX;
            continue;
        }
        if (c == open_ch)
            depth++;
        else if (c == close_ch) {
            depth--;
            if (depth == 0)
                return pos + 1u;
        }
        pos++;
    }
    return SIZE_MAX;
}

/// @brief Locate a top-level array property @p key in the root object, by raw byte scan.
/// @details Only matches keys at object depth 1 (true top level), then returns the byte range
///          of the '['..']' array value via @p out_start / @p out_end.
/// @return 1 if found (range set), 0 otherwise.
int gltf_json_find_top_level_array(
    const char *json, size_t len, const char *key, size_t *out_start, size_t *out_end) {
    int object_depth = 0;
    int array_depth = 0;
    size_t pos = 0;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    while (pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_top_key = 0;
            if (next == SIZE_MAX)
                return 0;
            if (object_depth == 1 && array_depth == 0)
                at_top_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_top_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t start;
                size_t end;
                if (colon < len && json[colon] == ':') {
                    start = gltf_json_skip_ws(json, len, colon + 1u);
                    if (start < len && json[start] == '[') {
                        end = gltf_json_find_matching(json, len, start, '[', ']');
                        if (end != SIZE_MAX) {
                            if (out_start)
                                *out_start = start;
                            if (out_end)
                                *out_end = end;
                            return 1;
                        }
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            object_depth++;
        else if (c == '}')
            object_depth--;
        else if (c == '[')
            array_depth++;
        else if (c == ']')
            array_depth--;
        pos++;
    }
    return 0;
}

/// @brief Read a direct string property @p key from the object spanning [obj_start, obj_end).
/// @details Only the object's own (depth-1) keys are considered; nested objects are skipped.
/// @return The unescaped value (caller frees), or NULL if absent or not a string.
char *gltf_json_object_get_string(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return NULL;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] == '"')
                        return gltf_json_read_string_alloc(json, len, value, NULL);
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return NULL;
            continue;
        }
        pos++;
    }
    return NULL;
}

/// @brief Read a direct non-negative integer property @p key as size_t (overflow-checked).
/// @return The parsed value, or @p fallback if absent, non-numeric, or it would overflow.
size_t gltf_json_object_get_size(const char *json,
                                 size_t len,
                                 size_t obj_start,
                                 size_t obj_end,
                                 const char *key,
                                 size_t fallback) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return fallback;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                size_t result = 0;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                        while (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                            size_t digit = (size_t)(json[value] - '0');
                            if (result > (SIZE_MAX - digit) / 10u)
                                return fallback;
                            result = result * 10u + digit;
                            value++;
                        }
                        return result;
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return fallback;
            continue;
        }
        pos++;
    }
    return fallback;
}

/// @brief Read a direct signed integer property @p key as int (overflow-checked, allows leading
/// '-').
/// @return The parsed value, or @p fallback if absent, non-numeric, or it would overflow.
int gltf_json_object_get_int(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key, int fallback) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return fallback;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                int sign = 1;
                int result = 0;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] == '-') {
                        sign = -1;
                        value++;
                    }
                    if (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                        while (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                            int digit = (int)(json[value] - '0');
                            if (result > (INT_MAX - digit) / 10)
                                return fallback;
                            result = result * 10 + digit;
                            value++;
                        }
                        return sign * result;
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return fallback;
            continue;
        }
        pos++;
    }
    return fallback;
}

/// @brief Find the byte range of a direct property @p key's value within an object.
/// @details Reports the [out_start, out_end) span of the raw value (any JSON type) for the
///          caller to parse further. Only depth-1 keys match.
/// @return 1 if found (range set), 0 otherwise.
int gltf_json_object_find_value(const char *json,
                                size_t len,
                                size_t obj_start,
                                size_t obj_end,
                                const char *key,
                                size_t *out_start,
                                size_t *out_end) {
    int depth = 0;
    size_t pos = obj_start;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return 0;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                if (colon < obj_end && json[colon] == ':') {
                    size_t value = gltf_json_skip_ws(json, len, colon + 1u);
                    size_t end = gltf_json_skip_value(json, len, value);
                    if (end != SIZE_MAX && end <= obj_end) {
                        if (out_start)
                            *out_start = value;
                        if (out_end)
                            *out_end = end;
                        return 1;
                    }
                }
                return 0;
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return 0;
            continue;
        }
        pos++;
    }
    return 0;
}

/// @brief Find the byte range of the @p item_index-th element of a JSON array.
/// @details Walks comma-separated values (skipping each whole) until it reaches the index.
/// @return 1 with [out_start, out_end) set, or 0 if the index is out of range or input malformed.
int gltf_json_array_item_range(const char *json,
                               size_t len,
                               size_t array_start,
                               size_t array_end,
                               int item_index,
                               size_t *out_start,
                               size_t *out_end) {
    size_t pos;
    int index = 0;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    if (!json || item_index < 0 || array_start >= array_end || array_end > len ||
        json[array_start] != '[')
        return 0;
    pos = array_start + 1u;
    while (pos < array_end) {
        size_t value_start;
        size_t value_end;
        pos = gltf_json_skip_ws(json, len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        value_start = pos;
        value_end = gltf_json_skip_value(json, len, value_start);
        if (value_end == SIZE_MAX || value_end > array_end)
            return 0;
        if (index == item_index) {
            if (out_start)
                *out_start = value_start;
            if (out_end)
                *out_end = value_end;
            return 1;
        }
        index++;
        pos = gltf_json_skip_ws(json, len, value_end);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    return 0;
}

/// @brief Read the @p item_index-th array element as a double via strtod (@p fallback otherwise).
/// @details Rejects string/object/array elements and non-finite results, returning @p fallback.
double gltf_json_array_get_number(const char *json,
                                  size_t len,
                                  size_t array_start,
                                  size_t array_end,
                                  int item_index,
                                  double fallback) {
    size_t value_start;
    size_t value_end;
    size_t text_len;
    char *text;
    char *endptr;
    double value;
    if (!gltf_json_array_item_range(
            json, len, array_start, array_end, item_index, &value_start, &value_end))
        return fallback;
    value_start = gltf_json_skip_ws(json, len, value_start);
    if (value_start >= value_end || json[value_start] == '"' || json[value_start] == '{' ||
        json[value_start] == '[')
        return fallback;
    text_len = value_end - value_start;
    if (text_len > SIZE_MAX - 1u)
        return fallback;
    text = (char *)malloc(text_len + 1u);
    if (!text)
        return fallback;
    memcpy(text, json + value_start, text_len);
    text[text_len] = '\0';
    endptr = NULL;
    errno = 0;
    value = strtod(text, &endptr);
    while (endptr && *endptr && isspace((unsigned char)*endptr))
        endptr++;
    if (endptr == text || (endptr && *endptr != '\0') || errno == ERANGE || !isfinite(value))
        value = fallback;
    free(text);
    return value;
}

/// @brief Read the @p item_index-th array element as an unescaped string (caller frees; NULL if not
/// a string).
char *gltf_json_array_get_string_alloc(
    const char *json, size_t len, size_t array_start, size_t array_end, int item_index) {
    size_t value_start;
    size_t value_end;
    if (!gltf_json_array_item_range(
            json, len, array_start, array_end, item_index, &value_start, &value_end))
        return NULL;
    value_start = gltf_json_skip_ws(json, len, value_start);
    if (value_start >= value_end || json[value_start] != '"')
        return NULL;
    return gltf_json_read_string_alloc(json, len, value_start, NULL);
}

/// @brief Read a property @p key as a boolean, accepting literal true/false or a nonzero number.
/// @return 1 for true/nonzero, 0 for false/zero, or @p fallback if the key is absent.
int gltf_json_object_get_boolish(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key, int fallback) {
    size_t value_start;
    size_t value_end;
    size_t value;
    if (!gltf_json_object_find_value(json, len, obj_start, obj_end, key, &value_start, &value_end))
        return fallback;
    value = gltf_json_skip_ws(json, len, value_start);
    if (value + 4u <= value_end && strncmp(json + value, "true", 4u) == 0)
        return 1;
    if (value + 5u <= value_end && strncmp(json + value, "false", 5u) == 0)
        return 0;
    return gltf_json_object_get_int(json, len, obj_start, obj_end, key, fallback) ? 1 : 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
