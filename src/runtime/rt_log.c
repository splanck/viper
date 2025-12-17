//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_log.c
// Purpose: Simple logging functions for Viper.Log namespace.
//
// Key invariants: All log functions are thread-safe for the global level.
//                 Messages are written atomically to stderr.
// Ownership/Lifetime: No state retained beyond global log level.
//
//===----------------------------------------------------------------------===//

#include "rt_log.h"
#include "rt_internal.h"

#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Global log level - default is INFO (1)
    static int64_t g_log_level = RT_LOG_INFO;

    /// @brief Get current time as HH:MM:SS string.
    static void get_time_str(char *buf, size_t size)
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        if (tm_info)
        {
            strftime(buf, size, "%H:%M:%S", tm_info);
        }
        else
        {
            buf[0] = '\0';
        }
    }

    /// @brief Internal logging function.
    static void log_message(int64_t level, const char *level_str, rt_string message)
    {
        if (level < g_log_level)
            return;

        const char *msg = "";
        if (message && message->data)
        {
            msg = rt_string_cstr(message);
            if (!msg)
                msg = "";
        }

        char time_buf[16];
        get_time_str(time_buf, sizeof(time_buf));

        fprintf(stderr, "[%s] %s %s\n", level_str, time_buf, msg);
        fflush(stderr);
    }

    void rt_log_debug(rt_string message)
    {
        log_message(RT_LOG_DEBUG, "DEBUG", message);
    }

    void rt_log_info(rt_string message)
    {
        log_message(RT_LOG_INFO, "INFO", message);
    }

    void rt_log_warn(rt_string message)
    {
        log_message(RT_LOG_WARN, "WARN", message);
    }

    void rt_log_error(rt_string message)
    {
        log_message(RT_LOG_ERROR, "ERROR", message);
    }

    int64_t rt_log_level(void)
    {
        return g_log_level;
    }

    void rt_log_set_level(int64_t level)
    {
        // Clamp to valid range
        if (level < RT_LOG_DEBUG)
            level = RT_LOG_DEBUG;
        if (level > RT_LOG_OFF)
            level = RT_LOG_OFF;
        g_log_level = level;
    }

    bool rt_log_enabled(int64_t level)
    {
        return level >= g_log_level && level < RT_LOG_OFF;
    }

    int64_t rt_log_level_debug(void)
    {
        return RT_LOG_DEBUG;
    }

    int64_t rt_log_level_info(void)
    {
        return RT_LOG_INFO;
    }

    int64_t rt_log_level_warn(void)
    {
        return RT_LOG_WARN;
    }

    int64_t rt_log_level_error(void)
    {
        return RT_LOG_ERROR;
    }

    int64_t rt_log_level_off(void)
    {
        return RT_LOG_OFF;
    }

#ifdef __cplusplus
}
#endif
