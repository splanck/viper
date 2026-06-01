//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_tempmgr.c
// Purpose: Canvas3D per-frame transient-resource tracking — temp buffers,
//   final-overlay temp buffers, and the GC-managed transient-object hash set.
//   Split out of rt_canvas3d.c; the bookkeeping arrays live on rt_canvas3d
//   (see rt_canvas3d_internal.h).
// Key invariants:
//   - Tracked temp buffers/objects are released at end-of-frame, or on a failed
//     allocation path via the release_* helpers.
//   - The transient-object hash set mirrors the temp_objects list exactly.
// Ownership/Lifetime:
//   - Temp buffers are malloc'd elsewhere; tracking takes ownership for the frame.
//   - Tracked objects are retained on insert and released on removal/frame end.
// Links: rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// @brief Track a malloc'd temp buffer so it is freed at end-of-frame.
int canvas3d_track_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->temp_buf_count; ++i) {
        if (c->temp_buffers[i] == buffer)
            return 1;
    }
    if (c->temp_buf_count >= c->temp_buf_capacity) {
        if (c->temp_buf_capacity < 0 || c->temp_buf_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap = c->temp_buf_capacity == 0 ? 8 : c->temp_buf_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb = (void **)realloc(c->temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->temp_buffers = nb;
        c->temp_buf_capacity = new_cap;
    }
    c->temp_buffers[c->temp_buf_count++] = buffer;
    return 1;
}

/// @brief Remove a tracked temp buffer without freeing it.
int canvas3d_untrack_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->temp_buf_count; ++i) {
        if (c->temp_buffers[i] == buffer) {
            for (int32_t j = i; j < c->temp_buf_count - 1; ++j)
                c->temp_buffers[j] = c->temp_buffers[j + 1];
            c->temp_buffers[--c->temp_buf_count] = NULL;
            return 1;
        }
    }
    return 0;
}

/// @brief Untrack and free a temp buffer when a later allocation path fails.
void canvas3d_release_tracked_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!buffer)
        return;
    if (canvas3d_untrack_temp_buffer(c, buffer))
        free(buffer);
}

/// @brief Track a malloc'd buffer used by deferred final-overlay commands.
///
/// Final overlays are recorded before frame finalization and replayed after
/// post-FX. Their geometry must survive normal End() cleanup, so they use a
/// separate temp-buffer list cleared after Flip() or ClearOverlay().
int canvas3d_track_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    if (c->final_overlay_temp_buf_count >= c->final_overlay_temp_buf_capacity) {
        if (c->final_overlay_temp_buf_capacity < 0 ||
            c->final_overlay_temp_buf_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap =
            c->final_overlay_temp_buf_capacity == 0 ? 8 : c->final_overlay_temp_buf_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb =
            (void **)realloc(c->final_overlay_temp_buffers, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->final_overlay_temp_buffers = nb;
        c->final_overlay_temp_buf_capacity = new_cap;
    }
    c->final_overlay_temp_buffers[c->final_overlay_temp_buf_count++] = buffer;
    return 1;
}

/// @brief Remove a buffer from the final-overlay temp-buffer tracking list (does not free it).
/// @return 1 if it was found and removed, 0 otherwise.
int canvas3d_untrack_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    for (int32_t i = 0; i < c->final_overlay_temp_buf_count; ++i) {
        if (c->final_overlay_temp_buffers[i] == buffer) {
            for (int32_t j = i; j < c->final_overlay_temp_buf_count - 1; ++j)
                c->final_overlay_temp_buffers[j] = c->final_overlay_temp_buffers[j + 1];
            c->final_overlay_temp_buffers[--c->final_overlay_temp_buf_count] = NULL;
            return 1;
        }
    }
    return 0;
}

/// @brief Untrack and free a final-overlay temp buffer in one step.
void canvas3d_release_tracked_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!buffer)
        return;
    if (canvas3d_untrack_final_overlay_temp_buffer(c, buffer))
        free(buffer);
}

/// @brief Clear the per-frame transient-object tracking set (all slots empty).
void canvas3d_temp_object_set_clear(rt_canvas3d *c) {
    if (!c || !c->temp_object_set || c->temp_object_set_capacity <= 0)
        return;
    memset(c->temp_object_set, 0, (size_t)c->temp_object_set_capacity * sizeof(void *));
}

/// @brief Ensure the transient-object set has a power-of-two capacity sized for @p count_hint
/// entries.
int canvas3d_ensure_temp_object_set(rt_canvas3d *c, int32_t count_hint) {
    if (!c)
        return 0;
    if (count_hint > INT32_MAX / 2)
        return 0;
    int32_t needed =
        canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
    if (needed < 32)
        needed = 32;
    if (c->temp_object_set_capacity >= needed)
        return 1;
    if ((size_t)needed > SIZE_MAX / sizeof(*c->temp_object_set))
        return 0;
    void **grown = (void **)realloc(c->temp_object_set, (size_t)needed * sizeof(*grown));
    if (!grown)
        return 0;
    c->temp_object_set = grown;
    c->temp_object_set_capacity = needed;
    canvas3d_temp_object_set_clear(c);
    for (int32_t i = 0; i < c->temp_obj_count; ++i) {
        void *existing = c->temp_objects[i];
        if (!existing)
            continue;
        int32_t mask = c->temp_object_set_capacity - 1;
        int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)existing) & (uint32_t)mask);
        for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
            if (!c->temp_object_set[slot]) {
                c->temp_object_set[slot] = existing;
                break;
            }
            slot = (slot + 1) & mask;
        }
    }
    return 1;
}

