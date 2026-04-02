//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_async.c
// Purpose: Implements high-level async task combinators for the Viper.Threads.Async
//          class by composing Future/Promise, Thread, and Cancellation primitives.
//          Provides Run, All, Any, Delay, Map, and RunCancellable.
//
// Key invariants:
//   - Run/Delay/RunCancellable spawn background threads and resolve a Future.
//   - All/Any/Map register completion listeners instead of spinning or blocking
//     helper threads on other futures.
//   - Traps inside async callbacks are converted into Future errors.
//   - Promise/Future lifetimes are held explicitly while async work is in flight.
//
// Ownership/Lifetime:
//   - Callback arguments are forwarded as raw pointers; callers must keep raw
//     non-runtime pointers alive until the async callback consumes them.
//   - Async contexts own the promise reference they were created with.
//   - Listener states self-retain while callbacks are registered or executing.
//
// Links: src/runtime/threads/rt_async.h (public API),
//        src/runtime/threads/rt_future.h (Promise/Future primitives),
//        src/runtime/threads/rt_cancellation.h (cancellation token)
//
//===----------------------------------------------------------------------===//

#include "rt_async.h"

#include "rt_cancellation.h"
#include "rt_future.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threads.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

static void async_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void async_release_owned_arg(void **slot, int8_t *owned) {
    if (!slot || !owned || !*owned)
        return;
    async_release_ref(slot);
    *owned = 0;
}

static void async_promise_error_cstr(void *promise, const char *msg) {
    rt_promise_set_error(promise, rt_const_cstr(msg ? msg : "Unknown error"));
}

static void async_promise_error_copy(void *promise, const char *msg, const char *fallback) {
    const char *text = msg ? msg : fallback;
    if (!text) {
        async_promise_error_cstr(promise, "Unknown error");
        return;
    }
    rt_string copy = rt_string_from_bytes(text, strlen(text));
    rt_promise_set_error(promise, copy);
    rt_string_unref(copy);
}

static void async_promise_error_from_trap(void *promise, const char *fallback) {
    async_promise_error_copy(promise, rt_trap_get_error(), fallback);
}

static int8_t async_start_detached(void *entry, void *arg) {
    void *thread = rt_thread_start(entry, arg);
    if (!thread)
        return 0;
    if (rt_obj_release_check0(thread))
        rt_obj_free(thread);
    return 1;
}

//=============================================================================
// Async.Run / Async.Delay / Async.RunCancellable
//=============================================================================

typedef struct {
    void *(*callback)(void *);
    void *arg;
    int8_t owns_arg;
    void *promise;
} async_run_ctx;

typedef struct {
    int64_t ms;
    void *promise;
} async_delay_ctx;

typedef struct {
    void *(*callback)(void *, void *);
    void *arg;
    int8_t owns_arg;
    void *token;
    void *promise;
} async_cancel_ctx;

static void async_run_ctx_finalize(void *obj) {
    async_run_ctx *ctx = (async_run_ctx *)obj;
    if (!ctx)
        return;
    async_release_owned_arg(&ctx->arg, &ctx->owns_arg);
    async_release_ref(&ctx->promise);
}

static void async_delay_ctx_finalize(void *obj) {
    async_delay_ctx *ctx = (async_delay_ctx *)obj;
    if (!ctx)
        return;
    async_release_ref(&ctx->promise);
}

static void async_cancel_ctx_finalize(void *obj) {
    async_cancel_ctx *ctx = (async_cancel_ctx *)obj;
    if (!ctx)
        return;
    async_release_owned_arg(&ctx->arg, &ctx->owns_arg);
    async_release_ref(&ctx->token);
    async_release_ref(&ctx->promise);
}

static void async_run_entry(void *ctx_ptr) {
    async_run_ctx *ctx = (async_run_ctx *)ctx_ptr;
    if (!ctx || !ctx->callback) {
        if (ctx && ctx->promise)
            async_promise_error_cstr(ctx->promise, "Async.Run: nil callback");
        goto done;
    }

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        void *result = ctx->callback(ctx->arg);
        if (ctx->owns_arg && result == ctx->arg)
            rt_promise_set_owned(ctx->promise, result);
        else
            rt_promise_set(ctx->promise, result);
    } else {
        async_promise_error_from_trap(ctx->promise, "Async.Run: task trapped");
    }
    rt_trap_clear_recovery();

done:
    if (ctx && rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
}

