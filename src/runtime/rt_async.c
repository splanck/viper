//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_async.c
/// @brief Async task combinators built on Future/Promise + threads.
///
/// Provides high-level async patterns by composing the existing
/// Future/Promise, Thread, and Cancellation primitives.
///
//===----------------------------------------------------------------------===//

#include "rt_async.h"

#include "rt_cancellation.h"
#include "rt_future.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threads.h"

#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal: async_run helper
//=============================================================================

typedef struct
{
    void *(*callback)(void *);
    void *arg;
    void *promise;
} async_run_ctx;

static void async_run_entry(void *ctx_ptr)
{
    async_run_ctx *ctx = (async_run_ctx *)ctx_ptr;
    void *result = NULL;
    void *promise = ctx->promise;
    void *(*cb)(void *) = ctx->callback;
    void *arg = ctx->arg;
    free(ctx);

    result = cb(arg);
    rt_promise_set(promise, result);
}

void *rt_async_run(void *callback, void *arg)
{
    void *promise;
    void *future;
    async_run_ctx *ctx;

    if (!callback)
    {
        rt_trap("Async.Run: nil callback");
        return NULL;
    }

    promise = rt_promise_new();
    future = rt_promise_get_future(promise);

    ctx = (async_run_ctx *)malloc(sizeof(async_run_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return future;
    }

    ctx->callback = (void *(*)(void *))callback;
    ctx->arg = arg;
    ctx->promise = promise;

    rt_thread_start((void *)async_run_entry, ctx);
    return future;
}

//=============================================================================
// Internal: async_all helper
//=============================================================================

typedef struct
{
    void *futures_seq;
    void *promise;
} async_all_ctx;

static void async_all_entry(void *ctx_ptr)
{
    async_all_ctx *ctx = (async_all_ctx *)ctx_ptr;
    void *futures_seq = ctx->futures_seq;
    void *promise = ctx->promise;
    int64_t count;
    int64_t i;
    void *results;

    free(ctx);

    count = rt_seq_len(futures_seq);
    results = rt_seq_new();

    for (i = 0; i < count; i++)
    {
        void *f = rt_seq_get(futures_seq, i);
        rt_future_wait(f);

        if (rt_future_is_error(f))
        {
            rt_string err = rt_future_get_error(f);
            rt_promise_set_error(promise, err);
            return;
        }
        else
        {
            void *val = rt_future_get(f);
            rt_seq_push(results, val);
        }
    }

    rt_promise_set(promise, results);
}

void *rt_async_all(void *futures)
{
    void *promise;
    void *future;
    async_all_ctx *ctx;

    if (!futures || rt_seq_len(futures) == 0)
    {
        promise = rt_promise_new();
        future = rt_promise_get_future(promise);
        rt_promise_set(promise, rt_seq_new());
        return future;
    }

    promise = rt_promise_new();
    future = rt_promise_get_future(promise);

    ctx = (async_all_ctx *)malloc(sizeof(async_all_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return future;
    }

    ctx->futures_seq = futures;
    ctx->promise = promise;

    rt_thread_start((void *)async_all_entry, ctx);
    return future;
}

//=============================================================================
// Internal: async_any helper
//=============================================================================

typedef struct
{
    void *futures_seq;
    void *promise;
} async_any_ctx;

static void async_any_entry(void *ctx_ptr)
{
    async_any_ctx *ctx = (async_any_ctx *)ctx_ptr;
    void *futures_seq = ctx->futures_seq;
    void *promise = ctx->promise;
    int64_t count;

    free(ctx);

    count = rt_seq_len(futures_seq);

    /* Spin-poll until any future is done. Not ideal but portable
       across platforms and avoids building a select-like mechanism. */
    for (;;)
    {
        int64_t i;
        for (i = 0; i < count; i++)
        {
            void *f = rt_seq_get(futures_seq, i);
            if (rt_future_is_done(f))
            {
                if (rt_future_is_error(f))
                {
                    rt_promise_set_error(promise, rt_future_get_error(f));
                }
                else
                {
                    void *val = rt_future_get(f);
                    rt_promise_set(promise, val);
                }
                return;
            }
        }
        rt_thread_sleep(1); /* yield to avoid busy-wait */
    }
}

void *rt_async_any(void *futures)
{
    void *promise;
    void *future;
    async_any_ctx *ctx;

    if (!futures || rt_seq_len(futures) == 0)
    {
        promise = rt_promise_new();
        future = rt_promise_get_future(promise);
        rt_promise_set_error(promise,
                             rt_string_from_bytes("Async.Any: empty futures", 24));
        return future;
    }

    promise = rt_promise_new();
    future = rt_promise_get_future(promise);

    ctx = (async_any_ctx *)malloc(sizeof(async_any_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return future;
    }

    ctx->futures_seq = futures;
    ctx->promise = promise;

    rt_thread_start((void *)async_any_entry, ctx);
    return future;
}

//=============================================================================
// async_delay
//=============================================================================

typedef struct
{
    int64_t ms;
    void *promise;
} async_delay_ctx;

static void async_delay_entry(void *ctx_ptr)
{
    async_delay_ctx *ctx = (async_delay_ctx *)ctx_ptr;
    int64_t ms = ctx->ms;
    void *promise = ctx->promise;
    free(ctx);

    if (ms > 0)
        rt_thread_sleep(ms);
    rt_promise_set(promise, NULL);
}

void *rt_async_delay(int64_t ms)
{
    void *promise;
    void *future;
    async_delay_ctx *ctx;

    if (ms < 0)
        ms = 0;

    promise = rt_promise_new();
    future = rt_promise_get_future(promise);

    ctx = (async_delay_ctx *)malloc(sizeof(async_delay_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return future;
    }

    ctx->ms = ms;
    ctx->promise = promise;

    rt_thread_start((void *)async_delay_entry, ctx);
    return future;
}

//=============================================================================
// async_map
//=============================================================================

typedef struct
{
    void *source_future;
    void *(*mapper)(void *, void *);
    void *arg;
    void *promise;
} async_map_ctx;

static void async_map_entry(void *ctx_ptr)
{
    async_map_ctx *ctx = (async_map_ctx *)ctx_ptr;
    void *source = ctx->source_future;
    void *(*mapper)(void *, void *) = ctx->mapper;
    void *arg = ctx->arg;
    void *promise = ctx->promise;
    free(ctx);

    rt_future_wait(source);

    if (rt_future_is_error(source))
    {
        rt_promise_set_error(promise, rt_future_get_error(source));
    }
    else
    {
        void *val = rt_future_get(source);
        void *mapped = mapper(val, arg);
        rt_promise_set(promise, mapped);
    }
}

void *rt_async_map(void *future, void *mapper, void *arg)
{
    void *promise;
    void *result_future;
    async_map_ctx *ctx;

    if (!future || !mapper)
    {
        rt_trap("Async.Map: nil future or mapper");
        return NULL;
    }

    promise = rt_promise_new();
    result_future = rt_promise_get_future(promise);

    ctx = (async_map_ctx *)malloc(sizeof(async_map_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return result_future;
    }

    ctx->source_future = future;
    ctx->mapper = (void *(*)(void *, void *))mapper;
    ctx->arg = arg;
    ctx->promise = promise;

    rt_thread_start((void *)async_map_entry, ctx);
    return result_future;
}

//=============================================================================
// async_run_cancellable
//=============================================================================

typedef struct
{
    void *(*callback)(void *, void *);
    void *arg;
    void *token;
    void *promise;
} async_cancel_ctx;

static void async_cancel_entry(void *ctx_ptr)
{
    async_cancel_ctx *ctx = (async_cancel_ctx *)ctx_ptr;
    void *(*cb)(void *, void *) = ctx->callback;
    void *arg = ctx->arg;
    void *token = ctx->token;
    void *promise = ctx->promise;
    void *result;
    free(ctx);

    result = cb(arg, token);

    if (token && rt_cancellation_is_cancelled(token))
    {
        rt_promise_set_error(promise,
                             rt_string_from_bytes("cancelled", 9));
    }
    else
    {
        rt_promise_set(promise, result);
    }
}

void *rt_async_run_cancellable(void *callback, void *arg, void *token)
{
    void *promise;
    void *future;
    async_cancel_ctx *ctx;

    if (!callback)
    {
        rt_trap("Async.RunCancellable: nil callback");
        return NULL;
    }

    promise = rt_promise_new();
    future = rt_promise_get_future(promise);

    ctx = (async_cancel_ctx *)malloc(sizeof(async_cancel_ctx));
    if (!ctx)
    {
        rt_promise_set_error(promise, rt_string_from_bytes("alloc failed", 12));
        return future;
    }

    ctx->callback = (void *(*)(void *, void *))callback;
    ctx->arg = arg;
    ctx->token = token;
    ctx->promise = promise;

    rt_thread_start((void *)async_cancel_entry, ctx);
    return future;
}
