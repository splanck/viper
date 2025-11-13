//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares internal helpers for processing file paths and mode strings
// in the runtime's file I/O implementation. BASIC's OPEN statement uses numeric
// mode codes and string paths that must be validated and converted to platform
// file API parameters.
//
// The runtime must bridge BASIC's file I/O model to platform file APIs (fopen on
// POSIX, platform-specific APIs on Windows). This involves converting BASIC's
// mode enumeration (INPUT, OUTPUT, APPEND, RANDOM, BINARY) to C file mode strings
// or POSIX flags, extracting path strings from runtime objects, and validating
// all parameters before invoking system calls.
//
// Key Responsibilities:
// - Mode translation: rt_file_mode_string converts BASIC mode enumerations to
//   fopen-compatible mode strings ("r", "w", "a", etc.)
// - Flag conversion: rt_file_mode_to_flags maps mode strings to POSIX open() flags
//   for platforms requiring lower-level file control
// - Path extraction: rt_file_path_from_vstr extracts null-terminated UTF-8 paths
//   from runtime string objects, validating encoding and null-termination
// - Buffer management: Helpers provide views into string data suitable for
//   direct system call usage without additional copying
//
// These internal helpers are part of the runtime implementation and not exposed
// to IL programs. They provide a validated, platform-neutral interface that the
// file I/O implementation builds upon.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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
    /// @return True when the mode string is valid; false otherwise.
    bool rt_file_mode_to_flags(const char *mode, int32_t basic_mode, int *flags_out);

    /// @brief Extract a filesystem path pointer from a runtime string.
    /// @param path Runtime string containing the path; may be NULL.
    /// @param out_path Receives pointer to the UTF-8 data on success.
    /// @return True when a valid path pointer is produced.
    bool rt_file_path_from_vstr(const ViperString *path, const char **out_path);

    /// @brief Produce a byte view for a runtime string suitable for writing to a file.
    /// @param s Runtime string; may be NULL.
    /// @param data_out Receives pointer to the string bytes when non-null.
    /// @return Number of bytes referenced by the view.
    size_t rt_file_string_view(const ViperString *s, const uint8_t **data_out);

#ifdef __cplusplus
}
#endif
