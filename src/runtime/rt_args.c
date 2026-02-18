//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_args.c
/// @brief Command-line arguments and environment variable handling.
///
/// This file provides access to command-line arguments passed to the program
/// and environment variables from the operating system. It implements the
/// Viper.Environment class functionality.
///
/// **Command-Line Arguments:**
/// ```
/// Program invocation: myprogram.exe arg1 arg2 arg3
///
/// Index:  0           1     2     3
/// Value:  myprogram   arg1  arg2  arg3
///         ^           ^
///         |           +-- First real argument
///         +-------------- Program name (index 0)
/// ```
///
/// **Usage Examples:**
/// ```
/// ' Get argument count
/// Dim count = Environment.GetArgumentCount()
/// Print "Arguments: " & count
///
/// ' Get specific argument
/// If count > 1 Then
///     Dim firstArg = Environment.GetArgument(1)
///     Print "First argument: " & firstArg
/// End If
///
/// ' Get full command line
/// Dim cmdLine = Environment.GetCommandLine()
/// Print "Command: " & cmdLine
///
/// ' Work with environment variables
/// Dim home = Environment.GetVariable("HOME")
/// Print "Home directory: " & home
///
/// If Environment.HasVariable("DEBUG") Then
///     Print "Debug mode enabled"
/// End If
///
/// Environment.SetVariable("MY_VAR", "my value")
/// ```
///
/// **Environment Variables:**
/// | Function      | Description                              |
/// |---------------|------------------------------------------|
/// | GetVariable   | Get variable value (empty if not set)    |
/// | HasVariable   | Check if variable exists                 |
/// | SetVariable   | Set or create variable                   |
///
/// **Platform Notes:**
/// - Environment variable names are case-sensitive on Unix, case-insensitive on Windows
/// - Empty variable values are allowed
/// - Setting a variable affects only the current process
///
/// **Thread Safety:** Argument access is thread-safe. Environment variable
/// modification may not be thread-safe on all platforms.
///
/// @see rt_exec.c For executing external programs
///
//===----------------------------------------------------------------------===//

#include "rt_args.h"
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static RtArgsState *rt_args_state(void)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();
    return ctx ? &ctx->args_state : NULL;
}

static int rt_args_grow_if_needed(RtArgsState *state, size_t new_size)
{
    if (!state)
        return 0;
    if (new_size <= state->cap)
        return 1;
    size_t new_cap = state->cap ? state->cap * 2 : 8;
    while (new_cap < new_size)
    {
        if (new_cap > SIZE_MAX / 2)
        {
            rt_trap("rt_args: capacity overflow");
            return 0;
        }
        new_cap *= 2;
    }
    if (new_cap > SIZE_MAX / sizeof(rt_string))
    {
        rt_trap("rt_args: size overflow");
        return 0;
    }
    rt_string *next = (rt_string *)realloc(state->items, new_cap * sizeof(rt_string));
    if (!next)
    {
        rt_trap("rt_args: allocation failed");
        return 0;
    }
    state->items = next;
    state->cap = new_cap;
    return 1;
}

/// @brief Clear all stored command-line arguments.
///
/// Releases all stored argument strings and resets the argument count to zero.
/// This is typically called during context cleanup or when reinitializing
/// the argument state.
///
/// @note Internal use - typically not called directly from Viper code.
/// @note Releases references to all stored strings.
void rt_args_clear(void)
{
    RtArgsState *state = rt_args_state();
    if (!state)
        return;
    for (size_t i = 0; i < state->size; ++i)
    {
        if (state->items[i])
            rt_string_unref(state->items[i]);
        state->items[i] = NULL;
    }
    state->size = 0;
}

/// @brief Add a command-line argument to the argument store.
///
/// Appends an argument string to the argument list. Used during program
/// initialization to populate arguments from main(argc, argv).
///
/// @param s Argument string to add. NULL is stored as empty string.
///
/// @note Internal use - arguments are set up by the runtime before main runs.
/// @note The string is retained (reference count incremented).
/// @note Traps on allocation failure.
void rt_args_push(rt_string s)
{
    RtArgsState *state = rt_args_state();
    if (!state)
        return;
    if (!rt_args_grow_if_needed(state, state->size + 1))
        return;
    // Retain; store NULL as empty string for predictability
    if (!s)
        s = rt_str_empty();
    else
        rt_string_ref(s);
    state->items[state->size++] = s;
}

