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

int8_t rt_g3d_commit_queue_enqueue(void *obj, rt_g3d_commit_fn fn, void *user_data) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items || !fn)
        return 0;
    rt_g3d_commit_item *item = (rt_g3d_commit_item *)malloc(sizeof(rt_g3d_commit_item));
    if (!item)
        rt_trap("Graphics3D commit queue: memory allocation failed");
    item->fn = fn;
    item->user_data = user_data;
    rt_concqueue_enqueue(queue->items, item);
    __atomic_fetch_add(&queue->submitted, 1, __ATOMIC_RELAXED);
    return 1;
}

int64_t rt_g3d_commit_queue_drain(void *obj, int64_t max_items) {
    rt_g3d_commit_queue *queue = (rt_g3d_commit_queue *)obj;
    if (!queue || !queue->items)
        return 0;
    RT_ASSERT_MAIN_THREAD();

    int64_t count = 0;
    while (max_items <= 0 || count < max_items) {
        rt_g3d_commit_item *item = (rt_g3d_commit_item *)rt_concqueue_try_dequeue(queue->items);
        if (!item)
            break;
        rt_g3d_commit_fn fn = item->fn;
        void *user_data = item->user_data;
        free(item);
        fn(user_data);
        count++;
    }
    if (count > 0)
        __atomic_fetch_add(&queue->drained, count, __ATOMIC_RELAXED);
    return count;
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
