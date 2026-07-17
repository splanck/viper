//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_asset_error.c
// Purpose: Thread-local diagnostics for recoverable runtime asset load failures.
// Key invariants:
//   - The last error describes the most recent top-level failed content load.
//   - Warnings survive a successful top-level load so callers can inspect degradation.
// Ownership/Lifetime:
//   - All buffers are fixed-size thread-local storage owned by this module.
//   - Getter functions allocate rt_string values for script-facing access.
// Links: rt_asset_error.h, docs/zannalib/graphics/rendering3d.md
//
//===----------------------------------------------------------------------===//

#include "rt_asset_error.h"

#include "rt_platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RT_ASSET_ERROR_MESSAGE_CAP 512
#define RT_ASSET_WARNING_CAP 16
#define RT_ASSET_WARNING_MESSAGE_CAP 256
#define RT_ASSET_WARNING_JOINED_CAP                                                                \
    ((RT_ASSET_WARNING_CAP * (RT_ASSET_WARNING_MESSAGE_CAP + 1)) + 64)

static RT_THREAD_LOCAL rt_asset_error_code g_asset_error_code = RT_ASSET_ERROR_NONE;
static RT_THREAD_LOCAL char g_asset_error_message[RT_ASSET_ERROR_MESSAGE_CAP];
static RT_THREAD_LOCAL int g_asset_error_message_truncated = 0;
static RT_THREAD_LOCAL char g_asset_warnings[RT_ASSET_WARNING_CAP][RT_ASSET_WARNING_MESSAGE_CAP];
static RT_THREAD_LOCAL int g_asset_warning_truncated[RT_ASSET_WARNING_CAP];
static RT_THREAD_LOCAL int64_t g_asset_warning_count = 0;
static RT_THREAD_LOCAL int64_t g_asset_warning_suppressed = 0;
static RT_THREAD_LOCAL int32_t g_asset_load_depth = 0;
static RT_THREAD_LOCAL int64_t g_asset_import_stats[RT_ASSET_IMPORT_STAT_COUNT];

static int asset_error_vformat(char *dst, size_t dst_cap, const char *fmt, va_list ap)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 0)))
#endif
    ;

/// @brief Format an asset diagnostic and report whether storage truncated it.
/// @details Keeps every diagnostic buffer NUL-terminated even when `vsnprintf`
///          fails or the formatted output exceeds @p dst_cap.
/// @param dst Destination buffer.
/// @param dst_cap Destination byte capacity including NUL.
/// @param fmt printf-style format string; NULL is treated as an empty string.
/// @param ap Format argument list.
/// @return 1 when formatted text was truncated; otherwise 0.
static int asset_error_vformat(char *dst, size_t dst_cap, const char *fmt, va_list ap) {
    int written;
    if (!dst || dst_cap == 0)
        return 0;
    if (!fmt)
        fmt = "";
    written = vsnprintf(dst, dst_cap, fmt, ap);
    dst[dst_cap - 1] = '\0';
    return written < 0 || (size_t)written >= dst_cap;
}

void rt_asset_error_clear_error(void) {
    g_asset_error_code = RT_ASSET_ERROR_NONE;
    g_asset_error_message[0] = '\0';
    g_asset_error_message_truncated = 0;
}

void rt_asset_error_clear(void) {
    rt_asset_error_clear_error();
    g_asset_warning_count = 0;
    g_asset_warning_suppressed = 0;
    for (int64_t i = 0; i < RT_ASSET_WARNING_CAP; i++) {
        g_asset_warnings[i][0] = '\0';
        g_asset_warning_truncated[i] = 0;
    }
    for (int i = 0; i < RT_ASSET_IMPORT_STAT_COUNT; i++)
        g_asset_import_stats[i] = 0;
}

void rt_asset_error_add_import_stat(rt_asset_import_stat stat, int64_t amount) {
    if (stat < 0 || stat >= RT_ASSET_IMPORT_STAT_COUNT || amount <= 0)
        return;
    if (g_asset_import_stats[stat] > INT64_MAX - amount)
        g_asset_import_stats[stat] = INT64_MAX;
    else
        g_asset_import_stats[stat] += amount;
}

int64_t rt_asset_error_get_import_stat(rt_asset_import_stat stat) {
    if (stat < 0 || stat >= RT_ASSET_IMPORT_STAT_COUNT)
        return 0;
    return g_asset_import_stats[stat];
}

int rt_asset_error_begin_load(void) {
    if (g_asset_load_depth <= 0) {
        g_asset_load_depth = 0;
        rt_asset_error_clear();
    }
    g_asset_load_depth++;
    return g_asset_load_depth == 1;
}

void rt_asset_error_end_load_success(void) {
    if (g_asset_load_depth > 0)
        g_asset_load_depth--;
    if (g_asset_load_depth == 0)
        rt_asset_error_clear_error();
}

