//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_morphtarget3d.c
// Purpose: MorphTarget3D — named blend shapes with per-vertex position, normal,
//   and tangent deltas. Applied on GPU when the active backend can carry the
//   payload, otherwise on CPU before draw submission.
//
// Key invariants:
//   - Deltas are stored as 3 floats per vertex per shape (sparse via zero default).
//   - Morph application: dst = base + sum(weight[i] * delta[i]) per vertex.
//   - Normals/tangents re-normalized after morph if any shape has corresponding deltas.
//   - Temporary morphed vertex buffer registered with canvas temp_buffers
//     to avoid use-after-free with deferred draw queue.
//
// Links: rt_morphtarget3d.h, plans/3d/16-morph-targets.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_morphtarget3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_d3d11_shared.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[64];
    float *pos_deltas; /* 3 * vertex_count floats (dx, dy, dz per vertex) */
    float *nrm_deltas; /* 3 * vertex_count floats (or NULL) */
    float *tan_deltas; /* 3 * vertex_count floats (or NULL); tangent.w is preserved */
} vgfx3d_morph_shape_t;

typedef struct {
    void *vptr;
    vgfx3d_morph_shape_t *shapes;
    float *weights;
    float *prev_weights;
    float *motion_weight_snapshot;
    float *packed_pos_deltas;
    float *packed_nrm_deltas;
    uint64_t payload_generation;
    int32_t shape_count;
    int32_t shape_capacity;
    int32_t vertex_count;
    int64_t last_motion_frame;
    int8_t has_prev_weights;
    int8_t packed_dirty;
} rt_morphtarget3d;

/// @brief Flag the packed GPU payload dirty and bump the generation counter.
/// @details Called after any per-shape delta array mutation so downstream
///   consumers (GPU backends that cache the flat payload as a vertex/constant
///   buffer) know to re-upload on the next draw. The generation counter
///   wraps from UINT64_MAX back to 1 rather than 0 so "0" remains a reliable
///   "never seen" sentinel in caches that compare against the last-known value.
static void morphtarget_touch_payload(rt_morphtarget3d *mt) {
    if (!mt)
        return;
    mt->packed_dirty = 1;
    if (mt->payload_generation == UINT64_MAX)
        mt->payload_generation = 1;
    else
        mt->payload_generation++;
}

/// @brief Re-linearize the per-shape delta arrays into a single GPU-friendly buffer.
/// @details Blend shapes arrive from authoring tools as independent arrays
///   (one `pos_deltas` / `nrm_deltas` allocation per shape). GPU backends,
///   however, want a single flat `shape_count * vertex_count * 3` buffer
///   they can bind once. This routine concatenates them and replaces any
///   previously-built packed arrays. Normal deltas are optional and only
///   allocated when at least one shape supplies them, saving memory for
///   position-only morphs (blink, phoneme shapes, etc.).
/// @return 1 on success (or no-op when `packed_dirty == 0`), 0 on OOM — the
///   caller should skip the draw rather than render with stale deltas.
static int morphtarget_rebuild_packed_payload(rt_morphtarget3d *mt) {
    size_t delta_count;
    float *packed_pos = NULL;
    float *packed_nrm = NULL;
    int has_normal_deltas = 0;

    if (!mt)
        return 0;
    if (!mt->packed_dirty)
        return 1;

    delta_count = (size_t)mt->shape_count * (size_t)mt->vertex_count * 3u;
    if (delta_count > 0)
        packed_pos = (float *)calloc(delta_count, sizeof(float));
    for (int32_t s = 0; s < mt->shape_count; s++) {
        if (mt->shapes[s].nrm_deltas) {
            has_normal_deltas = 1;
            break;
        }
    }
    if (delta_count > 0 && has_normal_deltas)
        packed_nrm = (float *)calloc(delta_count, sizeof(float));
    if (delta_count > 0 && (!packed_pos || (has_normal_deltas && !packed_nrm))) {
        free(packed_pos);
        free(packed_nrm);
        return 0;
    }

    for (int32_t s = 0; s < mt->shape_count; s++) {
        size_t offset = (size_t)s * (size_t)mt->vertex_count * 3u;
        if (packed_pos && mt->shapes[s].pos_deltas) {
            memcpy(&packed_pos[offset],
                   mt->shapes[s].pos_deltas,
                   (size_t)mt->vertex_count * 3u * sizeof(float));
        }
        if (packed_nrm && mt->shapes[s].nrm_deltas) {
            memcpy(&packed_nrm[offset],
                   mt->shapes[s].nrm_deltas,
                   (size_t)mt->vertex_count * 3u * sizeof(float));
        }
    }

    free(mt->packed_pos_deltas);
    free(mt->packed_nrm_deltas);
    mt->packed_pos_deltas = packed_pos;
    mt->packed_nrm_deltas = packed_nrm;
    mt->packed_dirty = 0;
    return 1;
}

