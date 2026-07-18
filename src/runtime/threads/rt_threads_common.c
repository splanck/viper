//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_threads_common.c
// Purpose: Platform-neutral thread helpers and the SafeThread API. Holds the
//   join/timed-join/id/alive wrappers (which delegate to the public Thread API)
//   and the SafeThread layer that adds trap-recovery around a worker. Compiled
//   on all platforms; the platform thread impls live in rt_threads_win.c /
//   rt_threads_posix.c.
//
// Key invariants:
//   - Public callback entry points use typed function pointers; compatibility
//     adapters never call through an object-pointer cast.
//   - SafeThread captures one worker trap and reports it deterministically to
//     the joining caller while still releasing the native thread handle.
// Ownership/Lifetime:
//   - Thread objects own their native inner handle until join, detach, or
//     finalization transfers/releases it. Callback arguments are borrowed.
//
// Links: rt_threads_internal.h, rt_threads_win.c, rt_threads_posix.c, rt_threads.h
//
//===----------------------------------------------------------------------===//

#include "rt_threads_internal.h"

static void thread_join_inner_or_release(void *inner) {
    if (!inner)
        return;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "Thread.Join: failed");
        rt_trap_clear_recovery();
        thread_release_object(inner);
        rt_trap(saved_error);
        return;
    }

    rt_thread_join(inner);
    rt_trap_clear_recovery();
    thread_release_object(inner);
}

int8_t thread_try_join_inner_or_release(void *inner) {
    if (!inner)
        return 1;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "Thread.TryJoin: failed");
        rt_trap_clear_recovery();
        thread_release_object(inner);
        rt_trap(saved_error);
        return 0;
    }

    int8_t joined = rt_thread_try_join(inner);
    rt_trap_clear_recovery();
    thread_release_object(inner);
    return joined;
}

int8_t thread_join_for_inner_or_release(void *inner, int64_t ms) {
    if (!inner)
        return 1;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "Thread.JoinFor: failed");
        rt_trap_clear_recovery();
        thread_release_object(inner);
        rt_trap(saved_error);
        return 0;
    }

    int8_t joined = rt_thread_join_for(inner, ms);
    rt_trap_clear_recovery();
    thread_release_object(inner);
    return joined;
}

static int64_t thread_get_id_inner_or_release(void *inner) {
    if (!inner)
        return 0;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "Thread.GetId: failed");
        rt_trap_clear_recovery();
        thread_release_object(inner);
        rt_trap(saved_error);
        return 0;
    }

    int64_t id = rt_thread_get_id(inner);
    rt_trap_clear_recovery();
    thread_release_object(inner);
    return id;
}

static int8_t thread_is_alive_inner_or_release(void *inner) {
    if (!inner)
        return 0;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "Thread.IsAlive: failed");
        rt_trap_clear_recovery();
        thread_release_object(inner);
        rt_trap(saved_error);
        return 0;
    }

    int8_t alive = rt_thread_get_is_alive(inner);
    rt_trap_clear_recovery();
    thread_release_object(inner);
    return alive;
}

//===----------------------------------------------------------------------===//
// Safe Thread Implementation (platform-independent)
//===----------------------------------------------------------------------===//

/// @brief Drop the owned-arg refcount for a SafeThread context.
static void safe_thread_release_owned_arg(SafeThreadCtx *ctx) {
    if (!ctx || !ctx->owns_arg || !ctx->arg)
        return;
    if (rt_obj_release_check0(ctx->arg))
        rt_obj_free(ctx->arg);
    ctx->arg = NULL;
    ctx->owns_arg = 0;
}

/// @brief GC finalizer for SafeThread context — releases all owned per-thread state.
static void safe_thread_finalize(void *obj) {
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    if (!ctx)
        return;
    ctx->magic = 0;
    safe_thread_release_owned_arg(ctx);
    if (ctx->thread) {
        if (rt_obj_release_check0(ctx->thread))
            rt_obj_free(ctx->thread);
        ctx->thread = NULL;
    }
    if (ctx->monitor) {
        if (rt_obj_release_check0(ctx->monitor))
            rt_obj_free(ctx->monitor);
        ctx->monitor = NULL;
    }
}

