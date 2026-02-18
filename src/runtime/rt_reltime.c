//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_reltime.h"
#include "rt_internal.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static int64_t current_unix_seconds(void)
{
    return (int64_t)time(NULL);
}

static int64_t i64_abs(int64_t x)
{
    if (x == INT64_MIN)
        return INT64_MAX; // -INT64_MIN is UB; saturate instead
    return x < 0 ? -x : x;
}

// ---------------------------------------------------------------------------
// rt_reltime_format_from
// ---------------------------------------------------------------------------

rt_string rt_reltime_format_from(int64_t timestamp, int64_t reference)
{
    int64_t diff = timestamp - reference; // positive = future, negative = past
    int64_t abs_diff = i64_abs(diff);
    int in_future = diff > 0;

    const char *unit = NULL;
    int64_t value = 0;

    if (abs_diff < 10)
    {
        return rt_string_from_bytes("just now", 8);
    }
    else if (abs_diff < 60)
    {
        value = abs_diff;
        unit = value == 1 ? "second" : "seconds";
    }
    else if (abs_diff < 3600)
    {
        value = abs_diff / 60;
        unit = value == 1 ? "minute" : "minutes";
    }
    else if (abs_diff < 86400)
    {
        value = abs_diff / 3600;
        unit = value == 1 ? "hour" : "hours";
    }
    else if (abs_diff < 2592000) // ~30 days
    {
        value = abs_diff / 86400;
        unit = value == 1 ? "day" : "days";
    }
    else if (abs_diff < 31536000) // ~365 days
    {
        value = abs_diff / 2592000;
        unit = value == 1 ? "month" : "months";
    }
    else
    {
        value = abs_diff / 31536000;
        unit = value == 1 ? "year" : "years";
    }

    char buf[128];
    int len;
    if (in_future)
        len = snprintf(buf, sizeof(buf), "in %lld %s", (long long)value, unit);
    else
        len = snprintf(buf, sizeof(buf), "%lld %s ago", (long long)value, unit);

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}

// ---------------------------------------------------------------------------
// rt_reltime_format
// ---------------------------------------------------------------------------

rt_string rt_reltime_format(int64_t timestamp)
{
    return rt_reltime_format_from(timestamp, current_unix_seconds());
}

// ---------------------------------------------------------------------------
// rt_reltime_format_duration
// ---------------------------------------------------------------------------

rt_string rt_reltime_format_duration(int64_t duration_ms)
{
    int64_t abs_ms = i64_abs(duration_ms);
    int negative = duration_ms < 0;

    int64_t total_secs = abs_ms / 1000;
    int64_t days = total_secs / 86400;
    int64_t hours = (total_secs % 86400) / 3600;
    int64_t minutes = (total_secs % 3600) / 60;
    int64_t seconds = total_secs % 60;

    rt_string_builder sb;
    rt_sb_init(&sb);

    if (negative)
        rt_sb_append_cstr(&sb, "-");

    int has_prev = 0;
    if (days > 0)
    {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%lldd", (long long)days);
        rt_sb_append_bytes(&sb, buf, (size_t)len);
        has_prev = 1;
    }
    if (hours > 0)
    {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%s%lldh", has_prev ? " " : "", (long long)hours);
        rt_sb_append_bytes(&sb, buf, (size_t)len);
        has_prev = 1;
    }
    if (minutes > 0)
    {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%s%lldm", has_prev ? " " : "", (long long)minutes);
        rt_sb_append_bytes(&sb, buf, (size_t)len);
        has_prev = 1;
    }
    if (seconds > 0 || !has_prev)
    {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%s%llds", has_prev ? " " : "", (long long)seconds);
        rt_sb_append_bytes(&sb, buf, (size_t)len);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_reltime_format_short
// ---------------------------------------------------------------------------

rt_string rt_reltime_format_short(int64_t timestamp)
{
    int64_t now = current_unix_seconds();
    int64_t diff = timestamp - now;
    int64_t abs_diff = i64_abs(diff);

    char buf[32];
    int len;

    if (abs_diff < 10)
    {
        return rt_string_from_bytes("now", 3);
    }
    else if (abs_diff < 60)
    {
        len = snprintf(buf, sizeof(buf), "%llds", (long long)abs_diff);
    }
    else if (abs_diff < 3600)
    {
        len = snprintf(buf, sizeof(buf), "%lldm", (long long)(abs_diff / 60));
    }
    else if (abs_diff < 86400)
    {
        len = snprintf(buf, sizeof(buf), "%lldh", (long long)(abs_diff / 3600));
    }
    else if (abs_diff < 2592000)
    {
        len = snprintf(buf, sizeof(buf), "%lldd", (long long)(abs_diff / 86400));
    }
    else if (abs_diff < 31536000)
    {
        len = snprintf(buf, sizeof(buf), "%lldmo", (long long)(abs_diff / 2592000));
    }
    else
    {
        len = snprintf(buf, sizeof(buf), "%lldy", (long long)(abs_diff / 31536000));
    }

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}
