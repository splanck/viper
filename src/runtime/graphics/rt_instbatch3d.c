//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_instbatch3d.c
// Purpose: Instanced rendering batch — stores N transforms for one mesh+material.
//   Canvas3D.DrawInstanced culls per-instance and dispatches via backend.
//
// Key invariants:
//   - Transforms are float[16] row-major, stored contiguously.
//   - Software backend: loops N individual submit_draw calls.
//   - Mesh/material are retained by the batch because it stores them across frames.
//   - Three parallel transform buffers are kept: live transforms,
//     start-of-frame snapshot for motion vectors, and previous-frame transforms.
//
// Ownership/Lifetime:
//   - InstanceBatch3D is GC-managed; finalizer releases mesh, material,
//     and the three float-matrix buffers.
//   - Transient per-frame matrix copies are parked on the canvas's temp-buffer
//     queue and freed at end-of-frame.
//
// Links: rt_instbatch3d.h, rt_canvas3d.c, vgfx3d_backend.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_instbatch3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_mat4.h"
#include "vgfx3d_backend.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern double rt_mat4_get(void *m, int64_t r, int64_t c);
extern int rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

#define INST_INIT_CAP 64
#define INSTBATCH3D_FLOAT_ABS_MAX 3.40282346638528859812e38

typedef struct {
    void *vptr;
    void *mesh;              /* retained Mesh3D */
    void *material;          /* retained Material3D */
    float *transforms;       /* N * 16 floats */
    float *current_snapshot; /* current-frame snapshot for motion history */
    float *prev_transforms;  /* previous-frame transforms */
    int32_t instance_count;
    int32_t instance_capacity;
    int32_t motion_snapshot_count;
    int32_t prev_count;
    int64_t last_motion_frame;
    int8_t has_prev_snapshot;
} rt_instbatch3d;

/// @brief Compute the next geometric growth capacity for the instance buffer.
/// @details Starting capacity is INST_INIT_CAP; subsequent doublings are guarded
///   against INT32_MAX/2 overflow and against the resulting byte-count exceeding
///   SIZE_MAX so both signed-integer and size_t overflow are prevented.  Returns 1
///   on success and writes the new capacity into *out_capacity; returns 0 if the
///   current capacity is already too large to double safely.
static int instbatch_next_capacity(int32_t current, int32_t *out_capacity) {
    if (!out_capacity)
        return 0;
    if (current <= 0) {
        *out_capacity = INST_INIT_CAP;
        return 1;
    }
    if (current > INT32_MAX / 2)
        return 0;
    int32_t next = current * 2;
    if ((size_t)next > SIZE_MAX / (16u * sizeof(float)))
        return 0;
    *out_capacity = next;
    return 1;
}

/// @brief Return the (row, col) element of a 4x4 identity matrix.
/// @details Used as a NaN/Inf fallback value when sanitizing incoming Mat4
///   transforms — replacing bad floats with the identity element preserves the
///   matrix structure and avoids propagating undefined GPU state.
static float instbatch_identity_at(int row, int col) {
    return row == col ? 1.0f : 0.0f;
}

static int instbatch_value_fits_float(double value) {
    return isfinite(value) && value >= -INSTBATCH3D_FLOAT_ABS_MAX &&
           value <= INSTBATCH3D_FLOAT_ABS_MAX;
}

/// @brief Copy a Mat4 object into a float[16] slot, replacing any NaN/Inf with
///   the corresponding identity-matrix element.
/// @details rt_mat4_get returns double, so the per-element isfinite check and
///   narrowing cast to float both happen here.  Replacing non-finite values
///   with the identity element keeps the matrix structurally valid and prevents
///   GPU pipeline exceptions that would otherwise silently corrupt a frame.
static void instbatch_copy_mat4_sanitized(float *dst, void *transform) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            double value = rt_mat4_get(transform, i, j);
            dst[i * 4 + j] =
                instbatch_value_fits_float(value) ? (float)value : instbatch_identity_at(i, j);
        }
    }
}

/// @brief Copy one 4x4 matrix between stride-16-float slots.
/// @details Instance batches store transforms in a flat float array where
///   slot `i` occupies `[i*16, i*16+16)`. This helper encapsulates the
///   stride math so callers don't repeat it across the file, and folds in
///   the null / negative-index guards that the motion-snapshot path
///   depends on.
static void instbatch_copy_matrix_slot(float *dst,
                                       int32_t dst_idx,
                                       const float *src,
                                       int32_t src_idx) {
    if (!dst || !src || dst_idx < 0 || src_idx < 0)
        return;
    memcpy(&dst[(size_t)dst_idx * 16u], &src[(size_t)src_idx * 16u], 16u * sizeof(float));
}