/// @brief Atomically take a retained snapshot of a SafeThread's inner Thread.
/// @details Reads ctx->thread under the SafeThread monitor and retains it
///          before releasing the lock, so the caller holds a stable reference
///          even if another thread concurrently swaps/clears the inner thread.
/// @return The retained inner thread (caller releases), or NULL.
void *safe_thread_copy_inner_thread(SafeThreadCtx *ctx) {
    if (!ctx)
        return NULL;
    void *inner = NULL;
    if (ctx->monitor)
        rt_monitor_enter(ctx->monitor);
    inner = ctx->thread;

    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        thread_save_trap_error(saved_error, sizeof(saved_error), "SafeThread: retain failed");
        rt_trap_clear_recovery();
        if (ctx->monitor)
            rt_monitor_exit(ctx->monitor);
        rt_trap(saved_error);
        return NULL;
    }

    rt_obj_retain_maybe(inner);
    rt_trap_clear_recovery();
    if (ctx->monitor)
        rt_monitor_exit(ctx->monitor);
    return inner;
}

/// @brief Entry point wrapper that sets up trap recovery.
static void safe_thread_entry(void *ctx_ptr) {
    SafeThreadCtx *ctx = (SafeThreadCtx *)ctx_ptr;
    if (!ctx)
        return;
    if (!ctx->entry) {
        rt_monitor_enter(ctx->monitor);
        ctx->trapped = 1;
        snprintf(ctx->error, sizeof(ctx->error), "%s", "Thread.StartSafe: null entry");
        rt_monitor_exit(ctx->monitor);
        goto done;
    }

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);

    if (setjmp(recovery) == 0) {
        ctx->entry(ctx->arg);
    } else {
        const char *err = rt_trap_get_error();
        rt_monitor_enter(ctx->monitor);
        ctx->trapped = 1;
        snprintf(ctx->error, sizeof(ctx->error), "%s", err ? err : "Unknown trap");
        rt_monitor_exit(ctx->monitor);
    }

    rt_trap_clear_recovery();

done:
    safe_thread_release_owned_arg(ctx);
    if (rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
}

/// @brief Start a thread with automatic trap/error capture (errors don't crash, they're stored).
static void *rt_thread_start_safe_impl(rt_thread_entry_fn entry, void *arg, int8_t retain_arg) {
    if (!entry)
        rt_trap("Thread.StartSafe: null entry");
    if (!entry)
        return NULL;

    SafeThreadCtx *ctx =
        (SafeThreadCtx *)rt_obj_new_i64(RT_SAFE_THREAD_CLASS_ID, (int64_t)sizeof(SafeThreadCtx));
    if (!ctx)
        rt_trap("Thread.StartSafe: failed to allocate context");
    if (!ctx)
        return NULL;

    memset(ctx, 0, sizeof(*ctx));
    rt_obj_set_finalizer(ctx, safe_thread_finalize);
    ctx->magic = RT_SAFE_THREAD_MAGIC;
    ctx->entry = entry;
    ctx->arg = arg;
    ctx->owns_arg = 0;
    ctx->thread = NULL;
    ctx->monitor = rt_obj_new_i64(/*class_id=*/0, 1);
    if (!ctx->monitor) {
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        rt_trap("Thread.StartSafe: failed to allocate state");
        return NULL;
    }
    ctx->trapped = 0;
    ctx->error[0] = '\0';
    if (retain_arg && arg) {
        if (!thread_try_retain_owned_value(arg, "Thread.StartSafeOwned: arg retain failed")) {
            thread_release_object(ctx);
            return NULL;
        }
        ctx->owns_arg = 1;
    }

    if (!thread_try_retain_self(ctx, "Thread.StartSafe: self retain failed")) {
        thread_release_object(ctx);
        return NULL;
    }

    char saved_error[256];
    jmp_buf start_recovery;
    rt_trap_set_recovery(&start_recovery);
    if (setjmp(start_recovery) != 0) {
        thread_save_trap_error(
            saved_error, sizeof(saved_error), "Thread.StartSafe: failed to start");
        rt_trap_clear_recovery();
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        rt_trap(saved_error);
        return NULL;
    }
    ctx->thread = rt_thread_start_fn(safe_thread_entry, ctx);
    rt_trap_clear_recovery();
    if (!ctx->thread) {
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        return NULL;
    }
    return ctx;
}

/// @brief Start a typed native callback with trap capture.
void *rt_thread_start_safe_fn(rt_thread_entry_fn entry, void *arg) {
    return rt_thread_start_safe_impl(entry, arg, 0);
}