/// @brief Grow the parallel shape / weight / prev-weight / motion arrays.
/// @details All four arrays must remain index-aligned, so they're reallocated
///   together under a single transaction — failure to allocate any one of
///   them frees the rest before returning, preventing a torn state where
///   `shapes[]` has more entries than `weights[]`. Capacity doubles from an
///   initial 4 slots until `min_capacity` fits, with an INT32_MAX/2 guard
///   against integer overflow.
/// @return 1 if capacity >= min_capacity (possibly no-op), 0 on OOM.
static int morphtarget_reserve_shapes(rt_morphtarget3d *mt, int32_t min_capacity) {
    int32_t new_capacity;
    vgfx3d_morph_shape_t *new_shapes = NULL;
    float *new_weights = NULL;
    float *new_prev_weights = NULL;
    float *new_motion_snapshot = NULL;

    if (!mt)
        return 0;
    if (min_capacity <= mt->shape_capacity)
        return 1;

    new_capacity = mt->shape_capacity > 0 ? mt->shape_capacity : 4;
    while (new_capacity < min_capacity) {
        if (new_capacity > INT32_MAX / 2) {
            new_capacity = min_capacity;
            break;
        }
        new_capacity *= 2;
    }

    new_shapes = (vgfx3d_morph_shape_t *)calloc((size_t)new_capacity, sizeof(*new_shapes));
    new_weights = (float *)calloc((size_t)new_capacity, sizeof(*new_weights));
    new_prev_weights = (float *)calloc((size_t)new_capacity, sizeof(*new_prev_weights));
    new_motion_snapshot = (float *)calloc((size_t)new_capacity, sizeof(*new_motion_snapshot));
    if (!new_shapes || !new_weights || !new_prev_weights || !new_motion_snapshot) {
        free(new_shapes);
        free(new_weights);
        free(new_prev_weights);
        free(new_motion_snapshot);
        return 0;
    }

    if (mt->shape_count > 0) {
        memcpy(new_shapes, mt->shapes, (size_t)mt->shape_count * sizeof(*new_shapes));
        memcpy(new_weights, mt->weights, (size_t)mt->shape_count * sizeof(*new_weights));
        memcpy(new_prev_weights, mt->prev_weights, (size_t)mt->shape_count * sizeof(*new_prev_weights));
        memcpy(new_motion_snapshot,
               mt->motion_weight_snapshot,
               (size_t)mt->shape_count * sizeof(*new_motion_snapshot));
    }

    free(mt->shapes);
    free(mt->weights);
    free(mt->prev_weights);
    free(mt->motion_weight_snapshot);
    mt->shapes = new_shapes;
    mt->weights = new_weights;
    mt->prev_weights = new_prev_weights;
    mt->motion_weight_snapshot = new_motion_snapshot;
    mt->shape_capacity = new_capacity;
    return 1;
}