void rt_asset_error_end_load_failure(void) {
    if (g_asset_load_depth > 0)
        g_asset_load_depth--;
}

void rt_asset_error_set(rt_asset_error_code code, const char *message) {
    int written;
    g_asset_error_code = code;
    written = snprintf(
        g_asset_error_message, sizeof(g_asset_error_message), "%s", message ? message : "");
    g_asset_error_message[sizeof(g_asset_error_message) - 1] = '\0';
    g_asset_error_message_truncated =
        written < 0 || (size_t)written >= sizeof(g_asset_error_message);
}

void rt_asset_error_setf(rt_asset_error_code code, const char *fmt, ...) {
    va_list ap;
    g_asset_error_code = code;
    va_start(ap, fmt);
    g_asset_error_message_truncated =
        asset_error_vformat(g_asset_error_message, sizeof(g_asset_error_message), fmt, ap);
    va_end(ap);
}

void rt_asset_error_set_if_empty(rt_asset_error_code code, const char *message) {
    if (g_asset_error_code == RT_ASSET_ERROR_NONE)
        rt_asset_error_set(code, message);
}

void rt_asset_error_setf_if_empty(rt_asset_error_code code, const char *fmt, ...) {
    va_list ap;
    if (g_asset_error_code != RT_ASSET_ERROR_NONE)
        return;
    g_asset_error_code = code;
    va_start(ap, fmt);
    g_asset_error_message_truncated =
        asset_error_vformat(g_asset_error_message, sizeof(g_asset_error_message), fmt, ap);
    va_end(ap);
}

rt_asset_error_code rt_asset_error_get_code(void) {
    return g_asset_error_code;
}

const char *rt_asset_error_get_message(void) {
    return g_asset_error_message;
}

void rt_asset_error_add_warning(const char *message) {
    if (g_asset_warning_count < RT_ASSET_WARNING_CAP) {
        int written = snprintf(g_asset_warnings[g_asset_warning_count],
                               RT_ASSET_WARNING_MESSAGE_CAP,
                               "%s",
                               message ? message : "");
        g_asset_warnings[g_asset_warning_count][RT_ASSET_WARNING_MESSAGE_CAP - 1] = '\0';
        g_asset_warning_truncated[g_asset_warning_count] =
            written < 0 || (size_t)written >= RT_ASSET_WARNING_MESSAGE_CAP;
        g_asset_warning_count++;
        return;
    }
    g_asset_warning_suppressed++;
    int written = snprintf(g_asset_warnings[RT_ASSET_WARNING_CAP - 1],
                           RT_ASSET_WARNING_MESSAGE_CAP,
                           "%lld more suppressed",
                           (long long)g_asset_warning_suppressed);
    g_asset_warnings[RT_ASSET_WARNING_CAP - 1][RT_ASSET_WARNING_MESSAGE_CAP - 1] = '\0';
    g_asset_warning_truncated[RT_ASSET_WARNING_CAP - 1] =
        written < 0 || (size_t)written >= RT_ASSET_WARNING_MESSAGE_CAP;
}

void rt_asset_error_add_warningf(const char *fmt, ...) {
    char message[RT_ASSET_WARNING_MESSAGE_CAP];
    va_list ap;
    va_start(ap, fmt);
    (void)asset_error_vformat(message, sizeof(message), fmt, ap);
    va_end(ap);
    rt_asset_error_add_warning(message);
}

int64_t rt_asset_error_get_warning_count(void) {
    return g_asset_warning_count;
}

const char *rt_asset_error_get_warning(int64_t index) {
    if (index < 0 || index >= g_asset_warning_count)
        return "";
    return g_asset_warnings[index];
}

int rt_asset_error_get_message_was_truncated(void) {
    return g_asset_error_message_truncated ? 1 : 0;
}

int rt_asset_error_get_warning_was_truncated(int64_t index) {
    if (index < 0 || index >= g_asset_warning_count)
        return 0;
    return g_asset_warning_truncated[index] ? 1 : 0;
}

int64_t rt_asset_error_get_warning_suppressed_count(void) {
    return g_asset_warning_suppressed;
}

rt_string rt_assets3d_get_last_load_error(void) {
    const char *message = rt_asset_error_get_message();
    if (!message)
        message = "";
    return rt_string_from_bytes(message, strlen(message));
}

int64_t rt_assets3d_get_last_load_error_code(void) {
    return (int64_t)rt_asset_error_get_code();
}

int64_t rt_assets3d_get_load_warning_count(void) {
    return rt_asset_error_get_warning_count();
}

rt_string rt_assets3d_get_load_warning(int64_t index) {
    const char *warning = rt_asset_error_get_warning(index);
    return rt_string_from_bytes(warning, strlen(warning));
}