/// @brief Drop a retained reference from a slot and clear it.
/// @details Paired helper for the batch's mesh / material / user-data slots — the
///   instance-batch owns refs to these, so finalize must release them. Idempotent on
///   already-null slots so a partially-initialized batch can be torn down safely.
static void instbatch_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Per-instance frustum cull test for an instanced batch.
/// @details Transforms the mesh's local AABB by one instance's model matrix
///   into world space, then runs the standard p-vertex/n-vertex frustum
///   test. Returns visible (1) when the frustum pointer is null so disabling
///   culling at a higher level (e.g. shadow pass without a camera frustum)
///   doesn't accidentally hide every instance. The matrix is promoted to
///   double precision for the transform because the AABB refit can amplify
///   rounding at large world coordinates.
/// @return 1 if the instance's world AABB is on-screen or intersecting, 0 if
///   definitively outside the frustum.
static int instbatch_instance_visible(const vgfx3d_frustum_t *frustum,
                                      const float mesh_min[3],
                                      const float mesh_max[3],
                                      const float *model_matrix) {
    double world_matrix[16];
    float world_min[3];
    float world_max[3];

    if (!frustum || !mesh_min || !mesh_max || !model_matrix)
        return 1;

    for (int i = 0; i < 16; i++)
        world_matrix[i] = (double)model_matrix[i];
    vgfx3d_transform_aabb(mesh_min, mesh_max, world_matrix, world_min, world_max);
    return vgfx3d_frustum_test_aabb(frustum, world_min, world_max) != 0;
}

/// @brief GC finalizer — release the current-frame and motion-history buffers.
/// @details Instance batches keep three parallel float-matrix arrays: the
///   live `transforms` set, the start-of-frame `current_snapshot` captured
///   by motion-vector logic, and last frame's `prev_transforms` used as
///   the motion "from" pose. All three are plain float heap allocations
///   with no downstream refs to release. Counters are zeroed post-free so
///   a lingering post-finalize read sees an empty batch rather than
///   capacity-matches-missing-buffer.
static void instbatch_finalizer(void *obj) {
    rt_instbatch3d *b = (rt_instbatch3d *)obj;
    free(b->transforms);
    free(b->current_snapshot);
    free(b->prev_transforms);
    b->transforms = NULL;
    b->current_snapshot = NULL;
    b->prev_transforms = NULL;
    b->instance_count = b->instance_capacity = 0;
    b->motion_snapshot_count = b->prev_count = 0;
    b->last_motion_frame = 0;
    b->has_prev_snapshot = 0;
    instbatch_release_ref(&b->mesh);
    instbatch_release_ref(&b->material);
}