/// @brief Get the number of command-line arguments.
///
/// Returns the total count of arguments including the program name (index 0).
/// The count is always at least 1 when arguments have been initialized.
///
/// **Usage example:**
/// ```
/// Dim count = Environment.GetArgumentCount()
/// If count < 2 Then
///     Print "Usage: program <filename>"
///     Environment.Exit(1)
/// End If
/// ```
///
/// @return Number of arguments (including program name), or 0 if not initialized.
///
/// @note Index 0 is the program name; real arguments start at index 1.
/// @note O(1) time complexity.
///
/// @see rt_args_get For retrieving individual arguments
int64_t rt_args_count(void)
{
    RtArgsState *state = rt_args_state();
    return state ? (int64_t)state->size : 0;
}

/// @brief Get a command-line argument by index.
///
/// Retrieves the argument at the specified index. Index 0 is the program name,
/// and subsequent indices are the actual command-line arguments.
///
/// **Usage example:**
/// ```
/// ' Get program name
/// Dim progName = Environment.GetArgument(0)
///
/// ' Process arguments
/// For i = 1 To Environment.GetArgumentCount() - 1
///     Dim arg = Environment.GetArgument(i)
///     ProcessArgument(arg)
/// Next
/// ```
///
/// @param index Zero-based index of the argument to retrieve.
///
/// @return The argument string at the specified index.
///
/// @note Traps if index is out of range.
/// @note Returns a new reference (caller must manage memory).
///
/// @see rt_args_count For getting the argument count
rt_string rt_args_get(int64_t index)
{
    RtArgsState *state = rt_args_state();
    if (!state)
        return NULL;
    if (index < 0 || (size_t)index >= state->size)
    {
        rt_trap("rt_args_get: index out of range");
        return NULL;
    }
    rt_string s = state->items[index];
    // Return retained reference to match common getter semantics
    return rt_string_ref(s);
}

/// @brief Get the full command line as a single string.
///
/// Returns all command-line arguments concatenated with spaces. This is
/// useful for logging or displaying the exact invocation.
///
/// **Usage example:**
/// ```
/// ' Log how the program was invoked
/// Dim cmdLine = Environment.GetCommandLine()
/// Log("Started with: " & cmdLine)
/// ```
///
/// @return String containing all arguments separated by spaces.
///
/// @note Returns empty string if no arguments are available.
/// @note Arguments are joined with single spaces.
///
/// @see rt_args_get For individual argument access
/// @see rt_args_count For argument count
rt_string rt_cmdline(void)
{
    RtArgsState *state = rt_args_state();
    if (!state || state->size == 0)
        return rt_str_empty();
    rt_string_builder sb;
    rt_sb_init(&sb);
    for (size_t i = 0; i < state->size; ++i)
    {
        const char *cstr = rt_string_cstr(state->items[i]);
        if (i > 0)
            (void)rt_sb_append_cstr(&sb, " ");
        (void)rt_sb_append_cstr(&sb, cstr ? cstr : "");
    }
    rt_string out = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return out;
}

/// @brief Clean up argument state for a context.
///
/// Releases all argument strings and frees the argument array. Called
/// during context destruction.
///
/// @param ctx The context whose arguments should be cleaned up.
///
/// @note Internal use only.
void rt_args_state_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    RtArgsState *state = &ctx->args_state;
    if (state->items)
    {
        for (size_t i = 0; i < state->size; ++i)
        {
            if (state->items[i])
                rt_string_unref(state->items[i]);
            state->items[i] = NULL;
        }
        free(state->items);
    }
    state->items = NULL;
    state->size = 0;
    state->cap = 0;
}

/// @brief Check if running in native (AOT-compiled) mode.
///
/// Returns whether the program is running as native compiled code (as opposed
/// to being interpreted by the VM). This can be used for conditional behavior
/// based on execution mode.
///
/// **Usage example:**
/// ```
/// If Environment.IsNative() Then
///     Print "Running native code"
/// Else
///     Print "Running in VM"
/// End If
/// ```
///
/// @return 1 if running as native code, 0 if running in VM.
///
/// @note In native builds, this always returns 1.
/// @note The VM overrides this to return 0.
int64_t rt_env_is_native(void)
{
    // Native runtime library is only linked into AOT binaries, so this path
    // always reports "native". The VM overrides this via its runtime bridge.
    return 1;
}

