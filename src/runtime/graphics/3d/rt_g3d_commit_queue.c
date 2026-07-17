//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/3d/rt_g3d_commit_queue.c
// Purpose: Internal main-thread commit queue for Graphics3D/Game3D worker jobs.
//
//===----------------------------------------------------------------------===//

#include "rt_g3d_commit_queue.h"

#include "rt_concqueue.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_trap.h"

#include <stdlib.h>

/// @brief One queued commit: the callback, its user data, and its budget cost.
typedef struct rt_g3d_commit_item {
    rt_g3d_commit_fn fn;
    rt_g3d_commit_cancel_fn cancel_fn;
    void *user_data;
    uint64_t cost;
} rt_g3d_commit_item;

/// @brief Queue state: a concurrent FIFO of items plus submit/drain counters.
typedef struct rt_g3d_commit_queue {
    void *items;
    int64_t submitted;
    int64_t drained;
} rt_g3d_commit_queue;

/// @brief Test-only countdown for deterministic enqueue-wrapper allocation failures.
static volatile int g_rt_g3d_commit_queue_fail_allocations;

/// @brief Consume one requested allocation failure without racing concurrent producers.
static int rt_g3d_commit_queue_should_fail_allocation(void) {
    int remaining = rt_atomic_load_i32(&g_rt_g3d_commit_queue_fail_allocations, __ATOMIC_ACQUIRE);
    while (remaining > 0) {
        int expected = remaining;
        if (rt_atomic_compare_exchange_i32(&g_rt_g3d_commit_queue_fail_allocations,
                                           &expected,
                                           remaining - 1,
                                           __ATOMIC_ACQ_REL,
                                           __ATOMIC_ACQUIRE))
            return 1;
        remaining = expected;
    }
    return 0;
}

/// @brief Configure deterministic allocation failure injection for unit tests.
void rt_g3d_commit_queue_test_fail_next_allocations(int32_t count) {
    rt_atomic_store_i32(
        &g_rt_g3d_commit_queue_fail_allocations, count > 0 ? count : 0, __ATOMIC_RELEASE);
}

/// @brief Allocate a commit queue wrapping a fresh concurrent FIFO.
/// @return Opaque queue handle, or NULL if either allocation fails.
void *rt_g3d_commit_queue_new(void) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)calloc(1, sizeof(rt_g3d_commit_queue));
    if (!queue)
        return NULL;
    queue->items = rt_concqueue_new();
    if (!queue->items) {
        free(queue);
        return NULL;
    }
    return queue;
}

/// @brief Drain and free every pending item without running its callback.
/// @details Used during teardown so worker-produced commits still in the queue are
///          reclaimed rather than leaked; the callbacks are intentionally skipped.
static void rt_g3d_commit_queue_discard_pending(rt_g3d_commit_queue *queue) {
    if (!queue || !queue->items)
        return;
    for (;;) {
        rt_g3d_commit_item *item = (rt_g3d_commit_item *)rt_concqueue_try_dequeue(queue->items);
        if (!item)
            break;
        if (item->cancel_fn)
            item->cancel_fn(item->user_data);
        free(item);
    }
}

/// @brief Close the queue, discard any pending commits, and free all backing memory.
void rt_g3d_commit_queue_free(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue)
        return;
    if (queue->items) {
        rt_concqueue_close(queue->items);
        rt_g3d_commit_queue_discard_pending(queue);
        if (rt_obj_release_check0(queue->items))
            rt_obj_free(queue->items);
        queue->items = NULL;
    }
    free(queue);
}

/// @brief Enqueue a commit callback tagged with a main-thread cost estimate and cleanup hook.
/// @details If allocation fails, ownership remains with the caller and zero is returned. If the
///          queue is later closed before the item drains, `cancel_fn` receives `user_data` so
///          payload ownership can be released.
/// @return 1 when queued; 0 for invalid input, a closed queue, or allocation failure.
int8_t rt_g3d_commit_queue_enqueue_cost_cancel(void *obj,
                                               rt_g3d_commit_fn fn,
                                               void *user_data,
                                               uint64_t cost,
                                               rt_g3d_commit_cancel_fn cancel_fn) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items || !fn)
        return 0;
    if (rt_concqueue_get_is_closed(queue->items))
        return 0;
    rt_g3d_commit_item *item = NULL;
    if (!rt_g3d_commit_queue_should_fail_allocation())
        item = (rt_g3d_commit_item *)malloc(sizeof(rt_g3d_commit_item));
    if (!item)
        return 0;
    item->fn = fn;
    item->cancel_fn = cancel_fn;
    item->user_data = user_data;
    item->cost = cost;
    if (!rt_concqueue_try_enqueue(queue->items, item)) {
        free(item);
        return 0;
    }
    rt_atomic_fetch_add_i64(&queue->submitted, 1, __ATOMIC_RELAXED);
    return 1;
}

