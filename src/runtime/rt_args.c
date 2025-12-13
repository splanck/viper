//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a simple argument store for the runtime. The store retains pushed
// strings and releases them on clear. Getters return retained copies so callers
// follow the usual ownership rules used by other rt_* getters.
//
//===----------------------------------------------------------------------===//

#include "rt_args.h"
#include "rt_internal.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct rt_args_store
{
    rt_string *items;
    size_t size;
    size_t cap;
} rt_args_store;

static rt_args_store g_args = {NULL, 0, 0};

static void rt_args_grow_if_needed(size_t new_size)
{
    if (new_size <= g_args.cap)
        return;
    size_t new_cap = g_args.cap ? g_args.cap * 2 : 8;
    while (new_cap < new_size)
        new_cap *= 2;
    rt_string *next = (rt_string *)realloc(g_args.items, new_cap * sizeof(rt_string));
    if (!next)
    {
        fprintf(stderr, "rt_args: allocation failed\n");
        abort();
    }
    g_args.items = next;
    g_args.cap = new_cap;
}

void rt_args_clear(void)
{
    for (size_t i = 0; i < g_args.size; ++i)
    {
        if (g_args.items[i])
            rt_string_unref(g_args.items[i]);
        g_args.items[i] = NULL;
    }
    g_args.size = 0;
}

void rt_args_push(rt_string s)
{
    rt_args_grow_if_needed(g_args.size + 1);
    // Retain; store NULL as empty string for predictability
    if (!s)
        s = rt_str_empty();
    else
        rt_string_ref(s);
    g_args.items[g_args.size++] = s;
}

int64_t rt_args_count(void)
{
    return (int64_t)g_args.size;
}

rt_string rt_args_get(int64_t index)
{
    if (index < 0 || (size_t)index >= g_args.size)
    {
        fprintf(stderr, "rt_args_get: index out of range\n");
        abort();
    }
    rt_string s = g_args.items[index];
    // Return retained reference to match common getter semantics
    return rt_string_ref(s);
}

rt_string rt_cmdline(void)
{
    if (g_args.size == 0)
        return rt_str_empty();
    rt_string_builder sb;
    rt_sb_init(&sb);
    for (size_t i = 0; i < g_args.size; ++i)
    {
        const char *cstr = rt_string_cstr(g_args.items[i]);
        if (i > 0)
            (void)rt_sb_append_cstr(&sb, " ");
        (void)rt_sb_append_cstr(&sb, cstr ? cstr : "");
    }
    rt_string out = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return out;
}

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
    const char *cname = rt_env_require_name(name, "Viper.Environment.GetVariable: name must not be empty");

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
    const char *cname = rt_env_require_name(name, "Viper.Environment.HasVariable: name must not be empty");

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
    const char *cname = rt_env_require_name(name, "Viper.Environment.SetVariable: name must not be empty");
    const char *cvalue = value ? rt_string_cstr(value) : "";

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