/// @brief Whether @p obj is currently tracked as a per-frame transient object (linear-probe
/// lookup).
int canvas3d_temp_object_set_contains(rt_canvas3d *c, void *obj) {
    if (!c || !obj || c->temp_obj_count <= 0)
        return 0;
    if (c->temp_obj_count > INT32_MAX / 2) {
        for (int32_t i = 0; i < c->temp_obj_count; ++i) {
            if (c->temp_objects[i] == obj)
                return 1;
        }
        return 0;
    }
    if (!c->temp_object_set || c->temp_object_set_capacity < c->temp_obj_count * 2) {
        if (!canvas3d_ensure_temp_object_set(c, c->temp_obj_count + 1)) {
            for (int32_t i = 0; i < c->temp_obj_count; ++i) {
                if (c->temp_objects[i] == obj)
                    return 1;
            }
            return 0;
        }
    }
    int32_t mask = c->temp_object_set_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)obj) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
        void *entry = c->temp_object_set[slot];
        if (!entry)
            return 0;
        if (entry == obj)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

/// @brief Track @p obj as a per-frame transient object (linear-probe insert; grows as needed).
int canvas3d_temp_object_set_insert(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return 0;
    if (c->temp_obj_count >= INT32_MAX)
        return 0;
    if (!canvas3d_ensure_temp_object_set(c, c->temp_obj_count + 1))
        return 0;
    int32_t mask = c->temp_object_set_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64((uintptr_t)obj) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_object_set_capacity; ++probe) {
        if (!c->temp_object_set[slot]) {
            c->temp_object_set[slot] = obj;
            return 1;
        }
        if (c->temp_object_set[slot] == obj)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

/// @brief Rebuild the transient-object hash set from the tracked-object list (after
/// growth/removal).
void canvas3d_rebuild_temp_object_set(rt_canvas3d *c) {
    if (!c || !c->temp_object_set)
        return;
    canvas3d_temp_object_set_clear(c);
    for (int32_t i = 0; i < c->temp_obj_count; ++i)
        canvas3d_temp_object_set_insert(c, c->temp_objects[i]);
}

/// @brief Track a GC-managed object for end-of-frame release.
///
/// Retains `obj` immediately so it survives at least until the
/// frame ends, then releases at end-of-frame via `clear_temp_objects`.
int canvas3d_track_temp_object(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return 0;
    if (canvas3d_temp_object_set_contains(c, obj))
        return 1;
    if (c->temp_obj_count >= c->temp_obj_capacity) {
        if (c->temp_obj_capacity < 0 || c->temp_obj_capacity > INT32_MAX / 2)
            return 0;
        int32_t new_cap = c->temp_obj_capacity == 0 ? 8 : c->temp_obj_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return 0;
        void **nb = (void **)realloc(c->temp_objects, (size_t)new_cap * sizeof(void *));
        if (!nb)
            return 0;
        c->temp_objects = nb;
        c->temp_obj_capacity = new_cap;
    }
    if (!canvas3d_temp_object_set_insert(c, obj))
        return 0;
    rt_obj_retain_maybe(obj);
    c->temp_objects[c->temp_obj_count++] = obj;
    return 1;
}

/// @brief Untrack a per-frame transient object and release its reference.
void canvas3d_release_tracked_temp_object(rt_canvas3d *c, void *obj) {
    if (!c || !obj)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; ++i) {
        if (c->temp_objects[i] == obj) {
            for (int32_t j = i; j < c->temp_obj_count - 1; ++j)
                c->temp_objects[j] = c->temp_objects[j + 1];
            c->temp_objects[--c->temp_obj_count] = NULL;
            canvas3d_rebuild_temp_object_set(c);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            return;
        }
    }
}

/// @brief Free every tracked transient buffer (called at end of frame).
void canvas3d_clear_temp_buffers(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
    c->mesh_snapshot_count = 0;
}

/// @brief Release every tracked transient GC object (called at end of frame).
void canvas3d_clear_temp_objects(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t i = 0; i < c->temp_obj_count; i++) {
        if (c->temp_objects[i] && rt_obj_release_check0(c->temp_objects[i]))
            rt_obj_free(c->temp_objects[i]);
        c->temp_objects[i] = NULL;
    }
    c->temp_obj_count = 0;
    canvas3d_temp_object_set_clear(c);
}

#endif /* VIPER_ENABLE_GRAPHICS */
