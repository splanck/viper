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
#include "vgfx3d_backend.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);
extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
extern void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

#define VGFX3D_MAX_MORPH_SHAPES 32

typedef struct {
    char name[64];
    float *pos_deltas; /* 3 * vertex_count floats (dx, dy, dz per vertex) */
    float *nrm_deltas; /* 3 * vertex_count floats (or NULL) */
} vgfx3d_morph_shape_t;

typedef struct {
    void *vptr;
    vgfx3d_morph_shape_t shapes[VGFX3D_MAX_MORPH_SHAPES];
    float weights[VGFX3D_MAX_MORPH_SHAPES];
    float prev_weights[VGFX3D_MAX_MORPH_SHAPES];
    float motion_weight_snapshot[VGFX3D_MAX_MORPH_SHAPES];
    int32_t shape_count;
    int32_t vertex_count;
    int64_t last_motion_frame;
    int8_t has_prev_weights;
} rt_morphtarget3d;

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
}

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
    rt_obj_set_finalizer(mt, rt_morphtarget3d_finalize);
    return mt;
}

/*==========================================================================
 * Shape management
 *=========================================================================*/

/// @brief Register a new named blend shape and allocate its per-vertex delta arrays.
/// @details Each shape stores position deltas (and optionally normal deltas) for
///          every vertex in the base mesh. When blended, vertex_final = base +
///          sum(weight_i * delta_i). Returns the shape index, or -1 if the max
///          shape limit (32) is reached.
int64_t rt_morphtarget3d_add_shape(void *obj, rt_string name) {
    if (!obj)
        return -1;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (mt->shape_count >= VGFX3D_MAX_MORPH_SHAPES) {
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

    return mt->shape_count++;
}

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
}

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
}

/*==========================================================================
 * Weight control
 *=========================================================================*/

/// @brief Set the blend weight for a shape by index (0 = no effect, 1 = full deformation).
/// @details During apply(), each vertex is displaced by weight * position_delta.
///          Weights can exceed 1.0 for exaggeration or be negative for inverse deformation.
void rt_morphtarget3d_set_weight(void *obj, int64_t shape, double weight) {
    if (!obj)
        return;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return;
    mt->weights[shape] = (float)weight;
}

/// @brief Return the current blend weight for a shape by index.
double rt_morphtarget3d_get_weight(void *obj, int64_t shape) {
    if (!obj)
        return 0.0;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)obj;
    if (shape < 0 || shape >= mt->shape_count)
        return 0.0;
    return mt->weights[shape];
}

/// @brief Set the blend weight for a shape looked up by its string name.
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

/// @brief Return the count of elements in the morphtarget3d.
int64_t rt_morphtarget3d_get_shape_count(void *obj) {
    return obj ? ((rt_morphtarget3d *)obj)->shape_count : 0;
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
 * Mesh integration (placeholder — morph targets passed at draw time)
 *=========================================================================*/

/// @brief Set the morph targets of the mesh3d.
void rt_mesh3d_set_morph_targets(void *mesh, void *morph_targets) {
    (void)mesh;
    (void)morph_targets;
    /* Morph target reference stored conceptually. In practice, morph targets
     * are passed directly to DrawMeshMorphed at draw time. */
}

/*==========================================================================
 * CPU morph application + drawing
 *=========================================================================*/

void rt_canvas3d_draw_mesh_morphed(
    void *canvas, void *mesh, void *transform, void *material, void *morph_targets) {
    if (!canvas || !mesh || !transform || !material || !morph_targets)
        return;

    rt_mesh3d *m = (rt_mesh3d *)mesh;
    rt_morphtarget3d *mt = (rt_morphtarget3d *)morph_targets;

    if (m->vertex_count == 0)
        return;
    if (m->vertex_count != (uint32_t)mt->vertex_count)
        return;

    rt_canvas3d *c = (rt_canvas3d *)canvas;
    if (c && c->backend && vgfx3d_backend_prefers_gpu_morph(c->backend->name)) {
        const float *prev_weights =
            morphtarget_prepare_prev_weights(mt, rt_canvas3d_get_frame_serial(canvas));
        size_t delta_count = (size_t)mt->shape_count * (size_t)mt->vertex_count * 3;
        float *packed_deltas = NULL;
        float *packed_normal_deltas = NULL;
        float *packed_weights = NULL;
        int has_normal_deltas = 0;
        if (delta_count > 0)
            packed_deltas = (float *)calloc(delta_count, sizeof(float));
        for (int32_t s = 0; s < mt->shape_count; s++) {
            if (mt->shapes[s].nrm_deltas) {
                has_normal_deltas = 1;
                break;
            }
        }
        if (delta_count > 0 && has_normal_deltas)
            packed_normal_deltas = (float *)calloc(delta_count, sizeof(float));
        if (mt->shape_count > 0)
            packed_weights = (float *)malloc((size_t)mt->shape_count * sizeof(float));
        if ((delta_count > 0 && !packed_deltas) || (delta_count > 0 && has_normal_deltas && !packed_normal_deltas) ||
            (mt->shape_count > 0 && !packed_weights)) {
            free(packed_deltas);
            free(packed_normal_deltas);
            free(packed_weights);
            return;
        }

        for (int32_t s = 0; s < mt->shape_count; s++) {
            if (packed_weights)
                packed_weights[s] = mt->weights[s];
            if (packed_deltas && mt->shapes[s].pos_deltas) {
                memcpy(&packed_deltas[(size_t)s * (size_t)mt->vertex_count * 3],
                       mt->shapes[s].pos_deltas,
                       (size_t)mt->vertex_count * 3 * sizeof(float));
            }
            if (packed_normal_deltas && mt->shapes[s].nrm_deltas) {
                memcpy(&packed_normal_deltas[(size_t)s * (size_t)mt->vertex_count * 3],
                       mt->shapes[s].nrm_deltas,
                       (size_t)mt->vertex_count * 3 * sizeof(float));
            }
        }

        if (packed_deltas)
            rt_canvas3d_add_temp_buffer(canvas, packed_deltas);
        if (packed_normal_deltas)
            rt_canvas3d_add_temp_buffer(canvas, packed_normal_deltas);
        if (packed_weights)
            rt_canvas3d_add_temp_buffer(canvas, packed_weights);

        rt_mesh3d tmp = *m;
        tmp.morph_deltas = packed_deltas;
        tmp.morph_normal_deltas = packed_normal_deltas;
        tmp.morph_weights = packed_weights;
        tmp.prev_morph_weights = prev_weights;
        tmp.morph_shape_count = mt->shape_count;
        rt_canvas3d_draw_mesh_matrix_keyed(canvas,
                                           &tmp,
                                           ((mat4_impl *)transform)->m,
                                           material,
                                           transform,
                                           NULL,
                                           prev_weights);
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
    rt_canvas3d_draw_mesh(canvas, &tmp, transform, material);
}

#endif /* VIPER_ENABLE_GRAPHICS */