/// @brief Validate environment variable name input.
/// @details Ensures @p name is non-null and non-empty before calling
///          platform environment APIs. Traps on invalid input so callers see a
///          deterministic failure instead of undefined behaviour.
/// @param name Runtime string naming the environment variable.
/// @param context Human-readable operation for diagnostics.
/// @return Null-terminated name suitable for getenv/setenv.
static const char *rt_env_require_name(rt_string name, const char *context)
{
    if (!name)
    {
        rt_trap(context ? context : "Environment: variable name is null");
    }

    const char *cname = rt_string_cstr(name);
    if (!cname || cname[0] == '\0')
    {
        rt_trap(context ? context : "Environment: variable name is empty");
    }
    return cname;
}

/// @brief Retrieve an environment variable's value.
/// @details Returns an empty runtime string when the variable is unset. The
///          variable name must be non-empty; traps on invalid input.
/// @param name Environment variable to look up.
/// @return Newly allocated runtime string containing the value or empty when missing.
rt_string rt_env_get_var(rt_string name)
{
    const char *cname =
        rt_env_require_name(name, "Viper.Environment.GetVariable: name must not be empty");

#ifdef _WIN32
    DWORD required = GetEnvironmentVariableA(cname, NULL, 0);
    if (required == 0)
    {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        {
            return rt_str_empty();
        }
        rt_trap("Viper.Environment.GetVariable: failed to query variable");
    }

    if (required <= 1)
    {
        return rt_str_empty();
    }

    char *buffer = (char *)malloc(required);
    if (!buffer)
    {
        rt_trap("Viper.Environment.GetVariable: allocation failed");
    }

    DWORD written = GetEnvironmentVariableA(cname, buffer, required);
    if (written == 0)
    {
        free(buffer);
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        {
            return rt_str_empty();
        }
        rt_trap("Viper.Environment.GetVariable: failed to read variable");
    }

    rt_string out = rt_string_from_bytes(buffer, written);
    free(buffer);
    return out;
#else
    const char *value = getenv(cname);
    if (!value)
    {
        return rt_str_empty();
    }
    return rt_string_from_bytes(value, strlen(value));
#endif
}

/// @brief Determine whether an environment variable exists.
/// @details Returns 1 when @p name is present (even if its value is empty) and
///          0 otherwise. Traps on invalid names.
/// @param name Environment variable to probe.
/// @return 1 if present, 0 if missing.
int64_t rt_env_has_var(rt_string name)
{
    const char *cname =
        rt_env_require_name(name, "Viper.Environment.HasVariable: name must not be empty");

#ifdef _WIN32
    DWORD required = GetEnvironmentVariableA(cname, NULL, 0);
    if (required == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    {
        return 0;
    }
    return 1;
#else
    const char *value = getenv(cname);
    return value ? 1 : 0;
#endif
}

/// @brief Set or overwrite an environment variable.
/// @details Accepts empty strings as values. Traps when the name is empty and
///          when the underlying platform call fails.
/// @param name Environment variable to set.
/// @param value New value (NULL treated as empty string).
void rt_env_set_var(rt_string name, rt_string value)
{
    const char *cname =
        rt_env_require_name(name, "Viper.Environment.SetVariable: name must not be empty");
    const char *cvalue = value ? rt_string_cstr(value) : "";

    // Reject embedded null bytes: setenv/SetEnvironmentVariableA terminate at the
    // first '\0', so a Viper String with internal nulls would be silently truncated.
    if (strlen(cname) != (size_t)rt_str_len(name))
        rt_trap("Viper.Environment.SetVariable: name must not contain null bytes");
    if (value && strlen(cvalue) != (size_t)rt_str_len(value))
        rt_trap("Viper.Environment.SetVariable: value must not contain null bytes");

#ifdef _WIN32
    if (!SetEnvironmentVariableA(cname, cvalue))
    {
        rt_trap("Viper.Environment.SetVariable: failed to set variable");
    }
#else
    if (setenv(cname, cvalue, 1) != 0)
    {
        rt_trap("Viper.Environment.SetVariable: failed to set variable");
    }
#endif
}

/// @brief Terminate the process with the provided exit code.
/// @details Delegates to exit so any registered atexit handlers run
///          before shutdown. The exit code is truncated to int for compatibility.
/// @param code Exit status to report.
void rt_env_exit(int64_t code)
{
    exit((int)code);
}
