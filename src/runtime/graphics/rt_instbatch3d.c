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
//   - Mesh/material are borrowed (caller keeps them alive).
//
// Links: rt_instbatch3d.h, rt_canvas3d.c, vgfx3d_backend.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_instbatch3d.h"
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
extern double rt_mat4_get(void *m, int64_t r, int64_t c);

#define INST_INIT_CAP 64

typedef struct {
    void *vptr;
    void *mesh;        /* borrowed Mesh3D */
    void *material;    /* borrowed Material3D */
    float *transforms; /* N * 16 floats */
    float *current_snapshot; /* current-frame snapshot for motion history */
    float *prev_transforms;  /* previous-frame transforms */
    int32_t instance_count;
    int32_t instance_capacity;
    int32_t motion_snapshot_count;
    int32_t prev_count;
    int64_t last_motion_frame;
    int8_t has_prev_snapshot;
} rt_instbatch3d;

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
}

void *rt_instbatch3d_new(void *mesh, void *material) {
    if (!mesh || !material)
        return NULL;
    rt_instbatch3d *b = (rt_instbatch3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_instbatch3d));
    if (!b) {
        rt_trap("InstanceBatch3D.New: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->mesh = mesh;
    b->material = material;
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
        rt_trap("InstanceBatch3D.New: allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(b, instbatch_finalizer);
    return b;
}

void rt_instbatch3d_add(void *obj, void *transform) {
    if (!obj || !transform)
        return;
    rt_instbatch3d *b = (rt_instbatch3d *)obj;

    if (b->instance_count >= b->instance_capacity) {
        int32_t new_cap = b->instance_capacity * 2;
        b->transforms = (float *)realloc(b->transforms, (size_t)new_cap * 16 * sizeof(float));
        b->current_snapshot =
            (float *)realloc(b->current_snapshot, (size_t)new_cap * 16 * sizeof(float));
        b->prev_transforms =
            (float *)realloc(b->prev_transforms, (size_t)new_cap * 16 * sizeof(float));
        if (!b->transforms || !b->current_snapshot || !b->prev_transforms)
            return;
        b->instance_capacity = new_cap;
    }

    /* Copy Mat4 (double) to float[16] */
    float *dst = &b->transforms[b->instance_count * 16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            dst[i * 4 + j] = (float)rt_mat4_get(transform, i, j);

    b->instance_count++;
}

void rt_instbatch3d_remove(void *obj, int64_t index) {
    if (!obj)
        return;
    rt_instbatch3d *b = (rt_instbatch3d *)obj;
    if (index < 0 || index >= b->instance_count)
        return;

    /* Swap with last */
    if (index < b->instance_count - 1)
        memcpy(&b->transforms[index * 16],
               &b->transforms[(b->instance_count - 1) * 16],
               16 * sizeof(float));
    b->instance_count--;
    if (b->motion_snapshot_count > b->instance_count)
        b->motion_snapshot_count = b->instance_count;
    if (b->prev_count > b->instance_count)
        b->prev_count = b->instance_count;
}

void rt_instbatch3d_set(void *obj, int64_t index, void *transform) {
    if (!obj || !transform)
        return;
    rt_instbatch3d *b = (rt_instbatch3d *)obj;
    if (index < 0 || index >= b->instance_count)
        return;

    float *dst = &b->transforms[index * 16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            dst[i * 4 + j] = (float)rt_mat4_get(transform, i, j);
}

void rt_instbatch3d_clear(void *obj) {
    if (!obj)
        return;
    rt_instbatch3d *b = (rt_instbatch3d *)obj;
    b->instance_count = 0;
    b->motion_snapshot_count = 0;
    b->prev_count = 0;
    b->has_prev_snapshot = 0;
}

int64_t rt_instbatch3d_count(void *obj) {
    return obj ? ((rt_instbatch3d *)obj)->instance_count : 0;
}

/// @brief Draw all visible instances. Falls back to N individual draw calls
/// since the software backend doesn't have a native instanced path.
void rt_canvas3d_draw_instanced(void *canvas_obj, void *batch_obj) {
    if (!canvas_obj || !batch_obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas_obj;
    rt_instbatch3d *b = (rt_instbatch3d *)batch_obj;
    if (!c->in_frame || !c->backend || b->instance_count == 0)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)b->mesh;
    if (!mesh || mesh->vertex_count == 0 || mesh->index_count == 0)
        return;

    /* Build draw command from batch mesh/material */
    rt_material3d *mat = (rt_material3d *)b->material;

    /* Try GPU instanced path if the backend supports it (MTL-13) */
    if (c->backend->submit_draw_instanced) {
        if (b->last_motion_frame != rt_canvas3d_get_frame_serial(canvas_obj)) {
            if (b->motion_snapshot_count > 0) {
                memcpy(b->prev_transforms,
                       b->current_snapshot,
                       (size_t)b->motion_snapshot_count * 16 * sizeof(float));
                b->prev_count = b->motion_snapshot_count;
                b->has_prev_snapshot = 1;
            }
            memcpy(b->current_snapshot, b->transforms, (size_t)b->instance_count * 16 * sizeof(float));
            b->motion_snapshot_count = b->instance_count;
            b->last_motion_frame = rt_canvas3d_get_frame_serial(canvas_obj);
        }
        vgfx3d_draw_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.vertices = mesh->vertices;
        cmd.vertex_count = mesh->vertex_count;
        cmd.indices = mesh->indices;
        cmd.index_count = mesh->index_count;
        /* Use identity model matrix — per-instance transforms in instance buffer */
        cmd.model_matrix[0] = cmd.model_matrix[5] = cmd.model_matrix[10] =
            cmd.model_matrix[15] = 1.0f;
        cmd.diffuse_color[0] = (float)mat->diffuse[0];
        cmd.diffuse_color[1] = (float)mat->diffuse[1];
        cmd.diffuse_color[2] = (float)mat->diffuse[2];
        cmd.diffuse_color[3] = (float)mat->diffuse[3];
        cmd.specular[0] = (float)mat->specular[0];
        cmd.specular[1] = (float)mat->specular[1];
        cmd.specular[2] = (float)mat->specular[2];
        cmd.shininess = (float)mat->shininess;
        cmd.alpha = (float)mat->alpha;
        cmd.unlit = mat->unlit;
        cmd.texture = mat->texture;
        cmd.normal_map = mat->normal_map;
        cmd.specular_map = mat->specular_map;
        cmd.emissive_map = mat->emissive_map;
        cmd.emissive_color[0] = (float)mat->emissive[0];
        cmd.emissive_color[1] = (float)mat->emissive[1];
        cmd.emissive_color[2] = (float)mat->emissive[2];
        cmd.env_map = mat->env_map;
        cmd.reflectivity = (float)mat->reflectivity;
        cmd.prev_instance_matrices =
            (b->has_prev_snapshot && b->prev_count == b->instance_count) ? b->prev_transforms : NULL;
        cmd.has_prev_instance_matrices =
            (cmd.prev_instance_matrices && b->instance_count > 0) ? 1 : 0;

        vgfx3d_light_params_t lp[VGFX3D_MAX_LIGHTS];
        int32_t lc = 0;
        for (int li = 0; li < VGFX3D_MAX_LIGHTS; li++) {
            const rt_light3d *l = c->lights[li];
            if (!l)
                continue;
            lp[lc].type = l->type;
            lp[lc].direction[0] = (float)l->direction[0];
            lp[lc].direction[1] = (float)l->direction[1];
            lp[lc].direction[2] = (float)l->direction[2];
            lp[lc].position[0] = (float)l->position[0];
            lp[lc].position[1] = (float)l->position[1];
            lp[lc].position[2] = (float)l->position[2];
            lp[lc].color[0] = (float)l->color[0];
            lp[lc].color[1] = (float)l->color[1];
            lp[lc].color[2] = (float)l->color[2];
            lp[lc].intensity = (float)l->intensity;
            lp[lc].attenuation = (float)l->attenuation;
            lp[lc].inner_cos = (float)l->inner_cos;
            lp[lc].outer_cos = (float)l->outer_cos;
            lc++;
        }
        c->backend->submit_draw_instanced(
            c->backend_ctx, c->gfx_win, &cmd,
            b->transforms, b->instance_count,
            lp, lc, c->ambient, c->wireframe, c->backface_cull);
        return;
    }

    /* Software fallback: issue N individual draw calls */
    for (int32_t i = 0; i < b->instance_count; i++) {
        /* Convert float[16] back to a Mat4 object for DrawMesh */
        float *src = &b->transforms[i * 16];

        /* Build a draw command using the existing submit_draw path */
        vgfx3d_draw_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.vertices = mesh->vertices;
        cmd.vertex_count = mesh->vertex_count;
        cmd.indices = mesh->indices;
        cmd.index_count = mesh->index_count;

        /* Model matrix from instance transform */
        memcpy(cmd.model_matrix, src, 16 * sizeof(float));

        /* Material properties */
        cmd.diffuse_color[0] = (float)mat->diffuse[0];
        cmd.diffuse_color[1] = (float)mat->diffuse[1];
        cmd.diffuse_color[2] = (float)mat->diffuse[2];
        cmd.diffuse_color[3] = (float)mat->alpha;
        cmd.shininess = (float)mat->shininess;
        cmd.alpha = (float)mat->alpha;
        cmd.unlit = mat->unlit;
        cmd.texture = mat->texture;
        cmd.emissive_map = mat->emissive_map;
        cmd.emissive_color[0] = (float)mat->emissive[0];
        cmd.emissive_color[1] = (float)mat->emissive[1];
        cmd.emissive_color[2] = (float)mat->emissive[2];

        /* Build light params from pointer array (same as rt_canvas3d_draw_mesh) */
        vgfx3d_light_params_t lp[VGFX3D_MAX_LIGHTS];
        int32_t lc = 0;
        for (int li = 0; li < VGFX3D_MAX_LIGHTS; li++) {
            const rt_light3d *l = c->lights[li];
            if (!l)
                continue;
            lp[lc].type = l->type;
            lp[lc].direction[0] = (float)l->direction[0];
            lp[lc].direction[1] = (float)l->direction[1];
            lp[lc].direction[2] = (float)l->direction[2];
            lp[lc].position[0] = (float)l->position[0];
            lp[lc].position[1] = (float)l->position[1];
            lp[lc].position[2] = (float)l->position[2];
            lp[lc].color[0] = (float)l->color[0];
            lp[lc].color[1] = (float)l->color[1];
            lp[lc].color[2] = (float)l->color[2];
            lp[lc].intensity = (float)l->intensity;
            lp[lc].attenuation = (float)l->attenuation;
            lc++;
        }
        c->backend->submit_draw(
            c->backend_ctx, c->gfx_win, &cmd, lp, lc, c->ambient, c->wireframe, c->backface_cull);
    }
}

#endif /* VIPER_ENABLE_GRAPHICS */