/// @brief Report the maximum morph shape count the named backend can handle on-GPU.
/// @details The limits reflect each backend's uniform/constant-buffer budget
///   for blend-shape coefficients: Metal can bind full buffers as arguments
///   (essentially unlimited), OpenGL's default uniform storage caps around
///   32 shapes, and D3D11 uses the hard-coded `VGFX3D_D3D11_MAX_MORPH_SHAPES`
///   tuned to fit alongside other per-frame constants. Unknown backends
///   (software renderer, disabled builds) return 0 so the caller falls
///   back to CPU deformation.
static int32_t vgfx3d_backend_morph_shape_limit(const char *backend_name) {
    if (!backend_name)
        return 0;
    if (strcmp(backend_name, "metal") == 0)
        return INT32_MAX;
    if (strcmp(backend_name, "opengl") == 0)
        return 32;
    if (strcmp(backend_name, "d3d11") == 0)
        return VGFX3D_D3D11_MAX_MORPH_SHAPES;
    return 0;
}

/// @brief Decide whether to dispatch GPU-side morph blending for this draw.
/// @details Any mesh whose `shape_count` exceeds the backend's shader payload
///   capacity must fall back to CPU blending instead, because partial GPU
///   evaluation would silently drop the overflow shapes. Checked per draw,
///   not once at mesh load, so a morph that starts small can stay on the
///   GPU path even if other morph assets on the same canvas need CPU blending.
/// @return Non-zero when GPU morph is viable.
static int vgfx3d_backend_prefers_gpu_morph(const char *backend_name, int32_t shape_count) {
    int32_t limit = vgfx3d_backend_morph_shape_limit(backend_name);
    return limit > 0 && shape_count <= limit;
}

/*==========================================================================
 * Lifecycle
 *=========================================================================*/

/// @brief GC finalizer — release every per-shape delta array and packed buffer.
/// @details Walks `shapes[0..shape_count]` first to drop their owned
///   `pos_deltas` / `nrm_deltas`, then releases the four index-aligned
///   top-level arrays plus the packed GPU payload allocations. Order matters
///   only for debuggability — nothing here has cross-allocation dependencies,
///   so OOM during any intermediate alloc elsewhere in the file is safe
///   because finalize is idempotent against null slots.
static void rt_morphtarget3d_finalize(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    for (int32_t i = 0; i < mt->shape_count; i++) {
        free(mt->shapes[i].pos_deltas);
        free(mt->shapes[i].nrm_deltas);
        free(mt->shapes[i].tan_deltas);
    }
    free(mt->shapes);
    free(mt->weights);
    free(mt->prev_weights);
    free(mt->motion_weight_snapshot);
    free(mt->packed_pos_deltas);
    free(mt->packed_nrm_deltas);
}

/// @brief Create a morph target container for blendshape animation.
/// @details Morph targets (aka blend shapes) deform a mesh by adding weighted
///          per-vertex deltas to the base positions and normals. Shapes grow on
///          demand, while GPU backends may still fall back to CPU deformation if
///          the active shape count exceeds the backend's shader payload limits.
/// @param vertex_count Number of vertices in the base mesh (must match mesh vertex count).
/// @return Opaque morph target handle, or NULL on failure.
void *rt_morphtarget3d_new(int64_t vertex_count) {
    if (vertex_count <= 0) {
        rt_trap("MorphTarget3D.New: vertex_count must be > 0");
        return NULL;
    }
    rt_morphtarget3d *mt = (rt_morphtarget3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_morphtarget3d));
    if (!mt) {
        rt_trap("MorphTarget3D.New: memory allocation failed");
        return NULL;
    }
    mt->vptr = NULL;
    mt->shapes = NULL;
    mt->weights = NULL;
    mt->prev_weights = NULL;
    mt->motion_weight_snapshot = NULL;
    mt->shape_count = 0;
    mt->shape_capacity = 0;
    mt->vertex_count = (int32_t)vertex_count;
    mt->last_motion_frame = 0;
    mt->has_prev_weights = 0;
    mt->packed_pos_deltas = NULL;
    mt->packed_nrm_deltas = NULL;
    mt->payload_generation = 1;
    mt->packed_dirty = 1;
    rt_obj_set_finalizer(mt, rt_morphtarget3d_finalize);
    return mt;
}