/// @brief Start a typed native callback with trap capture and managed-argument ownership.
void *rt_thread_start_safe_owned_fn(rt_thread_entry_fn entry, void *arg) {
    return rt_thread_start_safe_impl(entry, arg, 1);
}

/// @brief Spawn a "safe" thread — same as `rt_thread_start` but with stronger trap recovery.
///
/// Safe threads route uncaught traps through a per-thread recovery
/// path that returns control to the spawning thread instead of
/// killing the process. Used by parallel-foreach and friends to
/// keep one bad worker from taking down the whole pool.
void *rt_thread_start_safe(void *entry, void *arg) {
    return rt_thread_start_safe_fn(thread_entry_from_opaque(entry), arg);
}

/// @brief Safe-thread variant that GC-retains `arg` (see `rt_thread_start_owned`).
void *rt_thread_start_safe_owned(void *entry, void *arg) {
    return rt_thread_start_safe_owned_fn(thread_entry_from_opaque(entry), arg);
}

/// @brief Check whether a safe thread trapped with an error.
int8_t rt_thread_has_error(void *obj) {
    if (!obj)
        return 0;
    if (is_regular_thread_handle(obj))
        return 0;
    if (!is_safe_thread_handle(obj)) {
        rt_trap("Thread.HasError: invalid thread handle");
        return 0;
    }
    rt_obj_retain_maybe(obj);
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    rt_monitor_enter(ctx->monitor);
    int8_t trapped = ctx->trapped;
    rt_monitor_exit(ctx->monitor);
    thread_release_object(obj);
    return trapped;
}

/// @brief Get the error message from a trapped safe thread (empty string if no error).
rt_string rt_thread_get_error(void *obj) {
    if (!obj)
        return rt_const_cstr("");
    if (is_regular_thread_handle(obj))
        return rt_const_cstr("");
    if (!is_safe_thread_handle(obj)) {
        rt_trap("Thread.Error: invalid thread handle");
        return rt_const_cstr("");
    }
    rt_obj_retain_maybe(obj);
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    char error[512];
    error[0] = '\0';
    rt_monitor_enter(ctx->monitor);
    int8_t trapped = ctx->trapped;
    int has_error = ctx->error[0] != '\0';
    if (trapped && has_error) {
        snprintf(error, sizeof(error), "%s", ctx->error);
    }
    rt_monitor_exit(ctx->monitor);
    thread_release_object(obj);
    if (!trapped || !has_error)
        return rt_const_cstr("");
    return rt_string_from_bytes(error, strlen(error));
}

/// @brief Join the underlying thread of a safe-started thread.
void rt_thread_safe_join(void *obj) {
    if (!obj)
        rt_trap("Thread.SafeJoin: null object");
    if (!obj)
        return;
    if (is_regular_thread_handle(obj)) {
        rt_thread_join(obj);
        return;
    }
    if (!is_safe_thread_handle(obj)) {
        rt_trap("Thread.SafeJoin: invalid thread handle");
        return;
    }
    rt_obj_retain_maybe(obj);
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    void *inner = safe_thread_copy_inner_thread(ctx);
    thread_release_object(obj);
    thread_join_inner_or_release(inner);
}

/// @brief Get the thread ID of a safe-started thread.
int64_t rt_thread_safe_get_id(void *obj) {
    if (!obj)
        return 0;
    if (is_regular_thread_handle(obj))
        return rt_thread_get_id(obj);
    if (!is_safe_thread_handle(obj)) {
        rt_trap("Thread.SafeGetId: invalid thread handle");
        return 0;
    }
    rt_obj_retain_maybe(obj);
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    void *inner = safe_thread_copy_inner_thread(ctx);
    thread_release_object(obj);
    return thread_get_id_inner_or_release(inner);
}

/// @brief Check if a safe-started thread is alive.
int8_t rt_thread_safe_is_alive(void *obj) {
    if (!obj)
        return 0;
    if (is_regular_thread_handle(obj))
        return rt_thread_get_is_alive(obj);
    if (!is_safe_thread_handle(obj)) {
        rt_trap("Thread.SafeIsAlive: invalid thread handle");
        return 0;
    }
    rt_obj_retain_maybe(obj);
    SafeThreadCtx *ctx = (SafeThreadCtx *)obj;
    void *inner = safe_thread_copy_inner_thread(ctx);
    thread_release_object(obj);
    return thread_is_alive_inner_or_release(inner);
}