static void async_delay_entry(void *ctx_ptr) {
    async_delay_ctx *ctx = (async_delay_ctx *)ctx_ptr;
    if (!ctx || !ctx->promise)
        goto done;

    if (ctx->ms > 0)
        rt_thread_sleep(ctx->ms);
    rt_promise_set(ctx->promise, NULL);

done:
    if (ctx && rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
}

static void async_cancel_entry(void *ctx_ptr) {
    async_cancel_ctx *ctx = (async_cancel_ctx *)ctx_ptr;
    if (!ctx || !ctx->callback) {
        if (ctx && ctx->promise)
            async_promise_error_cstr(ctx->promise, "Async.RunCancellable: nil callback");
        goto done;
    }

    if (ctx->token && rt_cancellation_is_cancelled(ctx->token)) {
        async_promise_error_cstr(ctx->promise, "cancelled");
        goto done;
    }

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        void *result = ctx->callback(ctx->arg, ctx->token);
        if (ctx->token && rt_cancellation_is_cancelled(ctx->token))
            async_promise_error_cstr(ctx->promise, "cancelled");
        else if (ctx->owns_arg && result == ctx->arg)
            rt_promise_set_owned(ctx->promise, result);
        else
            rt_promise_set(ctx->promise, result);
    } else {
        async_promise_error_from_trap(ctx->promise, "Async.RunCancellable: task trapped");
    }
    rt_trap_clear_recovery();

done:
    if (ctx && rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
}

/// @brief Run a callback asynchronously on the default thread pool, returning a Future.
static void *rt_async_run_impl(void *callback, void *arg, int8_t retain_arg) {
    if (!callback) {
        rt_trap("Async.Run: nil callback");
        return NULL;
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    async_run_ctx *ctx = (async_run_ctx *)rt_obj_new_i64(0, (int64_t)sizeof(async_run_ctx));
    if (!ctx) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    ctx->callback = (void *(*)(void *))callback;
    ctx->arg = arg;
    ctx->owns_arg = (retain_arg && arg) ? 1 : 0;
    if (ctx->owns_arg)
        rt_obj_retain_maybe(arg);
    ctx->promise = promise; // Takes ownership of the creator reference.
    rt_obj_set_finalizer(ctx, async_run_ctx_finalize);

    rt_obj_retain_maybe(ctx); // Worker self-reference.
    if (!async_start_detached((void *)async_run_entry, ctx)) {
        async_promise_error_cstr(promise, "Async.Run: failed to start thread");
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        return future;
    }

    if (rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
    return future;
}

void *rt_async_run(void *callback, void *arg) {
    return rt_async_run_impl(callback, arg, 0);
}

void *rt_async_run_owned(void *callback, void *arg) {
    return rt_async_run_impl(callback, arg, 1);
}

/// @brief Return a Future that completes after a delay of ms milliseconds.
void *rt_async_delay(int64_t ms) {
    if (ms < 0)
        ms = 0;

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    async_delay_ctx *ctx = (async_delay_ctx *)rt_obj_new_i64(0, (int64_t)sizeof(async_delay_ctx));
    if (!ctx) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    ctx->ms = ms;
    ctx->promise = promise; // Takes ownership of the creator reference.
    rt_obj_set_finalizer(ctx, async_delay_ctx_finalize);

    rt_obj_retain_maybe(ctx); // Worker self-reference.
    if (!async_start_detached((void *)async_delay_entry, ctx)) {
        async_promise_error_cstr(promise, "Async.Delay: failed to start thread");
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        return future;
    }

    if (rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
    return future;
}

/// @brief Run a callback asynchronously with cancellation support via a CancellationToken.
static void *rt_async_run_cancellable_impl(void *callback,
                                           void *arg,
                                           void *token,
                                           int8_t retain_arg) {
    if (!callback) {
        rt_trap("Async.RunCancellable: nil callback");
        return NULL;
    }

    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);
    async_cancel_ctx *ctx =
        (async_cancel_ctx *)rt_obj_new_i64(0, (int64_t)sizeof(async_cancel_ctx));
    if (!ctx) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    ctx->callback = (void *(*)(void *, void *))callback;
    ctx->arg = arg;
    ctx->owns_arg = (retain_arg && arg) ? 1 : 0;
    if (ctx->owns_arg)
        rt_obj_retain_maybe(arg);
    ctx->token = token;
    ctx->promise = promise; // Takes ownership of the creator reference.
    if (token)
        rt_obj_retain_maybe(token);
    rt_obj_set_finalizer(ctx, async_cancel_ctx_finalize);

    rt_obj_retain_maybe(ctx); // Worker self-reference.
    if (!async_start_detached((void *)async_cancel_entry, ctx)) {
        async_promise_error_cstr(promise, "Async.RunCancellable: failed to start thread");
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        if (rt_obj_release_check0(ctx))
            rt_obj_free(ctx);
        return future;
    }

    if (rt_obj_release_check0(ctx))
        rt_obj_free(ctx);
    return future;
}

void *rt_async_run_cancellable(void *callback, void *arg, void *token) {
    return rt_async_run_cancellable_impl(callback, arg, token, 0);
}

void *rt_async_run_cancellable_owned(void *callback, void *arg, void *token) {
    return rt_async_run_cancellable_impl(callback, arg, token, 1);
}

//=============================================================================
// Async.Map
//=============================================================================

typedef struct {
    void *(*mapper)(void *, void *);
    void *arg;
    int8_t owns_arg;
    void *promise;
} async_map_state;

static void async_map_state_finalize(void *obj) {
    async_map_state *state = (async_map_state *)obj;
    if (!state)
        return;
    async_release_owned_arg(&state->arg, &state->owns_arg);
    async_release_ref(&state->promise);
}

static void async_map_complete(void *future, void *ctx) {
    async_map_state *state = (async_map_state *)ctx;
    if (!state || !state->promise)
        goto done;

    if (rt_future_is_error(future)) {
        rt_promise_set_error(state->promise, rt_future_get_error(future));
        goto done;
    }

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        void *mapped = state->mapper(rt_future_peek_value(future), state->arg);
        if (state->owns_arg && mapped == state->arg)
            rt_promise_set_owned(state->promise, mapped);
        else
            rt_promise_set(state->promise, mapped);
    } else {
        async_promise_error_from_trap(state->promise, "Async.Map: mapper trapped");
    }
    rt_trap_clear_recovery();

done:
    if (state && rt_obj_release_check0(state))
        rt_obj_free(state);
}

/// @brief Transform a Future's result with a mapper function, returning a new Future.
static void *rt_async_map_impl(void *future, void *mapper, void *arg, int8_t retain_arg) {
    if (!future || !mapper) {
        rt_trap("Async.Map: nil future or mapper");
        return NULL;
    }

    void *promise = rt_promise_new();
    void *result_future = rt_promise_get_future(promise);
    async_map_state *state =
        (async_map_state *)rt_obj_new_i64(0, (int64_t)sizeof(async_map_state));
    if (!state) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return result_future;
    }

    state->mapper = (void *(*)(void *, void *))mapper;
    state->arg = arg;
    state->owns_arg = (retain_arg && arg) ? 1 : 0;
    if (state->owns_arg)
        rt_obj_retain_maybe(arg);
    state->promise = promise; // Takes ownership of the creator reference.
    rt_obj_set_finalizer(state, async_map_state_finalize);

    rt_obj_retain_maybe(state); // Completion callback reference.
    if (!rt_future_on_complete(future, async_map_complete, state)) {
        async_promise_error_cstr(promise, "Async.Map: failed to register completion");
        if (rt_obj_release_check0(state))
            rt_obj_free(state);
        if (rt_obj_release_check0(state))
            rt_obj_free(state);
        return result_future;
    }

    if (rt_obj_release_check0(state))
        rt_obj_free(state);
    return result_future;
}

void *rt_async_map(void *future, void *mapper, void *arg) {
    return rt_async_map_impl(future, mapper, arg, 0);
}

void *rt_async_map_owned(void *future, void *mapper, void *arg) {
    return rt_async_map_impl(future, mapper, arg, 1);
}

//=============================================================================
// Async.Any
//=============================================================================

typedef struct {
    void *promise;
    void *monitor;
    void **sources;
    int64_t count;
    int8_t completed;
} async_any_state;

static void async_any_complete(void *future, void *ctx);

static void async_any_state_finalize(void *obj) {
    async_any_state *state = (async_any_state *)obj;
    if (!state)
        return;
    free(state->sources);
    state->sources = NULL;
    async_release_ref(&state->monitor);
    async_release_ref(&state->promise);
}

static void async_any_listener_cancel(void *ctx) {
    async_any_state *state = (async_any_state *)ctx;
    if (state && rt_obj_release_check0(state))
        rt_obj_free(state);
}

static void async_any_cancel_remaining(async_any_state *state) {
    if (!state || !state->sources)
        return;
    for (int64_t i = 0; i < state->count; i++) {
        void *source = state->sources[i];
        if (!source)
            continue;
        if (rt_future_cancel_listener(source, async_any_complete, state))
            state->sources[i] = NULL;
    }
}

static void async_any_complete(void *future, void *ctx) {
    async_any_state *state = (async_any_state *)ctx;
    if (!state || !state->promise || !state->monitor)
        goto done;

    int8_t should_resolve = 0;
    rt_monitor_enter(state->monitor);
    if (!state->completed) {
        state->completed = 1;
        should_resolve = 1;
    }
    rt_monitor_exit(state->monitor);

    if (!should_resolve)
        goto done;

    if (rt_future_is_error(future))
        rt_promise_set_error(state->promise, rt_future_get_error(future));
    else if (rt_future_value_is_owned(future))
        rt_promise_set_owned(state->promise, rt_future_peek_value(future));
    else
        rt_promise_set(state->promise, rt_future_peek_value(future));
    async_any_cancel_remaining(state);

done:
    if (state && rt_obj_release_check0(state))
        rt_obj_free(state);
}

/// @brief Return a Future that completes when any of the input Futures completes.
void *rt_async_any(void *futures) {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int64_t count = futures ? rt_seq_len(futures) : 0;
    if (count == 0) {
        async_promise_error_cstr(promise, "Async.Any: empty futures");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    async_any_state *state =
        (async_any_state *)rt_obj_new_i64(0, (int64_t)sizeof(async_any_state));
    if (!state) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    state->promise = promise; // Takes ownership of the creator reference.
    state->monitor = rt_obj_new_i64(0, 1);
    state->sources = (void **)calloc((size_t)count, sizeof(void *));
    state->count = count;
    state->completed = 0;
    rt_obj_set_finalizer(state, async_any_state_finalize);
    if (!state->monitor || !state->sources) {
        async_promise_error_cstr(promise, "Async.Any: failed to allocate state");
        if (rt_obj_release_check0(state))
            rt_obj_free(state);
        return future;
    }

    int8_t registration_failed = 0;
    for (int64_t i = 0; i < count; i++) {
        void *source = rt_seq_get(futures, i);
        state->sources[i] = source;
        rt_obj_retain_maybe(state);
        if (!rt_future_on_complete_ex(
                source, async_any_complete, state, async_any_listener_cancel)) {
            if (rt_obj_release_check0(state))
                rt_obj_free(state);
            registration_failed = 1;
            break;
        }
    }

    if (registration_failed) {
        int8_t should_resolve = 0;
        rt_monitor_enter(state->monitor);
        if (!state->completed) {
            state->completed = 1;
            should_resolve = 1;
        }
        rt_monitor_exit(state->monitor);
        if (should_resolve)
            async_promise_error_cstr(promise, "Async.Any: failed to register completion");
        async_any_cancel_remaining(state);
    }

    if (rt_obj_release_check0(state))
        rt_obj_free(state);
    return future;
}

//=============================================================================
// Async.All
//=============================================================================

typedef struct {
    void *promise;
    void *monitor;
    void *results;
    void **sources;
    struct async_all_listener_ctx **listeners;
    uint8_t *owned_slots;
    int64_t slot_count;
    int64_t remaining;
    int8_t completed;
} async_all_state;

typedef struct async_all_listener_ctx {
    async_all_state *state;
    int64_t index;
} async_all_listener_ctx;

static void async_all_complete(void *future, void *ctx);

static void async_all_state_finalize(void *obj) {
    async_all_state *state = (async_all_state *)obj;
    if (!state)
        return;
    if (state->owned_slots && state->results) {
        for (int64_t i = 0; i < state->slot_count; i++) {
            if (!state->owned_slots[i])
                continue;
            void *value = rt_seq_get(state->results, i);
            if (value && rt_obj_release_check0(value))
                rt_obj_free(value);
        }
    }
    free(state->owned_slots);
    state->owned_slots = NULL;
    free(state->listeners);
    state->listeners = NULL;
    free(state->sources);
    state->sources = NULL;
    async_release_ref(&state->results);
    async_release_ref(&state->monitor);
    async_release_ref(&state->promise);
}

static void async_all_listener_cancel(void *ctx) {
    async_all_listener_ctx *listener = (async_all_listener_ctx *)ctx;
    async_all_state *state = listener ? listener->state : NULL;
    if (state && rt_obj_release_check0(state))
        rt_obj_free(state);
    free(listener);
}

static void async_all_cancel_remaining(async_all_state *state, int64_t skip_index) {
    if (!state || !state->sources || !state->listeners)
        return;
    for (int64_t i = 0; i < state->slot_count; i++) {
        if (i == skip_index || !state->listeners[i] || !state->sources[i])
            continue;
        if (rt_future_cancel_listener(state->sources[i], async_all_complete, state->listeners[i]))
            state->listeners[i] = NULL;
    }
}

static void async_all_complete(void *future, void *ctx) {
    async_all_listener_ctx *listener = (async_all_listener_ctx *)ctx;
    async_all_state *state = listener ? listener->state : NULL;
    if (!state || !state->promise || !state->monitor || !state->results)
        goto done;

    if (rt_future_is_error(future)) {
        int8_t should_resolve = 0;
        rt_monitor_enter(state->monitor);
        state->listeners[listener->index] = NULL;
        if (!state->completed) {
            state->completed = 1;
            should_resolve = 1;
        }
        rt_monitor_exit(state->monitor);
        if (should_resolve) {
            rt_promise_set_error(state->promise, rt_future_get_error(future));
            async_all_cancel_remaining(state, listener->index);
        }
        goto done;
    }

    int8_t resolve_success = 0;
    rt_monitor_enter(state->monitor);
    state->listeners[listener->index] = NULL;
    if (!state->completed) {
        void *value = rt_future_peek_value(future);
        int8_t owned = rt_future_value_is_owned(future);
        if (owned && value)
            rt_obj_retain_maybe(value);
        state->owned_slots[listener->index] = owned;
        rt_seq_set(state->results, listener->index, value);
        state->remaining--;
        if (state->remaining == 0) {
            state->completed = 1;
            resolve_success = 1;
        }
    }
    rt_monitor_exit(state->monitor);

    if (resolve_success)
        rt_promise_set_owned(state->promise, state->results);

done:
    if (state && rt_obj_release_check0(state))
        rt_obj_free(state);
    free(listener);
}

/// @brief Return a Future that completes when all input Futures have completed.
void *rt_async_all(void *futures) {
    void *promise = rt_promise_new();
    void *future = rt_promise_get_future(promise);

    int64_t count = futures ? rt_seq_len(futures) : 0;
    if (count == 0) {
        void *results = rt_seq_new();
        rt_promise_set_owned(promise, results);
        if (rt_obj_release_check0(results))
            rt_obj_free(results);
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    async_all_state *state =
        (async_all_state *)rt_obj_new_i64(0, (int64_t)sizeof(async_all_state));
    if (!state) {
        async_promise_error_cstr(promise, "alloc failed");
        if (rt_obj_release_check0(promise))
            rt_obj_free(promise);
        return future;
    }

    state->promise = promise; // Takes ownership of the creator reference.
    state->monitor = rt_obj_new_i64(0, 1);
    state->results = rt_seq_with_capacity(count);
    state->sources = (void **)calloc((size_t)count, sizeof(void *));
    state->listeners =
        (async_all_listener_ctx **)calloc((size_t)count, sizeof(async_all_listener_ctx *));
    state->owned_slots = (uint8_t *)calloc((size_t)count, sizeof(uint8_t));
    state->slot_count = count;
    state->remaining = count;
    state->completed = 0;
    rt_obj_set_finalizer(state, async_all_state_finalize);
    if (!state->monitor || !state->results || !state->sources || !state->listeners ||
        !state->owned_slots) {
        async_promise_error_cstr(promise, "Async.All: failed to allocate state");
        if (rt_obj_release_check0(state))
            rt_obj_free(state);
        return future;
    }

    for (int64_t i = 0; i < count; i++)
        rt_seq_push_raw(state->results, NULL);

    int8_t registration_failed = 0;
    for (int64_t i = 0; i < count; i++) {
        async_all_listener_ctx *listener =
            (async_all_listener_ctx *)calloc(1, sizeof(async_all_listener_ctx));
        if (!listener) {
            registration_failed = 1;
            break;
        }
        listener->state = state;
        listener->index = i;
        state->sources[i] = rt_seq_get(futures, i);
        state->listeners[i] = listener;

        rt_obj_retain_maybe(state);
        if (!rt_future_on_complete_ex(
                state->sources[i], async_all_complete, listener, async_all_listener_cancel)) {
            if (rt_obj_release_check0(state))
                rt_obj_free(state);
            state->listeners[i] = NULL;
            free(listener);
            registration_failed = 1;
            break;
        }
    }

    if (registration_failed) {
        int8_t should_resolve = 0;
        rt_monitor_enter(state->monitor);
        if (!state->completed) {
            state->completed = 1;
            should_resolve = 1;
        }
        rt_monitor_exit(state->monitor);
        if (should_resolve) {
            async_promise_error_cstr(promise, "Async.All: failed to register completion");
            async_all_cancel_remaining(state, -1);
        }
    }

    if (rt_obj_release_check0(state))
        rt_obj_free(state);
    return future;
}
