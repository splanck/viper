//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_internal.h
// Purpose: Shared internal definitions for the string operations runtime.
//   Exposes helpers for string byte-length access and allocation so that the
//   core, advanced, and specialized modules can construct strings without
//   duplicating internal logic.
//
// Key invariants:
//   - This header is INTERNAL to the runtime — never include from public APIs.
//   - Only rt_string_ops.c, rt_string_advanced.c, and rt_string_specialized.c
//     should include this header.
//   - The public API header (rt_string.h) remains the sole interface for callers.
//
// Ownership/Lifetime:
//   - Strings returned by rt_string_alloc are heap-managed with reference counting.
//   - rt_empty_string() returns a cached singleton (never freed).
//
// Links: src/runtime/core/rt_string.h (public API),
//        src/runtime/core/rt_string_ops.c (core operations),
//        src/runtime/core/rt_string_advanced.c (extended operations),
//        src/runtime/core/rt_string_specialized.c (case conversion + distance)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

/// @brief Report the byte length of a runtime string payload.
/// @details Handles literal, embedded (SSO), and heap-backed strings.
///          Null handles yield zero.
size_t rt_string_len_bytes(rt_string s);

/// @brief Allocate a new mutable string with given length and capacity.
/// @param len Initial byte length of the string content.
/// @param cap Total capacity in bytes (must be >= len).
/// @return A freshly allocated string handle.
rt_string rt_string_alloc(size_t len, size_t cap);

/// @brief Return the cached empty string singleton.
/// @return An immutable empty string (never freed).
rt_string rt_empty_string(void);

/// @brief Register a live runtime string handle for safe ownership checks.
/// @param s Runtime string handle.
void rt_string_register_handle(rt_string s);

/// @brief Remove a string handle from the live-handle registry before free.
/// @param s Runtime string handle.
void rt_string_unregister_handle(rt_string s);

/// @brief Release registry storage used for live string-handle validation.
/// @details Called during process shutdown after runtime-owned strings have
///          been released.
void rt_string_registry_shutdown(void);

/// @brief Convert a character (codepoint) position to a byte offset in a UTF-8 string.
/// @param data The string bytes.
/// @param byte_len Total byte length.
/// @param char_pos Character position (1-based; values <= 1 map to byte offset 0).
/// @return Byte offset corresponding to char_pos, clamped to byte_len.
size_t utf8_char_to_byte_offset(const char *data, size_t byte_len, int64_t char_pos);

/// @brief Get the byte length of a UTF-8 character from its lead byte.
/// @param c Lead byte of the UTF-8 sequence.
/// @return Number of bytes implied by the lead byte (1-4), or 0 for an invalid lead byte.
size_t utf8_char_len(unsigned char c);

/// @brief Strict UTF-8 step: bytes in the sequence (1-4), or 0 when invalid
///        (bad lead/continuation, overlong, surrogate, > U+10FFFF).
size_t rt_utf8_strict_step(const char *data, size_t remaining);

/// @brief Validate that a byte span is well-formed UTF-8.
/// @details Rejects invalid lead bytes, truncated/invalid continuation bytes,
///          overlong encodings, surrogate code points, and values above
///          U+10FFFF. Shared by text-format validators (TOML/YAML) so their
///          acceptance matches the standards' UTF-8 stream requirement.
/// @param data Span start (may contain NUL bytes; they are ASCII-valid).
/// @param len Span length in bytes.
/// @return 1 when the whole span is valid UTF-8, 0 otherwise.
int rt_utf8_span_valid(const char *data, size_t len);
