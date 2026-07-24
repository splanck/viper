//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine_linux_helpers.h
// Purpose: Testable parsing helpers for Linux machine and cgroup controls.
//
// Key invariants:
//   - Unsigned controls reject signs, overflow, and trailing non-whitespace.
//   - CPU sets are ordered, non-overlapping, non-empty ranges.
//
// Ownership/Lifetime:
//   - Header-only parsing over caller-owned immutable strings.
//
// Links: src/runtime/system/rt_machine.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include <limits.h>
#include <stdint.h>

static inline int rt_machine_linux_parse_decimal_component(const char **cursor_inout,
                                                           unsigned long long *out) {
    const char *cursor = cursor_inout ? *cursor_inout : NULL;
    if (!cursor || !out || *cursor < '0' || *cursor > '9')
        return 0;
    unsigned long long value = 0;
    do {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (ULLONG_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
        cursor++;
    } while (*cursor >= '0' && *cursor <= '9');
    *cursor_inout = cursor;
    *out = value;
    return 1;
}

static inline int rt_machine_linux_parse_u64(const char *text, unsigned long long *out) {
    const char *cursor = text;
    unsigned long long value = 0;
    if (!rt_machine_linux_parse_decimal_component(&cursor, &value))
        return 0;
    while (*cursor == ' ' || *cursor == '\t')
        cursor++;
    if (*cursor != '\0')
        return 0;
    *out = value;
    return 1;
}

static inline int64_t rt_machine_linux_count_cpuset(const char *text) {
    if (!text || text[0] == '\0')
        return 0;
    unsigned long long count = 0;
    unsigned long long previous_last = 0;
    int have_previous = 0;
    const char *cursor = text;
    while (*cursor) {
        unsigned long long first = 0;
        if (!rt_machine_linux_parse_decimal_component(&cursor, &first))
            return 0;

        unsigned long long last = first;
        if (*cursor == '-') {
            cursor++;
            if (!rt_machine_linux_parse_decimal_component(&cursor, &last) || last < first)
                return 0;
        }
        if (have_previous && first <= previous_last)
            return 0;
        unsigned long long width = last - first;
        if (width == ULLONG_MAX)
            return 0;
        unsigned long long span = width + 1u;
        if (ULLONG_MAX - count < span)
            return 0;
        count += span;
        previous_last = last;
        have_previous = 1;
        if (*cursor == '\0')
            break;
        if (*cursor != ',')
            return 0;
        cursor++;
        if (*cursor == '\0')
            return 0;
    }
    return count > 0 && count <= (unsigned long long)INT64_MAX ? (int64_t)count : 0;
}

static inline int64_t rt_machine_linux_cpu_quota(unsigned long long quota,
                                                unsigned long long period) {
    if (quota == 0 || period == 0)
        return 0;
    unsigned long long rounded = quota / period + (quota % period != 0);
    return rounded <= (unsigned long long)INT64_MAX ? (int64_t)rounded : 0;
}
