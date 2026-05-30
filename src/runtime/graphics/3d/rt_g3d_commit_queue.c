//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

typedef struct rt_g3d_commit_item {
    rt_g3d_commit_fn fn;
    void *user_data;
    uint64_t cost;
} rt_g3d_commit_item;

typedef struct rt_g3d_commit_queue {
    void *items;
    volatile int64_t submitted;
    volatile int64_t drained;
} rt_g3d_commit_queue;

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

static void rt_g3d_commit_queue_discard_pending(rt_g3d_commit_queue *queue) {
    if (!queue || !queue->items)
        return;
    for (;;) {
        rt_g3d_commit_item *item = (rt_g3d_commit_item *)rt_concqueue_try_dequeue(queue->items);
        if (!item)
            break;
        free(item);
    }
}

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

int8_t rt_g3d_commit_queue_enqueue_cost(void *obj,
                                        rt_g3d_commit_fn fn,
                                        void *user_data,
                                        uint64_t cost) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items || !fn)
        return 0;
    rt_g3d_commit_item *item = (rt_g3d_commit_item *)malloc(sizeof(rt_g3d_commit_item));
    if (!item)
        rt_trap("Graphics3D commit queue: memory allocation failed");
    item->fn = fn;
    item->user_data = user_data;
    item->cost = cost;
    rt_concqueue_enqueue(queue->items, item);
    __atomic_fetch_add(&queue->submitted, 1, __ATOMIC_RELAXED);
    return 1;
}

int8_t rt_g3d_commit_queue_enqueue(void *obj, rt_g3d_commit_fn fn, void *user_data) {
    return rt_g3d_commit_queue_enqueue_cost(obj, fn, user_data, 1u);
}

static uint64_t rt_g3d_commit_queue_cost_add(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b)
        return UINT64_MAX;
    return a + b;
}

int64_t rt_g3d_commit_queue_drain_budget(void *obj, int64_t max_items, uint64_t max_cost) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items)
        return 0;
    RT_ASSERT_MAIN_THREAD();

    int64_t count = 0;
    uint64_t cost = 0;
    while (max_items <= 0 || count < max_items) {
        rt_g3d_commit_item *peek =
            (rt_g3d_commit_item *)rt_concqueue_peek(queue->items);
        if (!peek)
            break;
        uint64_t item_cost = peek->cost;
        if (max_cost != UINT64_MAX && item_cost > 0) {
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
        fn(user_data);
        count++;
        cost = rt_g3d_commit_queue_cost_add(cost, item_cost);
    }
    if (count > 0)
        __atomic_fetch_add(&queue->drained, count, __ATOMIC_RELAXED);
    return count;
}

int64_t rt_g3d_commit_queue_drain(void *obj, int64_t max_items) {
    return rt_g3d_commit_queue_drain_budget(obj, max_items, UINT64_MAX);
}

int64_t rt_g3d_commit_queue_pending(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items)
        return 0;
    return rt_concqueue_len(queue->items);
}

int64_t rt_g3d_commit_queue_submitted(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue)
        return 0;
    return __atomic_load_n(&queue->submitted, __ATOMIC_RELAXED);
}

int64_t rt_g3d_commit_queue_drained(void *obj) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue)
        return 0;
    return __atomic_load_n(&queue->drained, __ATOMIC_RELAXED);
}
