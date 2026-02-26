//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_args.h
// Purpose: Process-wide command-line argument store and environment variable access, providing
// push/query semantics for argument strings and get/set/has helpers for environment variables.
//
// Key invariants:
//   - Argument indices are zero-based and contiguous.
//   - rt_args_get traps on out-of-range indices; callers must check rt_args_count first.
//   - Environment variable names must be non-empty strings.
//   - rt_cmdline returns all arguments joined by spaces without quoting.
//
// Ownership/Lifetime:
//   - Pushed strings are retained by the store; rt_args_push retains a copy.
//   - rt_args_get returns a retained reference that the caller must release.
//   - rt_args_clear releases all stored references and resets the count to zero.
//
// Links: src/runtime/core/rt_args.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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

    /// @brief Report whether the program is running as native code (not in the VM).
    /// @return 1 when executing a native binary, 0 when running under the VM.
    int64_t rt_env_is_native(void);

    /// @brief Look up an environment variable by name.
    /// @details Returns an empty string when the variable is missing. The name
    ///          must be non-empty; the function traps otherwise.
    /// @param name Environment variable to read.
    /// @return Newly allocated runtime string with the value or empty when unset.
    rt_string rt_env_get_var(rt_string name);

    /// @brief Test whether an environment variable is present.
    /// @details Treats empty values as "present"; returns 0 when missing. The
    ///          variable name must be non-empty.
    /// @param name Environment variable to probe.
    /// @return 1 when present, 0 when missing.
    int64_t rt_env_has_var(rt_string name);

    /// @brief Set or overwrite an environment variable.
    /// @details Accepts empty values; overwrites existing entries. Variable
    ///          names must be non-empty. Cross-platform: uses setenv on POSIX
    ///          and SetEnvironmentVariable on Windows so empty values remain
    ///          present.
    /// @param name Variable name to set.
    /// @param value Desired value (may be empty). NULL treated as empty.
    void rt_env_set_var(rt_string name, rt_string value);

    /// @brief Terminate the current process with the provided exit code.
    /// @details Delegates to exit so any atexit handlers run before exit.
    /// @param code Exit status to report to the host OS.
    void rt_env_exit(int64_t code);

#ifdef __cplusplus
}
#endif