rt_string rt_assets3d_get_load_warnings(void) {
    char joined[RT_ASSET_WARNING_JOINED_CAP];
    size_t used = 0;
    int64_t count = rt_asset_error_get_warning_count();
    joined[0] = '\0';
    for (int64_t i = 0; i < count; i++) {
        const char *warning = rt_asset_error_get_warning(i);
        size_t len = warning ? strlen(warning) : 0;
        if (i > 0 && used + 1 < sizeof(joined))
            joined[used++] = '\n';
        if (len > sizeof(joined) - used - 1)
            len = sizeof(joined) - used - 1;
        if (len > 0) {
            memcpy(joined + used, warning, len);
            used += len;
        }
        joined[used] = '\0';
        if (used + 1 >= sizeof(joined))
            break;
    }
    return rt_string_from_bytes(joined, used);
}

/// @brief Append @p text to a bounded report buffer, tracking the used length.
static void asset_report_append(char *dst, size_t cap, size_t *used, const char *text) {
    size_t len = text ? strlen(text) : 0;
    if (*used >= cap - 1)
        return;
    if (len > cap - 1 - *used)
        len = cap - 1 - *used;
    memcpy(dst + *used, text, len);
    *used += len;
    dst[*used] = '\0';
}

/// @brief Append @p text as a JSON string literal (quoted, minimally escaped).
static void asset_report_append_json_string(char *dst, size_t cap, size_t *used, const char *text) {
    asset_report_append(dst, cap, used, "\"");
    for (const char *p = text ? text : ""; *p; p++) {
        char escaped[8];
        unsigned char ch = (unsigned char)*p;
        if (ch == '"' || ch == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)ch;
            escaped[2] = '\0';
        } else if (ch == '\n') {
            memcpy(escaped, "\\n", 3);
        } else if (ch == '\r') {
            memcpy(escaped, "\\r", 3);
        } else if (ch == '\t') {
            memcpy(escaped, "\\t", 3);
        } else if (ch < 0x20u) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)ch);
        } else {
            escaped[0] = (char)ch;
            escaped[1] = '\0';
        }
        asset_report_append(dst, cap, used, escaped);
    }
    asset_report_append(dst, cap, used, "\"");
}

/// @brief Append one `"key":value` counter field (with optional leading comma).
static void asset_report_append_counter(
    char *dst, size_t cap, size_t *used, const char *key, int64_t value, int leading_comma) {
    char field[96];
    snprintf(field,
             sizeof(field),
             "%s\"%s\":%lld",
             leading_comma ? "," : "",
             key,
             (long long)value);
    asset_report_append(dst, cap, used, field);
}

rt_string rt_assets3d_get_import_report(void) {
    char report[RT_ASSET_WARNING_JOINED_CAP + 1024];
    size_t used = 0;
    int64_t count = rt_asset_error_get_warning_count();
    report[0] = '\0';
    asset_report_append(report, sizeof(report), &used, "{");
    asset_report_append_counter(report,
                                sizeof(report),
                                &used,
                                "skippedPrimitives",
                                g_asset_import_stats[RT_ASSET_IMPORT_STAT_SKIPPED_PRIMITIVES],
                                0);
    asset_report_append_counter(
        report,
        sizeof(report),
        &used,
        "truncatedInfluenceVertices",
        g_asset_import_stats[RT_ASSET_IMPORT_STAT_TRUNCATED_INFLUENCE_VERTICES],
        1);
    asset_report_append_counter(
        report,
        sizeof(report),
        &used,
        "outOfRangeJointVertices",
        g_asset_import_stats[RT_ASSET_IMPORT_STAT_OUT_OF_RANGE_JOINT_VERTICES],
        1);
    asset_report_append_counter(report,
                                sizeof(report),
                                &used,
                                "ignoredExtensions",
                                g_asset_import_stats[RT_ASSET_IMPORT_STAT_IGNORED_EXTENSIONS],
                                1);
    asset_report_append_counter(
        report,
        sizeof(report),
        &used,
        "bakedCubicSplineChannels",
        g_asset_import_stats[RT_ASSET_IMPORT_STAT_BAKED_CUBIC_SPLINE_CHANNELS],
        1);
    asset_report_append_counter(
        report,
        sizeof(report),
        &used,
        "compressedAnimationKeysDropped",
        g_asset_import_stats[RT_ASSET_IMPORT_STAT_COMPRESSED_ANIMATION_KEYS_DROPPED],
        1);
    asset_report_append_counter(
        report, sizeof(report), &used, "suppressedWarnings", g_asset_warning_suppressed, 1);
    asset_report_append(report, sizeof(report), &used, ",\"warnings\":[");
    for (int64_t i = 0; i < count; i++) {
        if (i > 0)
            asset_report_append(report, sizeof(report), &used, ",");
        asset_report_append_json_string(
            report, sizeof(report), &used, rt_asset_error_get_warning(i));
    }
    asset_report_append(report, sizeof(report), &used, "]}");
    return rt_string_from_bytes(report, used);
}