void *rt_morphtarget3d_clone(void *obj) {
    rt_morphtarget3d *src = (rt_morphtarget3d *)obj;
    rt_morphtarget3d *dst;
    if (!src)
        return NULL;
    dst = (rt_morphtarget3d *)rt_morphtarget3d_new(src->vertex_count);
    if (!dst)
        return NULL;
    for (int32_t i = 0; i < src->shape_count; i++) {
        int64_t shape = rt_morphtarget3d_add_shape(dst, rt_const_cstr(src->shapes[i].name));
        if (shape < 0)
            continue;
        if (src->shapes[i].pos_deltas && dst->shapes[shape].pos_deltas) {
            memcpy(dst->shapes[shape].pos_deltas,
                   src->shapes[i].pos_deltas,
                   (size_t)src->vertex_count * 3u * sizeof(float));
        }
        if (src->shapes[i].nrm_deltas) {
            dst->shapes[shape].nrm_deltas =
                (float *)calloc((size_t)src->vertex_count * 3u, sizeof(float));
            if (dst->shapes[shape].nrm_deltas) {
                memcpy(dst->shapes[shape].nrm_deltas,
                       src->shapes[i].nrm_deltas,
                       (size_t)src->vertex_count * 3u * sizeof(float));
            }
        }
        if (src->shapes[i].tan_deltas) {
            dst->shapes[shape].tan_deltas =
                (float *)calloc((size_t)src->vertex_count * 3u, sizeof(float));
            if (dst->shapes[shape].tan_deltas) {
                memcpy(dst->shapes[shape].tan_deltas,
                       src->shapes[i].tan_deltas,
                       (size_t)src->vertex_count * 3u * sizeof(float));
            }
        }
        dst->weights[shape] = src->weights ? src->weights[i] : 0.0f;
        dst->prev_weights[shape] = src->prev_weights ? src->prev_weights[i] : 0.0f;
        dst->motion_weight_snapshot[shape] =
            src->motion_weight_snapshot ? src->motion_weight_snapshot[i] : dst->weights[shape];
    }
    dst->has_prev_weights = src->has_prev_weights;
    dst->last_motion_frame = src->last_motion_frame;
    morphtarget_touch_payload(dst);
    return dst;
}

/*==========================================================================
 * Shape management
 *=========================================================================*/

/// @brief Register a named blend shape and allocate its per-vertex delta arrays.
int64_t rt_morphtarget3d_add_shape(void *obj, rt_string name) {
    if (!obj)
        return -1;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!morphtarget_reserve_shapes(mt, mt->shape_count + 1)) {
        rt_trap("MorphTarget3D.AddShape: memory allocation failed");
        return -1;
    }

    vgfx3d_morph_shape_t *shape = &mt->shapes[mt->shape_count];
    memset(shape, 0, sizeof(vgfx3d_morph_shape_t));

    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(shape->name, cstr, len);
            shape->name[len] = '\0';
        }
    }

    size_t delta_size = (size_t)mt->vertex_count * 3 * sizeof(float);
    shape->pos_deltas = (float *)calloc(1, delta_size);
    if (!shape->pos_deltas)
        return -1;
    /* Normal deltas allocated on first use (SetNormalDelta) */

    morphtarget_touch_payload(mt);
    return mt->shape_count++;
}

