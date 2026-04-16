//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_morphtarget3d.c
// Purpose: MorphTarget3D — named blend shapes with per-vertex position and
//   normal deltas. CPU-applied before draw submission.
//
// Key invariants:
//   - Deltas are stored as 3 floats per vertex per shape (sparse via zero default).
//   - Morph application: dst = base + sum(weight[i] * delta[i]) per vertex.
//   - Normals re-normalized after morph if any shape has normal deltas.
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

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[64];
    float *pos_deltas; /* 3 * vertex_count floats (dx, dy, dz per vertex) */
    float *nrm_deltas; /* 3 * vertex_count floats (or NULL) */
} vgfx3d_morph_shape_t;

typedef struct {
    void *vptr;
    vgfx3d_morph_shape_t shapes[VGFX3D_D3D11_MAX_MORPH_SHAPES];
    float weights[VGFX3D_D3D11_MAX_MORPH_SHAPES];
    float prev_weights[VGFX3D_D3D11_MAX_MORPH_SHAPES];
    float motion_weight_snapshot[VGFX3D_D3D11_MAX_MORPH_SHAPES];
    float *packed_pos_deltas;
    float *packed_nrm_deltas;
    uint64_t payload_generation;
    int32_t shape_count;
    int32_t vertex_count;
    int64_t last_motion_frame;
    int8_t has_prev_weights;
    int8_t packed_dirty;
} rt_morphtarget3d;

static void morphtarget_touch_payload(rt_morphtarget3d *mt) {
    if (!mt)
        return;
    mt->packed_dirty = 1;
    if (mt->payload_generation == UINT64_MAX)
        mt->payload_generation = 1;
    else
        mt->payload_generation++;
}

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

static int vgfx3d_backend_prefers_gpu_morph(const char *backend_name) {
    if (!backend_name)
        return 0;
    return strcmp(backend_name, "metal") == 0 || strcmp(backend_name, "opengl") == 0 ||
           strcmp(backend_name, "d3d11") == 0;
}

/*==========================================================================
 * Lifecycle
 *=========================================================================*/

static void rt_morphtarget3d_finalize(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    for (int32_t i = 0; i < mt->shape_count; i++) {
        free(mt->shapes[i].pos_deltas);
        free(mt->shapes[i].nrm_deltas);
    }
    free(mt->packed_pos_deltas);
    free(mt->packed_nrm_deltas);
}

/// @brief Create a morph target container for blendshape animation.
/// @details Morph targets (aka blend shapes) deform a mesh by adding weighted
///          per-vertex deltas to the base positions and normals. Up to 32 shapes
///          are supported. Each shape stores position deltas (and optionally normal
///          deltas) for all vertices. Weights control the blend amount per shape.
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
    memset(mt->shapes, 0, sizeof(mt->shapes));
    memset(mt->weights, 0, sizeof(mt->weights));
    memset(mt->prev_weights, 0, sizeof(mt->prev_weights));
    memset(mt->motion_weight_snapshot, 0, sizeof(mt->motion_weight_snapshot));
    mt->shape_count = 0;
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

/*==========================================================================
 * Shape management
 *=========================================================================*/

/// @brief Register a named blend shape and allocate its per-vertex delta arrays.
int64_t rt_morphtarget3d_add_shape(void *obj, rt_string name) {
    if (!obj)
        return -1;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (mt->shape_count >= VGFX3D_D3D11_MAX_MORPH_SHAPES) {
        rt_trap("MorphTarget3D.AddShape: max 32 shapes exceeded");
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
            mt->weights[i] = (float)weight;
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

/// @brief Monotonic counter that bumps whenever any delta changes.
/// GPU caches compare against the previous value to detect when re-upload is required.
uint64_t rt_morphtarget3d_get_payload_generation(void *obj) {
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (!mt)
        return 0;
    return mt->payload_generation;
}

static const float *morphtarget_prepare_prev_weights(rt_morphtarget3d *mt, int64_t frame_serial) {
    if (!mt)
        return NULL;
    if (mt->last_motion_frame != frame_serial) {
        if (mt->last_motion_frame != 0) {
            memcpy(mt->prev_weights, mt->motion_weight_snapshot, sizeof(mt->prev_weights));
            mt->has_prev_weights = 1;
        }
        memcpy(mt->motion_weight_snapshot, mt->weights, sizeof(mt->motion_weight_snapshot));
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
    if (c && c->backend && vgfx3d_backend_prefers_gpu_morph(c->backend->name)) {
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
    for (int32_t s = 0; s < mt->shape_count; s++) {
        float w = mt->weights[s];
        if (fabsf(w) < 1e-6f)
            continue;

        const float *pd = mt->shapes[s].pos_deltas;
        const float *nd = mt->shapes[s].nrm_deltas;
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
