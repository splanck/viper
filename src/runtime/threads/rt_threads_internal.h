//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threads_internal.h
// Purpose: Shared model and platform-neutral helpers for the thread runtime,
//   split into rt_threads_win.c (Win32), rt_threads_posix.c (pthread), and
//   rt_threads_common.c (platform-neutral SafeThread wrapper API). Holds the
//   thread-handle magics, the SafeThreadCtx record, the small handle/retain
//   helpers, and the cross-translation-unit bridge declarations.
//
// Key invariants:
//   - Per-handle "magic" words distinguish thread/safe-thread handles.
//   - Inline helpers here touch no public Thread.* (runtime.def) API; the
//     join/query wrappers that do live in rt_threads_common.c.
//   - is_regular_thread_handle is implemented per-platform; the join/query
//     wrappers and safe_thread_copy_inner_thread live in the common TU.
//
// Ownership/Lifetime:
//   - Thread / SafeThread handles are heap-allocated and GC-managed.
//
// Links: rt_threads_win.c, rt_threads_posix.c, rt_threads_common.c, rt_threads.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_threads.h"

#include "rt_context.h"
#include "rt_context_internal.h"
#include "rt_internal.h"
#include "zanna/runtime/rt.h"

#include "rt_object.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

#define RT_THREAD_MAGIC 0x56545244u      /* "VTRD" */
#define RT_SAFE_THREAD_MAGIC 0x56545346u /* "VTSF" */

typedef struct SafeThreadCtx {
    uint32_t magic;
    rt_thread_entry_fn entry;
    void *arg;
    int8_t owns_arg;
    void *thread;
    void *monitor;
    int8_t trapped;
    char error[512];
} SafeThreadCtx;

/// @brief Decode the legacy IL `obj` representation of a native callback.
/// @details The public IL ABI historically transports callback addresses in a
///          `void *`. ISO C does not define a cast from an object pointer to a
///          function pointer, so the compatibility boundary copies the object
///          representation after proving both pointer kinds have equal size.
///          All native runtime callers use the typed APIs and bypass this shim.
/// @param opaque Opaque callback representation supplied by the IL ABI.
/// @return Typed callback with the same representation, or NULL for NULL.
static inline rt_thread_entry_fn thread_entry_from_opaque(void *opaque) {
#if defined(__cplusplus)
    static_assert(sizeof(rt_thread_entry_fn) == sizeof(void *),
                  "thread callback and object pointers must have equal ABI size");
#else
    _Static_assert(sizeof(rt_thread_entry_fn) == sizeof(void *),
                   "thread callback and object pointers must have equal ABI size");
#endif
    rt_thread_entry_fn entry = NULL;
    memcpy(&entry, &opaque, sizeof(entry));
    return entry;
}

// Cross-TU bridges. is_regular_thread_handle is implemented per-platform; the
// remaining functions live in rt_threads_common.c (they wrap the public Thread
// API, so they cannot live in this internal header).
int is_regular_thread_handle(void *obj);
void *safe_thread_copy_inner_thread(SafeThreadCtx *ctx);
int8_t thread_try_join_inner_or_release(void *inner);
int8_t thread_join_for_inner_or_release(void *inner, int64_t ms);

/// @brief Read the 4-byte magic number stored at the head of a thread handle.
/// @details Thread objects start with a magic word that distinguishes them
///          from arbitrary heap memory. Combined with the runtime class id
///          this is a belt-and-suspenders check — class id alone could
///          collide if a stale handle is reinterpreted, while magic alone
///          could collide with random heap content. NULL handles return 0
///          so the magic comparison fails cleanly without a deref.
static inline uint32_t thread_handle_magic(void *obj) {
    if (!obj)
        return 0;
    return *(const uint32_t *)obj;
}

/// @brief Test whether @p obj is a live SafeThread handle (correct class id AND magic).
/// @details Used by the SafeThread API entry points to reject NULL,
///          stale, wrong-class, or freed-and-reused handles before
///          dereferencing. Returns 0 for any of those conditions.
static inline int is_safe_thread_handle(void *obj) {
    return rt_obj_is_instance(obj, RT_SAFE_THREAD_CLASS_ID, sizeof(SafeThreadCtx)) &&
           thread_handle_magic(obj) == RT_SAFE_THREAD_MAGIC;
}

/// @brief Release a retained Thread/SafeThread object and free it on last release.
static inline void thread_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static inline void thread_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    if (!buffer || buffer_size == 0)
        return;
    const char *err = rt_trap_get_error();
    if (!err || !*err)
        err = fallback ? fallback : "Thread: operation failed";
    snprintf(buffer, buffer_size, "%s", err);
}

/// @brief Try to retain a runtime-managed value for a spawned thread.
/// @details Installs a local recovery frame so retain overflow and stale-handle
///          traps cannot skip constructor rollback. On failure the diagnostic
///          is re-raised after clearing recovery, and a returning trap hook
///          receives a zero result so the caller can release initialized state.
///          Unmanaged non-null pointers retain the historical borrowed-argument
///          behavior because @ref rt_obj_retain_maybe ignores them.
/// @param arg Object, string, borrowed native pointer, or NULL.
/// @param fallback Diagnostic used when the recovered trap has no message.
/// @return Non-zero when construction may continue; zero after a trapped retain.
static inline int thread_try_retain_owned_value(void *arg, const char *fallback) {
    if (!arg)
        return 1;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        rt_trap(saved_error);
        return 0;
    }

    rt_obj_retain_maybe(arg);
    rt_trap_clear_recovery();
    return 1;
}

/// @brief Try to acquire the self-reference held until a thread exits.
/// @details Delegates to @ref thread_try_retain_owned_value and returns an
///          explicit status so platform constructors never continue with an
///          object that a returning trap path already released.
/// @param obj Newly initialized Thread or SafeThread object.
/// @param fallback Diagnostic used when the recovered trap has no message.
/// @return Non-zero when the self-reference was acquired; zero on failure.
static inline int thread_try_retain_self(void *obj, const char *fallback) {
    return thread_try_retain_owned_value(obj, fallback);
}
