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
//   - The transient-buffer/object hash sets mirror their tracking lists exactly.
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

#define CANVAS3D_FINAL_OVERLAY_ARENA_DEFAULT_BYTES (256u * 1024u)
#define CANVAS3D_FINAL_OVERLAY_ARENA_RETAIN_BYTES (4u * 1024u * 1024u)

/// @brief Round @p value up to the next power-of-two size, saturating on overflow.
/// @details Final-overlay arena growth only happens when no recorded command points into the
/// arena, so rounding up amortizes future HUD allocations without risking pointer invalidation.
/// @param value Minimum capacity requested by the caller.
/// @return A power-of-two capacity at least @p value, or @p value when rounding would overflow.
static size_t canvas3d_next_power_of_two_size(size_t value) {
    size_t result = 1u;
    if (value <= 1u)
        return 1u;
    while (result < value) {
        if (result > SIZE_MAX / 2u)
            return value;
        result *= 2u;
    }
    return result;
}

/// @brief Return whether @p alignment is a valid non-zero power-of-two.
/// @param alignment Alignment value to validate.
/// @return Non-zero when @p alignment can be used for arena pointer rounding.
static int canvas3d_valid_power_of_two_alignment(size_t alignment) {
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

/// @brief Allocate stable storage from the retained final-overlay vertex/index arena.
void *canvas3d_alloc_final_overlay_arena(rt_canvas3d *c, size_t bytes, size_t alignment) {
    size_t mask;
    size_t aligned_offset;
    size_t end_offset;
    size_t requested_capacity;
    uint8_t *grown;

    if (!c || bytes == 0u)
        return NULL;
    if (!canvas3d_valid_power_of_two_alignment(alignment))
        alignment = sizeof(void *);
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    mask = alignment - 1u;
    if (c->final_overlay_arena_used > SIZE_MAX - mask)
        return NULL;
    aligned_offset = (c->final_overlay_arena_used + mask) & ~mask;
    if (aligned_offset > SIZE_MAX - bytes)
        return NULL;
    end_offset = aligned_offset + bytes;
    if (end_offset > c->final_overlay_arena_peak)
        c->final_overlay_arena_peak = end_offset;
    if (end_offset > c->final_overlay_arena_capacity) {
        if (c->final_overlay_arena_used != 0u)
            return NULL;
        requested_capacity = end_offset;
        if (requested_capacity < CANVAS3D_FINAL_OVERLAY_ARENA_DEFAULT_BYTES)
            requested_capacity = CANVAS3D_FINAL_OVERLAY_ARENA_DEFAULT_BYTES;
        requested_capacity = canvas3d_next_power_of_two_size(requested_capacity);
        grown = (uint8_t *)malloc(requested_capacity);
        if (!grown)
            return NULL;
        free(c->final_overlay_arena);
        c->final_overlay_arena = grown;
        c->final_overlay_arena_capacity = requested_capacity;
    }
    c->final_overlay_arena_used = end_offset;
    return c->final_overlay_arena + aligned_offset;
}

/// @brief Reset retained final-overlay arena state after overlay replay.
void canvas3d_reset_final_overlay_arena(rt_canvas3d *c) {
    if (!c)
        return;
    c->final_overlay_arena_used = 0u;
    c->final_overlay_arena_peak = 0u;
    if (c->final_overlay_arena_capacity > CANVAS3D_FINAL_OVERLAY_ARENA_RETAIN_BYTES) {
        free(c->final_overlay_arena);
        c->final_overlay_arena = NULL;
        c->final_overlay_arena_capacity = 0u;
    }
}

/// @brief Clear the per-frame transient-buffer tracking set (all slots empty).
static void canvas3d_temp_buffer_set_clear(rt_canvas3d *c) {
    if (!c || !c->temp_buffer_set || c->temp_buffer_set_capacity <= 0)
        return;
    memset(c->temp_buffer_set, 0, (size_t)c->temp_buffer_set_capacity * sizeof(void *));
}

/// @brief Ensure the transient-buffer set is sized for @p count_hint tracked buffers.
/// @details The set is an open-addressed duplicate filter for `temp_buffers`. It is rebuilt from
///          the list after growth so callers can keep using swap-remove on the list.
/// @param c Canvas that owns the per-frame temp buffers.
/// @param count_hint Expected tracked buffer count.
/// @return Non-zero when the set exists and contains the current list contents.
static int canvas3d_ensure_temp_buffer_set(rt_canvas3d *c, int32_t count_hint) {
    int32_t needed;
    void **grown;
    if (!c)
        return 0;
    if (count_hint > INT32_MAX / 2)
        return 0;
    needed = canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
    if (needed < 32)
        needed = 32;
    if (c->temp_buffer_set_capacity >= needed)
        return 1;
    if ((size_t)needed > SIZE_MAX / sizeof(*c->temp_buffer_set))
        return 0;
    grown = (void **)realloc(c->temp_buffer_set, (size_t)needed * sizeof(*grown));
    if (!grown)
        return 0;
    c->temp_buffer_set = grown;
    c->temp_buffer_set_capacity = needed;
    canvas3d_temp_buffer_set_clear(c);
    for (int32_t i = 0; i < c->temp_buf_count; ++i) {
        void *existing = c->temp_buffers[i];
        int32_t mask;
        int32_t slot;
        if (!existing)
            continue;
        mask = c->temp_buffer_set_capacity - 1;
        slot = (int32_t)(canvas3d_hash_u64((uintptr_t)existing) & (uint32_t)mask);
        for (int32_t probe = 0; probe < c->temp_buffer_set_capacity; ++probe) {
            if (!c->temp_buffer_set[slot]) {
                c->temp_buffer_set[slot] = existing;
                break;
            }
            slot = (slot + 1) & mask;
        }
    }
    return 1;
}

/// @brief Return whether @p buffer is currently tracked as a per-frame transient buffer.
/// @details Uses the hash set when available and falls back to a linear scan if scratch allocation
///          fails, preserving the old no-duplicate behavior under memory pressure.
static int canvas3d_temp_buffer_set_contains(rt_canvas3d *c, void *buffer) {
    int32_t mask;
    int32_t slot;
    if (!c || !buffer || c->temp_buf_count <= 0)
        return 0;
    if (!c->temp_buffer_set || c->temp_buffer_set_capacity < c->temp_buf_count * 2) {
        if (!canvas3d_ensure_temp_buffer_set(c, c->temp_buf_count + 1)) {
            for (int32_t i = 0; i < c->temp_buf_count; ++i) {
                if (c->temp_buffers[i] == buffer)
                    return 1;
            }
            return 0;
        }
    }
    mask = c->temp_buffer_set_capacity - 1;
    slot = (int32_t)(canvas3d_hash_u64((uintptr_t)buffer) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_buffer_set_capacity; ++probe) {
        void *entry = c->temp_buffer_set[slot];
        if (!entry)
            return 0;
        if (entry == buffer)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

/// @brief Insert @p buffer into the transient-buffer duplicate set.
/// @return Non-zero when the buffer is present in the set after the call.
static int canvas3d_temp_buffer_set_insert(rt_canvas3d *c, void *buffer) {
    int32_t mask;
    int32_t slot;
    if (!c || !buffer)
        return 0;
    if (!canvas3d_ensure_temp_buffer_set(c, c->temp_buf_count + 1))
        return 0;
    mask = c->temp_buffer_set_capacity - 1;
    slot = (int32_t)(canvas3d_hash_u64((uintptr_t)buffer) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->temp_buffer_set_capacity; ++probe) {
        if (!c->temp_buffer_set[slot]) {
            c->temp_buffer_set[slot] = buffer;
            return 1;
        }
        if (c->temp_buffer_set[slot] == buffer)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

/// @brief Rebuild the transient-buffer hash set from the tracked-buffer list.
static void canvas3d_rebuild_temp_buffer_set(rt_canvas3d *c) {
    if (!c || !c->temp_buffer_set)
        return;
    canvas3d_temp_buffer_set_clear(c);
    for (int32_t i = 0; i < c->temp_buf_count; ++i)
        canvas3d_temp_buffer_set_insert(c, c->temp_buffers[i]);
}

/// @brief Track a malloc'd temp buffer so it is freed at end-of-frame.
int canvas3d_track_temp_buffer(rt_canvas3d *c, void *buffer) {
    if (!c || !buffer)
        return 0;
    if (canvas3d_temp_buffer_set_contains(c, buffer))
        return 1;
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
    if (!canvas3d_temp_buffer_set_insert(c, buffer)) {
        for (int32_t i = 0; i < c->temp_buf_count; ++i) {
            if (c->temp_buffers[i] == buffer)
                return 1;
        }
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
            int32_t last = c->temp_buf_count - 1;
            c->temp_buffers[i] = c->temp_buffers[last];
            c->temp_buffers[last] = NULL;
            c->temp_buf_count = last;
            canvas3d_rebuild_temp_buffer_set(c);
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

/// @brief Release a tracked mesh-geometry snapshot and refund its frame byte budget.
/// @details Mesh snapshots are ordinary frame-temp buffers, but snapshot byte accounting is
///   maintained separately so large dynamic meshes cannot consume unbounded memory. Callers use
///   this helper when a later rebase, tangent-generation, or validation step fails after the
///   buffers were successfully tracked. The byte refund is saturating: stale or duplicate rollback
///   calls cannot underflow the frame counter.
/// @param c Canvas that owns the per-frame snapshot budget.
/// @param vertices Tracked vertex snapshot buffer, or NULL.
/// @param vertex_bytes Number of bytes charged for @p vertices.
/// @param indices Tracked index snapshot buffer, or NULL.
/// @param index_bytes Number of bytes charged for @p indices.
void canvas3d_release_tracked_mesh_snapshot(
    rt_canvas3d *c, void *vertices, size_t vertex_bytes, void *indices, size_t index_bytes) {
    size_t total_bytes;
    if (!c)
        return;
    if (vertex_bytes > SIZE_MAX - index_bytes)
        total_bytes = SIZE_MAX;
    else
        total_bytes = vertex_bytes + index_bytes;
    if (total_bytes >= c->mesh_snapshot_bytes)
        c->mesh_snapshot_bytes = 0u;
    else
        c->mesh_snapshot_bytes -= total_bytes;
    canvas3d_release_tracked_temp_buffer(c, vertices);
    canvas3d_release_tracked_temp_buffer(c, indices);
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
            int32_t last = c->final_overlay_temp_buf_count - 1;
            c->final_overlay_temp_buffers[i] = c->final_overlay_temp_buffers[last];
            c->final_overlay_temp_buffers[last] = NULL;
            c->final_overlay_temp_buf_count = last;
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
    int32_t needed = canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
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
            int32_t last = c->temp_obj_count - 1;
            c->temp_objects[i] = c->temp_objects[last];
            c->temp_objects[last] = NULL;
            c->temp_obj_count = last;
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
    if (c->mesh_snapshot_bytes > (size_t)INT64_MAX)
        c->last_mesh_snapshot_bytes = INT64_MAX;
    else
        c->last_mesh_snapshot_bytes = (int64_t)c->mesh_snapshot_bytes;
    for (int32_t i = 0; i < c->temp_buf_count; i++)
        free(c->temp_buffers[i]);
    c->temp_buf_count = 0;
    canvas3d_temp_buffer_set_clear(c);
    c->float_snapshot_count = 0;
    c->mesh_snapshot_count = 0;
    canvas3d_mesh_snapshot_hash_clear(c);
    c->mesh_snapshot_bytes = 0u;
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
