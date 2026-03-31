//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_vegetation3d.c
// Purpose: Instanced vegetation rendering with cross-billboard blades,
//   wind animation, density map population, and distance-based LOD.
//
// Key invariants:
//   - Blade mesh: 2 perpendicular quads (8 verts, 4 tris) — cross-billboard.
//   - Population: LCG random scatter on terrain, filtered by density map.
//   - Wind: per-blade Y-axis shear using sin(position + time).
//   - LOD: progressive thinning + hard cull at far distance.
//   - Rendering: uses submit_draw_instanced for single-call GPU batching.
//
// Links: rt_vegetation3d.h, rt_terrain3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_vegetation3d.h"
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
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz,
    double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform,
                                   void *material);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_texture(void *m, void *tex);
extern void rt_material3d_set_unlit(void *m, int8_t u);
extern double rt_terrain3d_get_height_at(void *terrain, double x, double z);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

typedef struct {
    void *vptr;
    void *blade_mesh;     /* cross-billboard mesh (shared by all blades) */
    void *blade_material; /* textured, unlit */
    /* Blade geometry params */
    double blade_width;
    double blade_height;
    double size_variation; /* [0.0-1.0] random scale variation */
    /* Population */
    float *base_transforms; /* total_count * 16 floats */
    float *positions;       /* total_count * 3 floats (x,y,z for wind/LOD) */
    int32_t total_count;
    int32_t capacity;
    void *density_map;
    /* Wind */
    double wind_speed;
    double wind_strength;
    double wind_turbulence;
    double time;
    /* LOD */
    float lod_near;
    float lod_far;
    /* Per-frame visible set */
    float *visible_transforms; /* visible_count * 16 floats */
    int32_t visible_count;
    int32_t visible_capacity;
} rt_vegetation3d;

static void vegetation3d_finalizer(void *obj) {
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    free(v->base_transforms);
    free(v->positions);
    free(v->visible_transforms);
    v->base_transforms = NULL;
    v->positions = NULL;
    v->visible_transforms = NULL;
}

