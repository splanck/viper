//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_monitor_internal.h
// Purpose: Shared declarations and platform-neutral helpers for the monitor
//   (mutual-exclusion) runtime, whose implementation is split by platform into
//   rt_monitor_win.c (SRWLOCK + CONDITION_VARIABLE) and rt_monitor_posix.c
//   (pthread). Both translation units include this header for the common
//   includes, trap-recovery hooks, and the two small shared helpers.
//
// Key invariants:
//   - Helpers are static inline: one internal-linkage copy per TU, no exported
//     symbol, no source duplication.
//
// Ownership/Lifetime:
//   - monitor_release_enter_ref drops a GC ref taken while entering a monitor.
//
// Links: rt_monitor_win.c, rt_monitor_posix.c, rt_threads.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_threads.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

static inline void monitor_release_enter_ref(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static inline void monitor_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    if (!buffer || buffer_size == 0)
        return;
    const char *err = rt_trap_get_error();
    if (!err || !*err)
        err = fallback ? fallback : "Monitor.Enter: failed";
    snprintf(buffer, buffer_size, "%s", err);
}