/// @brief Enqueue a commit callback tagged with a main-thread cost estimate.
int8_t rt_g3d_commit_queue_enqueue_cost(void *obj,
                                        rt_g3d_commit_fn fn,
                                        void *user_data,
                                        uint64_t cost) {
    return rt_g3d_commit_queue_enqueue_cost_cancel(obj, fn, user_data, cost, NULL);
}

/// @brief Enqueue a commit callback with the default unit cost (1).
int8_t rt_g3d_commit_queue_enqueue(void *obj, rt_g3d_commit_fn fn, void *user_data) {
    return rt_g3d_commit_queue_enqueue_cost(obj, fn, user_data, RT_G3D_COMMIT_COST_UNIT);
}

/// @brief Saturating unsigned add — clamps to UINT64_MAX instead of overflowing.
/// @details Keeps the running cost budget monotonic even when per-item costs sum
///          past 64 bits, so the drain loop's budget comparison never wraps around.
static uint64_t rt_g3d_commit_queue_cost_add(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b)
        return UINT64_MAX;
    return a + b;
}

/// @brief Run pending commits on the main thread under item-count and cost budgets.
/// @details Peeks each item's cost before dequeuing, so a commit runs only when it
///          fits the remaining cost budget. A zero budget drains only zero-cost work; an unlimited
///          budget disables cost limiting. The first item always runs even if it exceeds a positive
///          budget (the `count > 0` guard), so an oversized asset commit cannot stall the queue
///          forever. Asserts it is called on the main thread.
/// @return Number of commits actually run.
int64_t rt_g3d_commit_queue_drain_budget(void *obj, int64_t max_items, uint64_t max_cost) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    int no_cost_limit;
    if (!queue || !queue->items)
        return 0;
    RT_ASSERT_MAIN_THREAD();

    int64_t count = 0;
    uint64_t cost = 0;
    no_cost_limit = (max_cost == UINT64_MAX);
    while (max_items <= 0 || count < max_items) {
        rt_g3d_commit_item *peek = (rt_g3d_commit_item *)rt_concqueue_peek(queue->items);
        if (!peek)
            break;
        uint64_t item_cost = peek->cost;
        if (!no_cost_limit && item_cost > 0) {
            uint64_t next_cost = rt_g3d_commit_queue_cost_add(cost, item_cost);
            if (next_cost > max_cost) {
                if (count > 0 || max_cost == 0)
                    break;
            }
        }
        rt_g3d_commit_item *item = (rt_g3d_commit_item *)rt_concqueue_try_dequeue(queue->items);
        if (!item)
            break;
        rt_g3d_commit_fn fn = item->fn;
        void *user_data = item->user_data;
        item_cost = item->cost;
        free(item);
        if (fn) {
            fn(user_data);
            count++;
            cost = rt_g3d_commit_queue_cost_add(cost, item_cost);
        }
    }
    if (count > 0)
        rt_atomic_fetch_add_i64(&queue->drained, count, __ATOMIC_RELAXED);
    return count;
}

/// @brief Drain up to @p max_items commits with no cost budget.
int64_t rt_g3d_commit_queue_drain(void *obj, int64_t max_items) {
    return rt_g3d_commit_queue_drain_budget(obj, max_items, RT_G3D_COMMIT_COST_UNLIMITED);
}

/// @brief Approximate count of commits still waiting to run.
int64_t rt_g3d_commit_queue_pending(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items)
        return 0;
    int64_t pending = rt_concqueue_len(queue->items);
    return pending > 0 ? pending : 0;
}

/// @brief Total commits ever accepted by the queue (monotonic counter).
int64_t rt_g3d_commit_queue_submitted(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue)
        return 0;
    int64_t submitted = rt_atomic_load_i64(&queue->submitted, __ATOMIC_RELAXED);
    return submitted > 0 ? submitted : 0;
}

/// @brief Total commits ever run by a drain call (monotonic counter).
int64_t rt_g3d_commit_queue_drained(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue)
        return 0;
    int64_t drained = rt_atomic_load_i64(&queue->drained, __ATOMIC_RELAXED);
    return drained > 0 ? drained : 0;
}