/// @brief Set the position delta for a single vertex of a single shape.
/// Out-of-range shape or vertex indices are silent no-ops; touches the payload
/// generation so GPU caches re-upload on the next draw.
void rt_morphtarget3d_set_delta(
    void *obj, int64_t shape, int64_t vertex, double dx, double dy, double dz) {
    if (!obj)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return;
    if (vertex < 0 || vertex >= mt->vertex_count)
        return;

    float *pd = mt->shapes[shape].pos_deltas;
    if (!pd)
        return;
    pd[vertex * 3 + 0] = (float)dx;
    pd[vertex * 3 + 1] = (float)dy;
    pd[vertex * 3 + 2] = (float)dz;
    morphtarget_touch_payload(mt);
}

/// @brief Set the normal delta for a single vertex of a single shape.
/// Lazy-allocates the per-shape normal delta array on first use to keep
/// position-only blendshapes memory-efficient. Triggers post-morph re-normalization.
void rt_morphtarget3d_set_normal_delta(
    void *obj, int64_t shape, int64_t vertex, double dx, double dy, double dz) {
    if (!obj)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return;
    if (vertex < 0 || vertex >= mt->vertex_count)
        return;

    /* Lazy-allocate normal deltas on first use */
    if (!mt->shapes[shape].nrm_deltas) {
        size_t sz = (size_t)mt->vertex_count * 3 * sizeof(float);
        mt->shapes[shape].nrm_deltas = (float *)calloc(1, sz);
        if (!mt->shapes[shape].nrm_deltas)
            return;
    }

    float *nd = mt->shapes[shape].nrm_deltas;
    nd[vertex * 3 + 0] = (float)dx;
    nd[vertex * 3 + 1] = (float)dy;
    nd[vertex * 3 + 2] = (float)dz;
    morphtarget_touch_payload(mt);
}

/// @brief Set the tangent delta for a single vertex of a single shape.
/// Lazy-allocates tangent deltas on first use. Tangent handedness (w) is not
/// morphed; CPU fallback preserves the base tangent sign and renormalizes xyz.
void rt_morphtarget3d_set_tangent_delta(
    void *obj, int64_t shape, int64_t vertex, double dx, double dy, double dz) {
    if (!obj)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return;
    if (vertex < 0 || vertex >= mt->vertex_count)
        return;

    if (!mt->shapes[shape].tan_deltas) {
        size_t sz = (size_t)mt->vertex_count * 3 * sizeof(float);
        mt->shapes[shape].tan_deltas = (float *)calloc(1, sz);
        if (!mt->shapes[shape].tan_deltas)
            return;
    }

    float *td = mt->shapes[shape].tan_deltas;
    td[vertex * 3 + 0] = (float)dx;
    td[vertex * 3 + 1] = (float)dy;
    td[vertex * 3 + 2] = (float)dz;
    morphtarget_touch_payload(mt);
}

/*==========================================================================
 * Weight control
 *=========================================================================*/

/// @brief Set the blend weight for a shape by index (0.0 = off, 1.0 = full).
///
/// Clamped to [-1, 1]. Values outside that range silently over-extruded
/// vertices past the target mesh and produced z-fighting / self-intersection
/// in extreme cases. Negative weights are kept (they invert the morph delta)
/// but bounded so the combined deformation stays well-defined.
void rt_morphtarget3d_set_weight(void *obj, int64_t shape, double weight) {
    if (!obj)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return;
    if (weight < -1.0)
        weight = -1.0;
    else if (weight > 1.0)
        weight = 1.0;
    mt->weights[shape] = (float)weight;
}

/// @brief Get the current blend weight for a shape by index.
double rt_morphtarget3d_get_weight(void *obj, int64_t shape) {
    if (!obj)
        return 0.0;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return 0.0;
    return mt->weights[shape];
}

/// @brief Set the blend weight for a shape by its name (string lookup).
void rt_morphtarget3d_set_weight_by_name(void *obj, rt_string name, double weight) {
    if (!obj || !name)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    const char *target = rt_string_cstr(name);
    if (!target)
        return;
    for (int32_t i = 0; i < mt->shape_count; i++) {
        if (strcmp(mt->shapes[i].name, target) == 0) {
            rt_morphtarget3d_set_weight(mt, i, weight);
            return;
        }
    }
}