/// @brief Create an instance batch for drawing N copies of one mesh efficiently.
/// @details Instance batching draws many objects with the same mesh and material
///          but different transforms in fewer draw calls. The software backend
///          falls back to individual draws; GPU backends may use native instancing.
///          Transforms are stored as contiguous float[16*N] row-major Mat4 arrays.
/// @param mesh     Mesh handle shared by all instances. The batch retains it
///                 because it is reused across frames.
/// @param material Material handle shared by all instances. The batch retains
///                 it because it is reused across frames.
/// @return Opaque batch handle, or NULL on failure.
void *rt_instbatch3d_new(void *mesh, void *material) {
    mesh = rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    material = rt_g3d_checked_or_null(material, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!mesh || !material)
        return NULL;
    rt_instbatch3d *b = (rt_instbatch3d *)rt_obj_new_i64(RT_G3D_INSTANCEBATCH3D_CLASS_ID, (int64_t)sizeof(rt_instbatch3d));
    if (!b) {
        rt_trap("InstanceBatch3D.New: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->mesh = mesh;
    b->material = material;
    rt_obj_retain_maybe(mesh);
    rt_obj_retain_maybe(material);
    b->transforms = (float *)calloc(INST_INIT_CAP * 16, sizeof(float));
    b->current_snapshot = (float *)calloc(INST_INIT_CAP * 16, sizeof(float));
    b->prev_transforms = (float *)calloc(INST_INIT_CAP * 16, sizeof(float));
    b->instance_count = 0;
    b->instance_capacity = INST_INIT_CAP;
    b->motion_snapshot_count = 0;
    b->prev_count = 0;
    b->last_motion_frame = 0;
    b->has_prev_snapshot = 0;
    if (!b->transforms || !b->current_snapshot || !b->prev_transforms) {
        instbatch_finalizer(b);
        if (rt_obj_release_check0(b))
            rt_obj_free(b);
        rt_trap("InstanceBatch3D.New: allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(b, instbatch_finalizer);
    return b;
}

/// @brief Add an instance with the given transform (grows array if full).
/// @details Copies the Mat4 (double) into the float[16] transform buffer.
///          The array uses geometric growth to amortize allocation cost.
void rt_instbatch3d_add(void *obj, void *transform) {
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    if (!b || !transform || rt_obj_class_id(transform) != RT_MAT4_CLASS_ID)
        return;

    if (b->instance_count >= b->instance_capacity) {
        int32_t new_cap;
        if (!instbatch_next_capacity(b->instance_capacity, &new_cap)) {
            rt_trap("InstanceBatch3D.Add: instance capacity overflow");
            return;
        }
        size_t old_bytes = (size_t)b->instance_capacity * 16u * sizeof(float);
        float *new_transforms = (float *)calloc((size_t)new_cap * 16u, sizeof(float));
        float *new_snapshot = (float *)calloc((size_t)new_cap * 16u, sizeof(float));
        float *new_prev = (float *)calloc((size_t)new_cap * 16u, sizeof(float));
        if (!new_transforms || !new_snapshot || !new_prev) {
            free(new_transforms);
            free(new_snapshot);
            free(new_prev);
            return;
        }
        if (old_bytes > 0) {
            memcpy(new_transforms, b->transforms, old_bytes);
            memcpy(new_snapshot, b->current_snapshot, old_bytes);
            memcpy(new_prev, b->prev_transforms, old_bytes);
        }
        free(b->transforms);
        free(b->current_snapshot);
        free(b->prev_transforms);
        b->transforms = new_transforms;
        b->current_snapshot = new_snapshot;
        b->prev_transforms = new_prev;
        b->instance_capacity = new_cap;
    }

    float *dst = &b->transforms[(size_t)b->instance_count * 16u];
    instbatch_copy_mat4_sanitized(dst, transform);

    b->instance_count++;
}

/// @brief Remove an instance by index (swap-removes with last for O(1) time).
void rt_instbatch3d_remove(void *obj, int64_t index) {
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    if (!b)
        return;
    int32_t last_idx;
    if (index < 0 || index >= b->instance_count)
        return;
    last_idx = b->instance_count - 1;

    /* Swap with last */
    if (index < last_idx) {
        instbatch_copy_matrix_slot(b->transforms, (int32_t)index, b->transforms, last_idx);
        if (last_idx < b->motion_snapshot_count)
            instbatch_copy_matrix_slot(
                b->current_snapshot, (int32_t)index, b->current_snapshot, last_idx);
        else if (index < b->motion_snapshot_count)
            b->motion_snapshot_count = (int32_t)index;
        if (last_idx < b->prev_count)
            instbatch_copy_matrix_slot(
                b->prev_transforms, (int32_t)index, b->prev_transforms, last_idx);
        else if (index < b->prev_count) {
            b->prev_count = (int32_t)index;
            b->has_prev_snapshot = b->prev_count > 0 ? 1 : 0;
        }
    }
    b->instance_count--;
    if (b->motion_snapshot_count > b->instance_count)
        b->motion_snapshot_count = b->instance_count;
    if (b->prev_count > b->instance_count)
        b->prev_count = b->instance_count;
}

/// @brief Update the transform of an existing instance at the given index.
void rt_instbatch3d_set(void *obj, int64_t index, void *transform) {
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    if (!b || !transform || rt_obj_class_id(transform) != RT_MAT4_CLASS_ID)
        return;
    if (index < 0 || index >= b->instance_count)
        return;

    float *dst = &b->transforms[index * 16];
    instbatch_copy_mat4_sanitized(dst, transform);
}

/// @brief Remove all instances from the batch, resetting count to zero.
void rt_instbatch3d_clear(void *obj) {
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    if (!b)
        return;
    b->instance_count = 0;
    b->motion_snapshot_count = 0;
    b->prev_count = 0;
    b->has_prev_snapshot = 0;
}

/// @brief Get the current number of instances in the batch.
int64_t rt_instbatch3d_count(void *obj) {
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    return b ? b->instance_count : 0;
}

/// @brief Draw all visible instances. Falls back to N individual draw calls
/// since the software backend doesn't have a native instanced path.
void rt_canvas3d_draw_instanced(void *canvas_obj, void *batch_obj) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    rt_instbatch3d *b =
        (rt_instbatch3d *)rt_g3d_checked_or_null(batch_obj, RT_G3D_INSTANCEBATCH3D_CLASS_ID);
    if (!c || !b)
        return;
    if (!c->in_frame || !c->backend || b->instance_count == 0)
        return;
    if ((size_t)b->instance_count > SIZE_MAX / (16u * sizeof(float))) {
        rt_trap("InstanceBatch3D.Draw: instance matrix allocation overflow");
        return;
    }

    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(b->mesh, RT_G3D_MESH3D_CLASS_ID);
    if (!mesh || mesh->vertex_count == 0 || mesh->index_count == 0)
        return;
    rt_mesh3d_refresh_bounds(mesh);

    float mesh_min[3] = {mesh->aabb_min[0], mesh->aabb_min[1], mesh->aabb_min[2]};
    float mesh_max[3] = {mesh->aabb_max[0], mesh->aabb_max[1], mesh->aabb_max[2]};
    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, c->cached_vp);

    /* Build draw command from batch mesh/material */
    rt_material3d *mat =
        (rt_material3d *)rt_g3d_checked_or_null(b->material, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!mat)
        return;

    if (b->last_motion_frame != rt_canvas3d_get_frame_serial(canvas_obj)) {
        if (b->motion_snapshot_count > 0) {
            memcpy(b->prev_transforms,
                   b->current_snapshot,
                   (size_t)b->motion_snapshot_count * 16 * sizeof(float));
            b->prev_count = b->motion_snapshot_count;
            b->has_prev_snapshot = 1;
        }
        memcpy(b->current_snapshot, b->transforms, (size_t)b->instance_count * 16u * sizeof(float));
        b->motion_snapshot_count = b->instance_count;
        b->last_motion_frame = rt_canvas3d_get_frame_serial(canvas_obj);
    }

    {
        const float *submit_transforms = b->transforms;
        float *owned_prev = NULL;
        const float *submit_prev = NULL;
        int8_t has_prev = 0;
        int32_t submit_count = b->instance_count;
        if (b->has_prev_snapshot && b->prev_count > 0) {
            if (b->prev_count == b->instance_count) {
                submit_prev = b->prev_transforms;
                has_prev = 1;
            } else {
                owned_prev = (float *)malloc((size_t)b->instance_count * 16u * sizeof(float));
                if (owned_prev) {
                    int32_t preserved = b->prev_count < b->instance_count ? b->prev_count : b->instance_count;
                    if (preserved > 0) {
                        memcpy(owned_prev,
                               b->prev_transforms,
                               (size_t)preserved * 16u * sizeof(float));
                    }
                    if (preserved < b->instance_count) {
                        memcpy(&owned_prev[(size_t)preserved * 16u],
                               &b->transforms[(size_t)preserved * 16u],
                               (size_t)(b->instance_count - preserved) * 16u * sizeof(float));
                    }
                    submit_prev = owned_prev;
                    has_prev = 1;
                }
            }
        }
        if (mesh->bsphere_radius > 0.0f && b->instance_count > 0) {
            float *visible_transforms =
                (float *)malloc((size_t)b->instance_count * 16u * sizeof(float));
            float *visible_prev =
                has_prev ? (float *)malloc((size_t)b->instance_count * 16u * sizeof(float)) : NULL;
            if (visible_transforms && (!has_prev || visible_prev)) {
                int32_t visible_count = 0;
                for (int32_t i = 0; i < b->instance_count; i++) {
                    const float *src = &b->transforms[(size_t)i * 16u];
                    if (!instbatch_instance_visible(&frustum, mesh_min, mesh_max, src))
                        continue;
                    memcpy(
                        &visible_transforms[(size_t)visible_count * 16u], src, 16u * sizeof(float));
                    if (visible_prev) {
                        memcpy(&visible_prev[(size_t)visible_count * 16u],
                               &submit_prev[(size_t)i * 16u],
                               16u * sizeof(float));
                    }
                    visible_count++;
                }
                if (visible_count == 0) {
                    free(visible_transforms);
                    free(visible_prev);
                    free(owned_prev);
                    return;
                }
                if (visible_count < b->instance_count) {
                    int visible_tracked = rt_canvas3d_add_temp_buffer(canvas_obj, visible_transforms);
                    int prev_tracked =
                        visible_prev ? rt_canvas3d_add_temp_buffer(canvas_obj, visible_prev) : 1;
                    if (!visible_tracked || !prev_tracked) {
                        if (!visible_tracked)
                            free(visible_transforms);
                        if (!prev_tracked)
                            free(visible_prev);
                        free(owned_prev);
                        return;
                    }
                    free(owned_prev);
                    submit_transforms = visible_transforms;
                    submit_prev = visible_prev;
                    submit_count = visible_count;
                    has_prev = visible_prev ? 1 : 0;
                } else {
                    free(visible_transforms);
                    free(visible_prev);
                }
            } else {
                free(visible_transforms);
                free(visible_prev);
            }
        }
        if (owned_prev && submit_prev == owned_prev)
            if (!rt_canvas3d_add_temp_buffer(canvas_obj, owned_prev)) {
                free(owned_prev);
                return;
            }
        rt_canvas3d_queue_instanced_batch(
            canvas_obj, mesh, mat, submit_transforms, submit_count, submit_prev, has_prev);
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
