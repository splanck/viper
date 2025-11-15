//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Runtime support for passing command-line arguments to programs. The helpers
// provide a simple process-wide argument store with clear/push semantics and
// query functions to retrieve the argument count and individual arguments.
// Strings are reference-counted runtime strings; the store retains pushed
// strings and returns retained copies from getters so callers own a reference.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "rt_string.h"

/// @brief Remove all stored arguments and release their references.
void rt_args_clear(void);

/// @brief Append an argument string to the store.
/// @details Retains @p s (no-op for NULL); callers retain ownership as well.
/// @param s Runtime string to append (may be NULL, treated as empty).
void rt_args_push(rt_string s);

/// @brief Return the number of stored arguments.
/// @return Argument count (non-negative).
int64_t rt_args_count(void);

/// @brief Retrieve argument by zero-based index.
/// @details Returns a retained reference to the stored string; caller must
///          release it. Traps when @p index is out of range.
/// @param index Zero-based index in [0, rt_args_count()).
/// @return Retained runtime string.
rt_string rt_args_get(int64_t index);

/// @brief Return a single string joining all arguments separated by spaces.
/// @details Returns a newly allocated string. No quoting is applied.
/// @return New string containing the command tail.
rt_string rt_cmdline(void);

#ifdef __cplusplus
}
#endif

