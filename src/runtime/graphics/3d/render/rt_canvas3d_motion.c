//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_motion.c
// Purpose: Canvas3D motion-history — per-object previous-frame model matrices
//   used to derive motion vectors. A growable entry array plus an open-addressing
//   hash index keyed by a stable per-draw key. Split out of rt_canvas3d.c; the
//   backing arrays live on rt_canvas3d (see rt_canvas3d_internal.h).
// Key invariants:
//   - The hash mirrors the entry array (slot stores entry index + 1; 0 = empty).
//   - An entry's current→previous roll happens at most once per frame_serial.
// Links: rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"

#include <stdlib.h>
#include <string.h>

/// @brief Ensure the motion-history entry array can hold @p needed entries (grows geometrically).
static int ensure_motion_history_capacity(rt_canvas3d *c, int32_t needed) {
    if (!c || needed <= 0)
        return 0;
    if (c->motion_history_capacity >= needed)
        return 1;

    int32_t new_cap = c->motion_history_capacity > 0 ? c->motion_history_capacity : 32;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            new_cap = needed;
        else
            new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(canvas_motion_history_t))
        return 0;

    canvas_motion_history_t *new_hist = (canvas_motion_history_t *)realloc(
        c->motion_history, (size_t)new_cap * sizeof(canvas_motion_history_t));
    if (!new_hist)
        return 0;
    c->motion_history = new_hist;
    c->motion_history_capacity = new_cap;
    return 1;
}

/// @brief Clear the motion-history hash table (all slots back to the empty sentinel 0).
static void canvas3d_motion_hash_reset(rt_canvas3d *c) {
    if (!c || !c->motion_history_hash || c->motion_history_hash_capacity <= 0)
        return;
    memset(c->motion_history_hash,
           0,
           (size_t)c->motion_history_hash_capacity * sizeof(*c->motion_history_hash));
}

/// @brief Ensure the motion-history hash has a power-of-two capacity sized for @p count_hint
/// entries.
/// @details Targets ~2x load headroom (min 32) and resets the table on growth.
static int canvas3d_ensure_motion_hash_capacity(rt_canvas3d *c, int32_t count_hint) {
    if (!c)
        return 0;
    int32_t needed = canvas3d_next_power_of_two_i32(count_hint > 0 ? count_hint * 2 : 32);
    if (needed < 32)
        needed = 32;
    if (c->motion_history_hash_capacity >= needed)
        return 1;
    if ((size_t)needed > SIZE_MAX / sizeof(*c->motion_history_hash))
        return 0;
    int32_t *grown = (int32_t *)realloc(c->motion_history_hash, (size_t)needed * sizeof(*grown));
    if (!grown)
        return 0;
    c->motion_history_hash = grown;
    c->motion_history_hash_capacity = needed;
    canvas3d_motion_hash_reset(c);
    return 1;
}

