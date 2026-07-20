//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf_json.h
// Purpose: Minimal in-place JSON scanner used by the glTF importer. Operates
//   directly on a (json, len) buffer with byte-offset cursors — it never builds
//   a DOM, so the loader can walk the document with zero intermediate
//   allocations except for the explicit string-extracting helpers.
// Key invariants:
//   - All scanners take a (json, len) pair and 0-based byte offsets; callers
//     must keep len in sync with the buffer.
//   - Object/array ranges are [start, end) byte offsets into the same buffer.
//   - `*_alloc` / `*_get_string` helpers return malloc'd C strings the caller
//     owns and must free(); all other helpers allocate nothing.
// Ownership/Lifetime:
//   - No global state; every call is pure over its argument buffer.
// Links: rt_gltf_json.c, rt_gltf.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Skip whitespace starting at pos; return the first non-space offset.
size_t gltf_json_skip_ws(const char *json, size_t len, size_t pos);

/// @brief Skip a quoted string token (pos must be at the opening quote); return
///   the offset just past the closing quote.
size_t gltf_json_skip_string_raw(const char *json, size_t len, size_t pos);

/// @brief Read a quoted string (with escape handling) into a fresh malloc'd
///   buffer; *out_next receives the offset past the string. Returns NULL on error.
char *gltf_json_read_string_alloc(const char *json, size_t len, size_t pos, size_t *out_next);

/// @brief Test whether the quoted key at pos equals `key`; *out_next receives the
///   offset past the key (and following colon when matched).
int gltf_json_key_matches(
    const char *json, size_t len, size_t pos, const char *key, size_t *out_next);

/// @brief Skip a complete JSON value (object/array/string/number/literal) and
///   return the offset just past it.
size_t gltf_json_skip_value(const char *json, size_t len, size_t pos);

/**
 * @brief Validate one complete JSON document without allocating a DOM.
 * @param json Exact document bytes; embedded NUL/control bytes are invalid JSON.
 * @param len Exact byte length of @p json.
 * @return 1 when one value consumes the whole document after optional whitespace,
 * otherwise 0.
 */
int gltf_json_validate_document(const char *json, size_t len);

/**
 * @brief Validate the lexical form of glTF fields whose schema requires integral tokens.
 * @details Traverses core glTF objects plus supported extension objects without allocating a DOM.
 *          Numeric values used by indices, counts, enum fields, integer arrays, attribute maps,
 *          and boolean-like fields must be complete base-10 integers: fractions, exponents,
 *          leading zeroes, suffixes, sign-only forms, and signed-64 overflow are rejected. Unknown
 *          optional extension objects and application-defined `extras` remain opaque.
 * @param json Exact structurally valid JSON document bytes.
 * @param len Exact byte length of @p json.
 * @return 1 when every recognized integral token is exact, otherwise 0.
 */
int gltf_json_validate_gltf_integral_tokens(const char *json, size_t len);

/// @brief From an opening bracket/brace at pos, return the offset just past its
///   matching close character.
size_t gltf_json_find_matching(
    const char *json, size_t len, size_t pos, char open_ch, char close_ch);

/// @brief Locate a top-level array property `key`; fills [*out_start,*out_end)
///   with its element span. Returns 1 on success, 0 if absent.
int gltf_json_find_top_level_array(
    const char *json, size_t len, const char *key, size_t *out_start, size_t *out_end);

/// @brief Return a malloc'd copy of the string property `key` within the object
///   range, or NULL if absent/not a string.
char *gltf_json_object_get_string(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key);

/// @brief Return the integer-valued size property `key` within the object range,
///   or `fallback` if absent.
size_t gltf_json_object_get_size(const char *json,
                                 size_t len,
                                 size_t obj_start,
                                 size_t obj_end,
                                 const char *key,
                                 size_t fallback);

/// @brief Return the int property `key` within the object range, or `fallback`.
int gltf_json_object_get_int(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key, int fallback);

/// @brief Find property `key` within the object range; fills [*out_start,*out_end)
///   with its value span. Returns 1 on success, 0 if absent.
int gltf_json_object_find_value(const char *json,
                                size_t len,
                                size_t obj_start,
                                size_t obj_end,
                                const char *key,
                                size_t *out_start,
                                size_t *out_end);

/// @brief Fill [*out_start,*out_end) with the byte span of element `item_index`
///   within the array range. Returns 1 on success, 0 if out of range.
int gltf_json_array_item_range(const char *json,
                               size_t len,
                               size_t array_start,
                               size_t array_end,
                               int item_index,
                               size_t *out_start,
                               size_t *out_end);

/// @brief Return numeric element `item_index` within the array range, or `fallback`.
double gltf_json_array_get_number(const char *json,
                                  size_t len,
                                  size_t array_start,
                                  size_t array_end,
                                  int item_index,
                                  double fallback);

/// @brief Return a malloc'd copy of string element `item_index` within the array
///   range, or NULL if absent/not a string.
char *gltf_json_array_get_string_alloc(
    const char *json, size_t len, size_t array_start, size_t array_end, int item_index);

/// @brief Return the boolean property `key` within the object range as 0/1, or
///   `fallback` if absent.
int gltf_json_object_get_boolish(
    const char *json, size_t len, size_t obj_start, size_t obj_end, const char *key, int fallback);

#ifdef __cplusplus
}
#endif
