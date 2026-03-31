//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_decal3d.c
// Purpose: 3D decals — surface-aligned textured quads with lifetime/fade.
//
// Key invariants:
//   - Quad is built from position + normal using arbitrary tangent frame.
//   - Offset 0.01 along normal prevents z-fighting with surface.
//   - Alpha fades linearly over last 20% of lifetime.
//
// Links: rt_decal3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_decal3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int32_t rt_obj_release_check0(void *p);
extern void rt_obj_free(void *p);
extern void rt_trap(const char *msg);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);

typedef struct {
    void *vptr;
    double position[3];
    double normal[3];
    double size;
    void *texture;
    double lifetime;
    double max_lifetime;
    double alpha;
    void *mesh;     /* built on first draw */
    void *material; /* built on first draw */
} rt_decal3d;

static void decal3d_finalizer(void *obj) {
    rt_decal3d *d = (rt_decal3d *)obj;
    if (d->mesh) {
        if (rt_obj_release_check0(d->mesh))
            rt_obj_free(d->mesh);
        d->mesh = NULL;
    }
    if (d->material) {
        if (rt_obj_release_check0(d->material))
            rt_obj_free(d->material);
        d->material = NULL;
    }
}

void *rt_decal3d_new(void *pos_v, void *normal_v, double size, void *texture) {
    if (!pos_v || !normal_v)
        return NULL;
    rt_decal3d *d = (rt_decal3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_decal3d));
    if (!d) {
        rt_trap("Decal3D.New: allocation failed");
        return NULL;
    }
    d->vptr = NULL;
    d->position[0] = rt_vec3_x(pos_v);
    d->position[1] = rt_vec3_y(pos_v);
    d->position[2] = rt_vec3_z(pos_v);
    d->normal[0] = rt_vec3_x(normal_v);
    d->normal[1] = rt_vec3_y(normal_v);
    d->normal[2] = rt_vec3_z(normal_v);
    d->size = size;
    d->texture = texture;
    d->lifetime = -1.0; /* permanent by default */
    d->max_lifetime = -1.0;
    d->alpha = 1.0;
    d->mesh = NULL;
    d->material = NULL;
    rt_obj_set_finalizer(d, decal3d_finalizer);
    return d;
}

void rt_decal3d_set_lifetime(void *obj, double seconds) {
    if (!obj)
        return;
    rt_decal3d *d = (rt_decal3d *)obj;
    d->lifetime = seconds;
    d->max_lifetime = seconds;
}

void rt_decal3d_update(void *obj, double dt) {
    if (!obj || dt <= 0)
        return;
    rt_decal3d *d = (rt_decal3d *)obj;
    if (d->lifetime < 0)
        return; /* permanent */
    d->lifetime -= dt;
    /* Fade alpha over last 20% of lifetime */
    if (d->max_lifetime > 0 && d->lifetime < d->max_lifetime * 0.2) {
        d->alpha = d->lifetime / (d->max_lifetime * 0.2);
        if (d->alpha < 0.0)
            d->alpha = 0.0;
    }
}

int8_t rt_decal3d_is_expired(void *obj) {
    if (!obj)
        return 1;
    rt_decal3d *d = (rt_decal3d *)obj;
    if (d->lifetime < 0)
        return 0; /* permanent */
    return d->lifetime <= 0 ? 1 : 0;
}

/// @brief Build the decal quad mesh on first draw.
static void ensure_decal_mesh(rt_decal3d *d) {
    if (d->mesh)
        return;

    /* Build tangent frame from normal */
    double nx = d->normal[0], ny = d->normal[1], nz = d->normal[2];

    /* Choose arbitrary up vector not parallel to normal */
    double ux = 0.0, uy = 1.0, uz = 0.0;
    if (fabs(ny) > 0.9) {
        ux = 1.0;
        uy = 0.0;
        uz = 0.0;
    }

    /* Right = cross(up, normal) */
    double rx = uy * nz - uz * ny;
    double ry = uz * nx - ux * nz;
    double rz = ux * ny - uy * nx;
    double rlen = sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen > 1e-8) {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
    }

    /* True up = cross(normal, right) */
    double tux = ny * rz - nz * ry;
    double tuy = nz * rx - nx * rz;
    double tuz = nx * ry - ny * rx;

    double hs = d->size * 0.5; /* half-size */
    double off = 0.01;         /* surface offset to prevent z-fighting */
    double cx = d->position[0] + nx * off;
    double cy = d->position[1] + ny * off;
    double cz = d->position[2] + nz * off;

    d->mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(d->mesh,
                         cx - rx * hs - tux * hs,
                         cy - ry * hs - tuy * hs,
                         cz - rz * hs - tuz * hs,
                         nx,
                         ny,
                         nz,
                         0.0,
                         0.0);
    rt_mesh3d_add_vertex(d->mesh,
                         cx + rx * hs - tux * hs,
                         cy + ry * hs - tuy * hs,
                         cz + rz * hs - tuz * hs,
                         nx,
                         ny,
                         nz,
                         1.0,
                         0.0);
    rt_mesh3d_add_vertex(d->mesh,
                         cx + rx * hs + tux * hs,
                         cy + ry * hs + tuy * hs,
                         cz + rz * hs + tuz * hs,
                         nx,
                         ny,
                         nz,
                         1.0,
                         1.0);
    rt_mesh3d_add_vertex(d->mesh,
                         cx - rx * hs + tux * hs,
                         cy - ry * hs + tuy * hs,
                         cz - rz * hs + tuz * hs,
                         nx,
                         ny,
                         nz,
                         0.0,
                         1.0);
    rt_mesh3d_add_triangle(d->mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(d->mesh, 0, 2, 3);

    /* Create material with texture and alpha */
    extern void *rt_material3d_new(void);
    extern void rt_material3d_set_texture(void *m, void *tex);
    extern void rt_material3d_set_alpha(void *m, double a);
    extern void rt_material3d_set_unlit(void *m, int8_t u);
    d->material = rt_material3d_new();
    if (d->texture)
        rt_material3d_set_texture(d->material, d->texture);
    rt_material3d_set_alpha(d->material, d->alpha);
    rt_material3d_set_unlit(d->material, 1); /* decals are unlit — they show texture only */
}

void rt_canvas3d_draw_decal(void *canvas, void *obj) {
    if (!canvas || !obj)
        return;
    rt_decal3d *d = (rt_decal3d *)obj;
    if (d->lifetime >= 0 && d->lifetime <= 0)
        return; /* expired */

    ensure_decal_mesh(d);
    if (!d->mesh || !d->material)
        return;

    /* Update material alpha for fade */
    extern void rt_material3d_set_alpha(void *m, double a);
    rt_material3d_set_alpha(d->material, d->alpha);

    rt_canvas3d_draw_mesh(canvas, d->mesh, rt_mat4_identity(), d->material);
}

#endif /* VIPER_ENABLE_GRAPHICS */
