//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_animation_events.c
// Purpose: Immutable event-batch snapshots shared by AnimStateMachine and
//          AnimTimeline.
//
//===----------------------------------------------------------------------===//

#include "rt_animation_events.h"

#include "rt_box.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_trap.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t *ids;
    int64_t count;
} rt_animation_event_batch_impl;

/// @brief Safe-cast a handle to an event batch.
/// @param ptr Candidate object pointer.
/// @param api Public API name for trap diagnostics.
/// @return Batch implementation, or NULL when @p ptr is NULL.
static rt_animation_event_batch_impl *checked_event_batch(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_ANIMATION_EVENT_BATCH_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_animation_event_batch_impl *)ptr;
}

/// @brief Finalizer that releases the copied ID array.
static void event_batch_finalizer(void *obj) {
    rt_animation_event_batch_impl *batch = (rt_animation_event_batch_impl *)obj;
    free(batch->ids);
    batch->ids = NULL;
    batch->count = 0;
}

void *rt_animation_event_batch_from_ids(const int64_t *ids, int64_t count) {
    rt_animation_event_batch_impl *batch = (rt_animation_event_batch_impl *)rt_obj_new_i64(
        RT_ANIMATION_EVENT_BATCH_CLASS_ID, (int64_t)sizeof(rt_animation_event_batch_impl));
    if (!batch)
        return NULL;
    batch->ids = NULL;
    batch->count = 0;
    rt_obj_set_finalizer(batch, event_batch_finalizer);

    if (!ids || count <= 0)
        return batch; // a legitimately empty snapshot (zero events fired)

    // A memory failure while copying the fired IDs must NOT masquerade as an empty
    // batch — that is indistinguishable from a genuine zero-event frame and would
    // silently drop gameplay events under memory pressure. Fail the whole snapshot
    // transactionally by returning NULL, matching the documented "NULL on
    // allocation failure" contract, so callers can tell the two apart (VDOC-279).
    if ((uint64_t)count > SIZE_MAX / sizeof(int64_t)) {
        if (rt_obj_release_check0(batch))
            rt_obj_free(batch);
        return NULL;
    }

    batch->ids = (int64_t *)malloc((size_t)count * sizeof(int64_t));
    if (!batch->ids) {
        if (rt_obj_release_check0(batch))
            rt_obj_free(batch);
        return NULL;
    }
    memcpy(batch->ids, ids, (size_t)count * sizeof(int64_t));
    batch->count = count;
    return batch;
}

int64_t rt_animation_event_batch_count(void *ptr) {
    rt_animation_event_batch_impl *batch =
        checked_event_batch(ptr, "AnimationEventBatch.Count: expected AnimationEventBatch");
    return batch ? batch->count : 0;
}

int64_t rt_animation_event_batch_get_id(void *ptr, int64_t index) {
    rt_animation_event_batch_impl *batch =
        checked_event_batch(ptr, "AnimationEventBatch.GetId: expected AnimationEventBatch");
    if (!batch || index < 0 || index >= batch->count)
        return 0;
    return batch->ids[index];
}

int8_t rt_animation_event_batch_contains(void *ptr, int64_t event_id) {
    rt_animation_event_batch_impl *batch =
        checked_event_batch(ptr, "AnimationEventBatch.Contains: expected AnimationEventBatch");
    if (!batch)
        return 0;
    for (int64_t i = 0; i < batch->count; ++i) {
        if (batch->ids[i] == event_id)
            return 1;
    }
    return 0;
}

void *rt_animation_event_batch_ids(void *ptr) {
    rt_animation_event_batch_impl *batch =
        checked_event_batch(ptr, "AnimationEventBatch.Ids: expected AnimationEventBatch");
    void *seq = rt_seq_new();
    if (!seq)
        return NULL;
    if (!batch)
        return seq;
    rt_seq_set_owns_elements(seq, 1);
    for (int64_t i = 0; i < batch->count; ++i) {
        void *boxed = rt_box_i64(batch->ids[i]);
        rt_seq_push(seq, boxed);
        if (boxed && rt_obj_release_check0(boxed))
            rt_obj_free(boxed);
    }
    return seq;
}