/// @brief Build the cross-billboard blade mesh (2 perpendicular quads).
static void build_blade_mesh(void *mesh, double w, double h) {
    double hw = w * 0.5;
    /* Quad 1: X-aligned */
    rt_mesh3d_add_vertex(mesh, -hw, 0, 0, 0, 0, 1, 0, 1);
    rt_mesh3d_add_vertex(mesh, hw, 0, 0, 0, 0, 1, 1, 1);
    rt_mesh3d_add_vertex(mesh, hw, h, 0, 0, 0, 1, 1, 0);
    rt_mesh3d_add_vertex(mesh, -hw, h, 0, 0, 0, 1, 0, 0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    /* Quad 2: Z-aligned (perpendicular) */
    rt_mesh3d_add_vertex(mesh, 0, 0, -hw, 1, 0, 0, 0, 1);
    rt_mesh3d_add_vertex(mesh, 0, 0, hw, 1, 0, 0, 1, 1);
    rt_mesh3d_add_vertex(mesh, 0, h, hw, 1, 0, 0, 1, 0);
    rt_mesh3d_add_vertex(mesh, 0, h, -hw, 1, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(mesh, 4, 5, 6);
    rt_mesh3d_add_triangle(mesh, 4, 6, 7);
}

/// @brief Simple LCG random number generator.
static uint32_t lcg_next(uint32_t *state) {
    *state = *state * 1103515245u + 12345u;
    return *state;
}

void *rt_vegetation3d_new(void *blade_texture) {
    rt_vegetation3d *v =
        (rt_vegetation3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_vegetation3d));
    if (!v) {
        rt_trap("Vegetation3D.New: allocation failed");
        return NULL;
    }
    v->vptr = NULL;
    v->blade_width = 0.4;
    v->blade_height = 1.2;
    v->size_variation = 0.3;
    v->base_transforms = NULL;
    v->positions = NULL;
    v->total_count = 0;
    v->capacity = 0;
    v->density_map = NULL;
    v->wind_speed = 2.0;
    v->wind_strength = 0.15;
    v->wind_turbulence = 0.5;
    v->time = 0.0;
    v->lod_near = 40.0f;
    v->lod_far = 100.0f;
    v->visible_transforms = NULL;
    v->visible_count = 0;
    v->visible_capacity = 0;

    /* Build blade mesh */
    v->blade_mesh = rt_mesh3d_new();
    build_blade_mesh(v->blade_mesh, v->blade_width, v->blade_height);

    /* Build material */
    v->blade_material = rt_material3d_new();
    if (blade_texture)
        rt_material3d_set_texture(v->blade_material, blade_texture);
    rt_material3d_set_unlit(v->blade_material, 1);

    rt_obj_set_finalizer(v, vegetation3d_finalizer);
    return v;
}

void rt_vegetation3d_set_density_map(void *obj, void *pixels) {
    if (obj)
        ((rt_vegetation3d *)obj)->density_map = pixels;
}

void rt_vegetation3d_set_wind_params(void *obj, double speed, double strength,
                                      double turbulence) {
    if (!obj)
        return;
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    v->wind_speed = speed;
    v->wind_strength = strength;
    v->wind_turbulence = turbulence;
}

void rt_vegetation3d_set_lod_distances(void *obj, double near_dist,
                                        double far_dist) {
    if (!obj)
        return;
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    v->lod_near = (float)near_dist;
    v->lod_far = (float)far_dist;
}

void rt_vegetation3d_set_blade_size(void *obj, double width, double height,
                                     double variation) {
    if (!obj)
        return;
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    v->blade_width = width;
    v->blade_height = height;
    v->size_variation = variation;
    /* Rebuild blade mesh with new size */
    if (v->blade_mesh) {
        rt_mesh3d *m = (rt_mesh3d *)v->blade_mesh;
        m->vertex_count = 0;
        m->index_count = 0;
    }
    build_blade_mesh(v->blade_mesh, width, height);
}

/// @brief Build a row-major 4x4 transform: translate(x,y,z) * rotateY(angle) * scale(s).
static void build_transform(float *out, double x, double y, double z,
                              double angle, double s) {
    double ca = cos(angle), sa = sin(angle);
    /* Row 0 */ out[0] = (float)(ca * s);  out[1] = 0;          out[2] = (float)(sa * s);  out[3] = (float)x;
    /* Row 1 */ out[4] = 0;                out[5] = (float)s;   out[6] = 0;                out[7] = (float)y;
    /* Row 2 */ out[8] = (float)(-sa * s); out[9] = 0;          out[10] = (float)(ca * s); out[11] = (float)z;
    /* Row 3 */ out[12] = 0;               out[13] = 0;         out[14] = 0;               out[15] = 1.0f;
}

void rt_vegetation3d_populate(void *obj, void *terrain, int64_t count) {
    if (!obj || !terrain || count <= 0)
        return;
    rt_vegetation3d *v = (rt_vegetation3d *)obj;

    /* Get terrain dimensions for scatter bounds */
    typedef struct {
        void *vptr;
        float *heights;
        int32_t width, depth;
        double scale[3];
    } terrain_view;
    terrain_view *tv = (terrain_view *)terrain;
    double tw = tv->width * tv->scale[0];
    double td = tv->depth * tv->scale[2];

    /* Allocate storage */
    int32_t cap = (int32_t)count;
    v->base_transforms = (float *)realloc(v->base_transforms,
                                           (size_t)cap * 16 * sizeof(float));
    v->positions = (float *)realloc(v->positions,
                                     (size_t)cap * 3 * sizeof(float));
    v->capacity = cap;
    v->total_count = 0;

    uint32_t rng = 42;

    for (int64_t i = 0; i < count; i++) {
        /* Random position within terrain bounds (margin of 2 units) */
        double fx = (double)(lcg_next(&rng) & 0xFFFF) / 65535.0;
        double fz = (double)(lcg_next(&rng) & 0xFFFF) / 65535.0;
        double wx = 2.0 + fx * (tw - 4.0);
        double wz = 2.0 + fz * (td - 4.0);

        /* Density map check */
        if (v->density_map) {
            int64_t pw = rt_pixels_width(v->density_map);
            int64_t ph = rt_pixels_height(v->density_map);
            int64_t px = (int64_t)(fx * (pw - 1));
            int64_t pz = (int64_t)(fz * (ph - 1));
            int64_t pixel = rt_pixels_get(v->density_map, px, pz);
            int32_t density = (int32_t)((pixel >> 24) & 0xFF); /* R channel */
            uint32_t roll = lcg_next(&rng) & 0xFF;
            if ((int32_t)roll > density)
                continue; /* skip based on density */
        }

        double wy = rt_terrain3d_get_height_at(terrain, wx, wz);

        /* Random Y rotation + scale variation */
        double angle =
            ((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) * 6.283185307;
        double scale_var =
            1.0 +
            (((double)(lcg_next(&rng) & 0xFFFF) / 65535.0) - 0.5) * 2.0 *
                v->size_variation;

        int32_t idx = v->total_count;
        build_transform(&v->base_transforms[idx * 16], wx, wy, wz, angle,
                         scale_var);
        v->positions[idx * 3 + 0] = (float)wx;
        v->positions[idx * 3 + 1] = (float)wy;
        v->positions[idx * 3 + 2] = (float)wz;
        v->total_count++;
    }
}

void rt_vegetation3d_update(void *obj, double dt, double camX, double camY,
                             double camZ) {
    if (!obj || dt <= 0)
        return;
    rt_vegetation3d *v = (rt_vegetation3d *)obj;
    v->time += dt;

    /* Ensure visible buffer is large enough */
    if (v->visible_capacity < v->total_count) {
        v->visible_capacity = v->total_count;
        v->visible_transforms = (float *)realloc(
            v->visible_transforms,
            (size_t)v->visible_capacity * 16 * sizeof(float));
    }
    v->visible_count = 0;

    for (int32_t i = 0; i < v->total_count; i++) {
        float bx = v->positions[i * 3 + 0];
        float bz = v->positions[i * 3 + 2];

        /* Distance to camera (XZ plane) */
        float dx = bx - (float)camX;
        float dz = bz - (float)camZ;
        float dist = sqrtf(dx * dx + dz * dz);

        /* Hard cull beyond far distance */
        if (dist > v->lod_far)
            continue;

        /* Progressive thinning between near and far */
        if (dist > v->lod_near) {
            float t = (dist - v->lod_near) / (v->lod_far - v->lod_near);
            int skip = 1 + (int)(t * 4.0f);
            if ((i % skip) != 0)
                continue;
        }

        /* Copy base transform */
        float *dst = &v->visible_transforms[v->visible_count * 16];
        memcpy(dst, &v->base_transforms[i * 16], 16 * sizeof(float));

        /* Apply wind shear to the transform's Y-column entries.
         * This bends the blade tops in the wind direction. */
        double phase = v->wind_turbulence * (bx * 0.1 + bz * 0.07) +
                       v->time * v->wind_speed;
        float wind_x = (float)(sin(phase) * v->wind_strength);
        float wind_z = (float)(cos(phase * 0.7) * v->wind_strength * 0.5);
        dst[1] += wind_x; /* shear X column by wind */
        dst[9] += wind_z; /* shear Z column by wind */

        v->visible_count++;
    }
}

void rt_canvas3d_draw_vegetation(void *canvas_obj, void *veg_obj) {
    if (!canvas_obj || !veg_obj)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas_obj;
    rt_vegetation3d *v = (rt_vegetation3d *)veg_obj;
    if (!c->in_frame || !c->backend || v->visible_count == 0)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)v->blade_mesh;
    rt_material3d *mat = (rt_material3d *)v->blade_material;
    if (!mesh || mesh->vertex_count == 0 || !mat)
        return;

    /* Disable backface culling for grass (visible from both sides) */
    int8_t prev_cull = c->backface_cull;
    c->backface_cull = 0;

    /* Try GPU instanced path */
    if (c->backend->submit_draw_instanced) {
        vgfx3d_draw_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.vertices = mesh->vertices;
        cmd.vertex_count = mesh->vertex_count;
        cmd.indices = mesh->indices;
        cmd.index_count = mesh->index_count;
        cmd.model_matrix[0] = cmd.model_matrix[5] = cmd.model_matrix[10] =
            cmd.model_matrix[15] = 1.0f;
        cmd.diffuse_color[0] = (float)mat->diffuse[0];
        cmd.diffuse_color[1] = (float)mat->diffuse[1];
        cmd.diffuse_color[2] = (float)mat->diffuse[2];
        cmd.diffuse_color[3] = (float)mat->diffuse[3];
        cmd.alpha = (float)mat->alpha;
        cmd.unlit = mat->unlit;
        cmd.texture = mat->texture;

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
            c->backend_ctx, c->gfx_win, &cmd, v->visible_transforms,
            v->visible_count, lp, lc, c->ambient, c->wireframe,
            c->backface_cull);
    } else {
        /* Software fallback: draw each blade individually */
        for (int32_t i = 0; i < v->visible_count; i++) {
            float *src = &v->visible_transforms[i * 16];
            vgfx3d_draw_cmd_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.vertices = mesh->vertices;
            cmd.vertex_count = mesh->vertex_count;
            cmd.indices = mesh->indices;
            cmd.index_count = mesh->index_count;
            memcpy(cmd.model_matrix, src, 16 * sizeof(float));
            cmd.diffuse_color[0] = (float)mat->diffuse[0];
            cmd.diffuse_color[1] = (float)mat->diffuse[1];
            cmd.diffuse_color[2] = (float)mat->diffuse[2];
            cmd.diffuse_color[3] = (float)mat->diffuse[3];
            cmd.alpha = (float)mat->alpha;
            cmd.unlit = mat->unlit;
            cmd.texture = mat->texture;

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
            c->backend->submit_draw(c->backend_ctx, c->gfx_win, &cmd, lp, lc,
                                     c->ambient, c->wireframe,
                                     c->backface_cull);
        }
    }

    c->backface_cull = prev_cull;
}

#endif /* VIPER_ENABLE_GRAPHICS */
