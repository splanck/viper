//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/io/rt_file_path.h
// Purpose: Internal helpers for translating BASIC OPEN mode enumerations to fopen mode strings and
// POSIX flags, extracting validated UTF-8 paths from runtime strings.
//
// Key invariants:
//   - rt_file_mode_string returns NULL for invalid mode enumerations.
//   - rt_file_mode_to_flags sets flags_out only on success; it is not modified on failure.
//   - Path extraction validates that the input is non-null and non-empty.
//   - All returned pointers are views into existing data; no allocation occurs.
//
// Ownership/Lifetime:
//   - All returned pointers are non-owning views; callers must not free them.
//   - ViperString inputs are borrowed, not retained.
//
// Links: src/runtime/io/rt_file_path.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <wchar.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Convert BASIC OPEN mode enumeration to the corresponding mode string.
/// @param mode BASIC OPEN mode value.
/// @return Pointer to constant mode string or NULL when the mode is invalid.
const char *rt_file_mode_string(int32_t mode);

/// @brief Parse an fopen-style @p mode string into POSIX open(2) flags.
/// @param mode Null-terminated mode string such as "r", "w", "a", or variants with modifiers.
/// @param basic_mode BASIC OPEN mode enumerator when known; pass a negative value when not
/// applicable.
/// @param flags_out Receives resolved flag bits on success.
/// @return 1 when the mode string is valid; 0 otherwise.
int8_t rt_file_mode_to_flags(const char *mode, int32_t basic_mode, int *flags_out);

/// @brief Extract a filesystem path pointer from a runtime string.
/// @param path Runtime string containing the path; may be NULL.
/// @param out_path Receives pointer to the UTF-8 data on success.
/// @return 1 when a valid path pointer is produced, 0 otherwise.
int8_t rt_file_path_from_vstr(const ViperString *path, const char **out_path);

/// @brief Produce a byte view for a runtime string suitable for writing to a file.
/// @param s Runtime string; may be NULL.
/// @param data_out Receives pointer to the string bytes when non-null.
/// @return Number of bytes referenced by the view.
size_t rt_file_string_view(const ViperString *s, const uint8_t **data_out);

#ifdef _WIN32
/// @brief Convert a UTF-8 path to a wide Windows path.
/// @param utf8 Null-terminated UTF-8 path.
/// @return Newly allocated wide string, or NULL on failure.
wchar_t *rt_file_path_utf8_to_wide(const char *utf8);

/// @brief Convert a wide Windows path to a runtime UTF-8 string.
/// @param wide Null-terminated wide path.
/// @return Runtime string, or empty string on failure.
rt_string rt_file_path_wide_to_string(const wchar_t *wide);
#endif

#ifdef __cplusplus
}
#endif