/// @brief Get the number of registered blend shapes.
int64_t rt_morphtarget3d_get_shape_count(void *obj) {
    return obj ? ((rt_morphtarget3d *)obj)->shape_count : 0;
}

/// @brief Borrow the contiguous packed position-delta payload for GPU upload.
/// Layout: shape-major, [shape][vertex][xyz]. Rebuilds on demand if dirty.
/// Returns NULL if the morph target is empty or rebuild fails (OOM).
const float *rt_morphtarget3d_get_packed_deltas(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!mt)
        return NULL;
    if (!morphtarget_rebuild_packed_payload(mt))
        return NULL;
    return mt->packed_pos_deltas;
}

/// @brief Borrow the packed normal-delta payload for GPU upload, or NULL when no
/// shape has normal deltas. Same layout as `_get_packed_deltas`.
const float *rt_morphtarget3d_get_packed_normal_deltas(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!mt)
        return NULL;
    if (!morphtarget_rebuild_packed_payload(mt))
        return NULL;
    return mt->packed_nrm_deltas;
}

int64_t rt_morphtarget3d_has_tangent_deltas(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!mt)
        return 0;
    for (int32_t i = 0; i < mt->shape_count; i++) {
        if (mt->shapes[i].tan_deltas)
            return 1;
    }
    return 0;
}

/// @brief Monotonic counter that bumps whenever any delta changes.
/// GPU caches compare against the previous value to detect when re-upload is required.
uint64_t rt_morphtarget3d_get_payload_generation(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!mt)
        return 0;
    return mt->payload_generation;
}

/// @brief Advance the previous-weight history by one frame for motion vectors.
/// @details Keyed on `frame_serial` so multiple draws within the same frame
///   don't rotate history more than once: the first draw of a new frame
///   copies the snapshot from the previous frame into `prev_weights` (so
///   motion-vector shaders can see the delta) and then snapshots the
///   current weights for next frame. `has_prev_weights` stays 0 on the
///   first-ever frame so motion shaders don't blend against zero noise.
/// @return The previous-frame weights for motion-vector use, or NULL when
///   no history exists yet.
static const float *morphtarget_prepare_prev_weights(rt_morphtarget3d *mt, int64_t frame_serial) {
    if (!mt)
        return NULL;
    if (mt->last_motion_frame != frame_serial) {
        size_t weight_bytes = (size_t)mt->shape_count * sizeof(float);
        if (mt->last_motion_frame != 0 && weight_bytes > 0) {
            memcpy(mt->prev_weights, mt->motion_weight_snapshot, weight_bytes);
            mt->has_prev_weights = 1;
        }
        if (weight_bytes > 0)
            memcpy(mt->motion_weight_snapshot, mt->weights, weight_bytes);
        mt->last_motion_frame = frame_serial;
    }
    return mt->has_prev_weights ? mt->prev_weights : NULL;
}

/*==========================================================================
 * Mesh integration
 *=========================================================================*/

/// @brief Bind a MorphTarget3D to a Mesh3D so subsequent draws apply blendshapes.
/// Vertex counts must match exactly; mismatches are silently rejected. Pass NULL
/// in @p morph_targets to detach.
void rt_mesh3d_set_morph_targets(void *mesh, void *morph_targets) {
    if (!mesh)
        return;
    rt_mesh3d *m = (rt_mesh3d *)mesh;
    if (!morph_targets) {
        if (m->morph_targets_ref && rt_obj_release_check0(m->morph_targets_ref))
            rt_obj_free(m->morph_targets_ref);
        m->morph_targets_ref = NULL;
        return;
    }
    rt_morphtarget3d *mt = (rt_morphtarget3d *)morph_targets;
    if ((int32_t)m->vertex_count != mt->vertex_count)
        return;
    if (m->morph_targets_ref == morph_targets)
        return;
    rt_obj_retain_maybe(morph_targets);
    if (m->morph_targets_ref && rt_obj_release_check0(m->morph_targets_ref))
        rt_obj_free(m->morph_targets_ref);
    m->morph_targets_ref = morph_targets;
}