/// @brief Insert history entry @p index into the hash by linear probing (slot stores index+1).
/// @return 1 on success, 0 if the table is full or inputs are invalid.
static int canvas3d_motion_hash_insert_existing(rt_canvas3d *c, int32_t index) {
    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    if (!c || !c->motion_history_hash || !hist || index < 0 || index >= c->motion_history_count)
        return 0;
    int32_t mask = c->motion_history_hash_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64(hist[index].key) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->motion_history_hash_capacity; ++probe) {
        if (c->motion_history_hash[slot] == 0) {
            c->motion_history_hash[slot] = index + 1;
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    return 0;
}

/// @brief Rebuild the motion-history hash from scratch over the current history array.
/// @details Sizes the table to the entry count, clears it, and re-inserts every entry.
static int canvas3d_rebuild_motion_hash(rt_canvas3d *c) {
    if (!c)
        return 0;
    if (c->motion_history_count <= 0) {
        canvas3d_motion_hash_reset(c);
        return 1;
    }
    if (!canvas3d_ensure_motion_hash_capacity(c, c->motion_history_count + 1))
        return 0;
    canvas3d_motion_hash_reset(c);
    for (int32_t i = 0; i < c->motion_history_count; ++i) {
        if (!canvas3d_motion_hash_insert_existing(c, i))
            return 0;
    }
    return 1;
}

/// @brief Look up the motion-history index for @p key, rebuilding the hash if it is
/// stale/undersized.
/// @return The history index, or -1 if the key is absent.
static int32_t canvas3d_motion_hash_find_index(rt_canvas3d *c, uintptr_t key) {
    if (!c || key == 0 || c->motion_history_count <= 0)
        return -1;
    if (!c->motion_history_hash ||
        c->motion_history_hash_capacity <
            canvas3d_next_power_of_two_i32(c->motion_history_count * 2)) {
        if (!canvas3d_rebuild_motion_hash(c))
            return -1;
    }
    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t mask = c->motion_history_hash_capacity - 1;
    int32_t slot = (int32_t)(canvas3d_hash_u64(key) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->motion_history_hash_capacity; ++probe) {
        int32_t encoded = c->motion_history_hash[slot];
        if (encoded == 0)
            return -1;
        int32_t index = encoded - 1;
        if (index >= 0 && index < c->motion_history_count && hist[index].key == key)
            return index;
        slot = (slot + 1) & mask;
    }
    return -1;
}

/// @brief Drop motion-history entries that haven't been touched in over a frame.
///
/// In-place compaction. Anything not seen in the current or previous
/// frame is considered stale (the mesh has stopped being drawn or
/// has been destroyed). Bounded eviction prevents the table from
/// growing without bound.
void canvas3d_prune_motion_history(rt_canvas3d *c) {
    if (!c || c->motion_history_count <= 0)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t dst = 0;
    for (int32_t i = 0; i < c->motion_history_count; i++) {
        if (c->frame_serial - hist[i].last_frame_seen > 1)
            continue;
        if (dst != i)
            hist[dst] = hist[i];
        dst++;
    }
    c->motion_history_count = dst;
    canvas3d_rebuild_motion_hash(c);
}

/// @brief Look up (and update) the previous-frame model matrix for a mesh.
///
/// Three cases:
///   1. Existing entry, first lookup this frame → roll current→previous,
///      update current, return previous.
///   2. Existing entry, repeat lookup this frame → just return the
///      previous (don't roll twice).
///   3. New entry → register, return "no previous yet".
/// Returns through `out_has_prev` whether the previous frame was
/// available — first-frame draws fall back to current=previous.
void canvas3d_resolve_previous_model(rt_canvas3d *c,
                                     uintptr_t motion_key,
                                     const float *current_model,
                                     float *out_prev_model,
                                     int8_t *out_has_prev) {
    if (out_has_prev)
        *out_has_prev = 0;
    if (out_prev_model)
        memset(out_prev_model, 0, sizeof(float) * 16);
    if (!c || motion_key == 0 || !current_model || !out_prev_model || !out_has_prev)
        return;

    canvas_motion_history_t *hist = (canvas_motion_history_t *)c->motion_history;
    int32_t found_index = canvas3d_motion_hash_find_index(c, motion_key);
    if (found_index >= 0) {
        canvas_motion_history_t *entry = &hist[found_index];
        if (entry->last_frame_seen != c->frame_serial) {
            if (entry->has_current) {
                memcpy(entry->prev_model, entry->current_model, sizeof(entry->prev_model));
                entry->has_prev = 1;
            }
            memcpy(entry->current_model, current_model, sizeof(entry->current_model));
            entry->has_current = 1;
            entry->last_frame_seen = c->frame_serial;
        }

        if (entry->has_prev) {
            memcpy(out_prev_model, entry->prev_model, sizeof(entry->prev_model));
            *out_has_prev = 1;
        }
        return;
    }

    if (!ensure_motion_history_capacity(c, c->motion_history_count + 1))
        return;
    if (!canvas3d_ensure_motion_hash_capacity(c, c->motion_history_count + 1))
        return;

    hist = (canvas_motion_history_t *)c->motion_history;
    int32_t new_index = c->motion_history_count++;
    canvas_motion_history_t *entry = &hist[new_index];
    memset(entry, 0, sizeof(*entry));
    entry->key = motion_key;
    memcpy(entry->current_model, current_model, sizeof(entry->current_model));
    entry->has_current = 1;
    entry->last_frame_seen = c->frame_serial;
    canvas3d_motion_hash_insert_existing(c, new_index);
}

/// @brief Mix one pointer/value into a running motion-history hash key (boost-style
///   hash_combine with the golden-ratio constant) so per-object motion vectors stay stable.
static uintptr_t canvas3d_mix_motion_key(uintptr_t key, uintptr_t value) {
    key ^= value + (uintptr_t)0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key;
}

/// @brief Mix a pointer-sized key into a 32-bit hash for the motion-history table.
uint32_t canvas3d_hash_u64(uintptr_t value) {
    uint64_t x = (uint64_t)value;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return (uint32_t)x;
}

/// @brief Round @p value up to the next power of two (used to size the open-addressing hash table).
int32_t canvas3d_next_power_of_two_i32(int32_t value) {
    int32_t cap = 1;
    if (value <= 1)
        return 1;
    while (cap < value) {
        if (cap > INT32_MAX / 2)
            return value;
        cap <<= 1;
    }
    return cap;
}

/// @brief Derive a stable object draw key for transform-handle draw calls.
uintptr_t canvas3d_mesh_transform_motion_key(const void *mesh_obj,
                                             const void *material_obj,
                                             const void *transform_obj) {
    uintptr_t key = (uintptr_t)transform_obj;
    key = canvas3d_mix_motion_key(key, (uintptr_t)mesh_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)material_obj);
    return key ? key : (uintptr_t)1u;
}

/// @brief Derive a stable per-instance key for the motion-blur history table.
/// @details Includes the caller's batch buffer identity so two batches with
///          the same mesh/material/count do not alias one another's previous
///          transforms. Keeping the same matrix buffer across frames preserves
///          continuous history; a reallocated buffer safely starts fresh.
uintptr_t canvas3d_instance_motion_key(const void *mesh_obj,
                                       const void *material_obj,
                                       const void *batch_obj,
                                       int32_t instance_count,
                                       int32_t index) {
    uintptr_t key = (uintptr_t)mesh_obj;
    uintptr_t instance_key = (uintptr_t)((uint32_t)index + 1u);
    key = canvas3d_mix_motion_key(key, (uintptr_t)material_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)batch_obj);
    key = canvas3d_mix_motion_key(key, (uintptr_t)((uint32_t)instance_count + 1u));
    key ^= instance_key * (uintptr_t)0xc2b2ae35u;
    return key ? key : instance_key;
}

#endif /* VIPER_ENABLE_GRAPHICS */