/*==========================================================================
 * CPU morph application + drawing
 *=========================================================================*/

/// @brief Submit a mesh draw with morph-target blend applied on GPU or CPU.
/// @details Picks the evaluation path by asking the backend
///   (`vgfx3d_backend_prefers_gpu_morph`). GPU path: shallow-stores the
///   existing morph fields on the mesh, points them at the packed payload
///   for this draw, invokes the normal `draw_mesh_matrix_keyed`, then
///   restores — this pattern avoids mutating the persistent mesh state
///   across a draw boundary and sidesteps adding a second draw overload
///   for each backend. CPU path: allocates a scratch vertex buffer,
///   accumulates weighted position plus optional normal/tangent deltas per
///   vertex, re-normalizes affected directions when any shape contributed them,
///   and tracks the buffer for end-of-frame cleanup. Small weights
///   (|w| < 1e-6) are skipped to avoid unnecessary fp math when a shape
///   is effectively dormant.
static void morphtarget_draw_mesh_matrix(void *canvas,
                                         void *mesh,
                                         const double *model_matrix,
                                         void *material,
                                         const void *motion_key,
                                         void *morph_targets) {
    if (!canvas || !mesh || !model_matrix || !material || !morph_targets)
        return;

    rt_mesh3d *m = (rt_mesh3d *)mesh;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)morph_targets;

    if (m->vertex_count == 0)
        return;
    if (m->vertex_count != (uint32_t)mt->vertex_count)
        return;

    rt_canvas3d *c = (rt_canvas3d *)canvas;
    if (c && c->backend && !rt_morphtarget3d_has_tangent_deltas(mt) &&
        vgfx3d_backend_prefers_gpu_morph(c->backend->name, mt->shape_count)) {
        void *saved_ref;
        const float *saved_deltas;
        const float *saved_normal_deltas;
        const float *saved_weights;
        const float *saved_prev_weights;
        int32_t saved_shape_count;
        const float *prev_weights =
            morphtarget_prepare_prev_weights(mt, rt_canvas3d_get_frame_serial(canvas));
        const float *packed_deltas = rt_morphtarget3d_get_packed_deltas(mt);
        const float *packed_normal_deltas = rt_morphtarget3d_get_packed_normal_deltas(mt);
        if (mt->shape_count > 0 && !packed_deltas)
            return;
        rt_canvas3d_add_temp_object(canvas, mt);

        saved_ref = m->morph_targets_ref;
        saved_deltas = m->morph_deltas;
        saved_normal_deltas = m->morph_normal_deltas;
        saved_weights = m->morph_weights;
        saved_prev_weights = m->prev_morph_weights;
        saved_shape_count = m->morph_shape_count;

        m->morph_targets_ref = morph_targets;
        m->morph_deltas = packed_deltas;
        m->morph_normal_deltas = packed_normal_deltas;
        m->morph_weights = mt->weights;
        m->prev_morph_weights = prev_weights;
        m->morph_shape_count = mt->shape_count;
        rt_canvas3d_draw_mesh_matrix_keyed(canvas, mesh, model_matrix, material, motion_key, NULL, prev_weights);
        m->morph_targets_ref = saved_ref;
        m->morph_deltas = saved_deltas;
        m->morph_normal_deltas = saved_normal_deltas;
        m->morph_weights = saved_weights;
        m->prev_morph_weights = saved_prev_weights;
        m->morph_shape_count = saved_shape_count;
        return;
    }

    /* Allocate morphed vertex buffer */
    vgfx3d_vertex_t *morphed =
        (vgfx3d_vertex_t *)malloc((size_t)m->vertex_count * sizeof(vgfx3d_vertex_t));
    if (!morphed)
        return;

    /* Start with base mesh */
    memcpy(morphed, m->vertices, (size_t)m->vertex_count * sizeof(vgfx3d_vertex_t));

    /* Accumulate weighted deltas */
    int has_normal_deltas = 0;
    int has_tangent_deltas = 0;
    for (int32_t s = 0; s < mt->shape_count; s++) {
        float w = mt->weights[s];
        if (fabsf(w) < 1e-6f)
            continue;

        const float *pd = mt->shapes[s].pos_deltas;
        const float *nd = mt->shapes[s].nrm_deltas;
        const float *td = mt->shapes[s].tan_deltas;
        if (!pd)
            continue;

        for (uint32_t v = 0; v < m->vertex_count; v++) {
            morphed[v].pos[0] += w * pd[v * 3 + 0];
            morphed[v].pos[1] += w * pd[v * 3 + 1];
            morphed[v].pos[2] += w * pd[v * 3 + 2];

            if (nd) {
                morphed[v].normal[0] += w * nd[v * 3 + 0];
                morphed[v].normal[1] += w * nd[v * 3 + 1];
                morphed[v].normal[2] += w * nd[v * 3 + 2];
                has_normal_deltas = 1;
            }
            if (td) {
                morphed[v].tangent[0] += w * td[v * 3 + 0];
                morphed[v].tangent[1] += w * td[v * 3 + 1];
                morphed[v].tangent[2] += w * td[v * 3 + 2];
                has_tangent_deltas = 1;
            }
        }
    }

    /* Re-normalize normals if any shape had normal deltas */
    if (has_normal_deltas) {
        for (uint32_t v = 0; v < m->vertex_count; v++) {
            float *n = morphed[v].normal;
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 1e-8f) {
                n[0] /= len;
                n[1] /= len;
                n[2] /= len;
            }
        }
    }

    if (has_tangent_deltas) {
        for (uint32_t v = 0; v < m->vertex_count; v++) {
            float *t = morphed[v].tangent;
            float len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            if (len > 1e-8f) {
                t[0] /= len;
                t[1] /= len;
                t[2] /= len;
            }
        }
    }

    /* Register buffer for end-of-frame cleanup (avoids use-after-free
     * with deferred draw queue) */
    rt_canvas3d_add_temp_buffer(canvas, morphed);

    /* Submit via normal draw pipeline */
    rt_mesh3d tmp = *m;
    tmp.vertices = morphed;
    rt_canvas3d_draw_mesh_matrix_keyed(
        canvas, &tmp, model_matrix, material, motion_key, NULL, NULL);
}

/// @brief Draw a mesh with morph targets applied, using a raw 4×4 model matrix.
/// On GPU-capable backends (Metal/OpenGL/D3D11), uploads packed deltas + weights
/// and blends in the vertex shader. On other backends, applies CPU morph then
/// submits via the standard draw pipeline.
void rt_canvas3d_draw_mesh_matrix_morphed(void *canvas,
                                          void *mesh,
                                          const double *model_matrix,
                                          void *material,
                                          const void *motion_key,
                                          void *morph_targets) {
    morphtarget_draw_mesh_matrix(canvas, mesh, model_matrix, material, motion_key, morph_targets);
}

/// @brief Draw a morphed mesh using a Mat4 transform handle (convenience wrapper).
/// The transform doubles as the motion-vector key for temporal effects (TAA, motion blur).
void rt_canvas3d_draw_mesh_morphed(
    void *canvas, void *mesh, void *transform, void *material, void *morph_targets) {
    if (!transform)
        return;
    morphtarget_draw_mesh_matrix(
        canvas, mesh, ((mat4_impl *)transform)->m, material, transform, morph_targets);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
