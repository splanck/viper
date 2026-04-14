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
extern void rt_obj_retain_maybe(void *obj);
extern int32_t rt_obj_release_check0(void *p);
extern void rt_obj_free(void *p);
#include "rt_trap.h"
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

static void decal3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void decal3d_finalizer(void *obj) {
    rt_decal3d *d = (rt_decal3d *)obj;
    decal3d_release_ref(&d->texture);
    decal3d_release_ref(&d->mesh);
    decal3d_release_ref(&d->material);
}

/// @brief Create a new 3D decal projected onto a surface.
/// @details Decals are flat quads oriented along a surface normal, used for
///          bullet holes, blood splatters, scorch marks, etc. The quad mesh is
///          lazily built on first draw from the position, normal, and size. If
///          a lifetime is set, the decal fades out over its last 20% and can be
///          checked with rt_decal3d_is_expired.
/// @param pos_v    Vec3 world-space position on the surface.
/// @param normal_v Vec3 surface normal at the decal location.
/// @param size     Width/height of the decal quad in world units.
/// @param texture  Pixels handle for the decal image. The decal retains it so
///        lazy material creation stays valid across frames.
/// @return Opaque decal handle, or NULL on failure.
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
    rt_obj_retain_maybe(texture);
    d->lifetime = -1.0; /* permanent by default */
    d->max_lifetime = -1.0;
    d->alpha = 1.0;
    d->mesh = NULL;
    d->material = NULL;
    rt_obj_set_finalizer(d, decal3d_finalizer);
    return d;
}

/// @brief Set how long the decal should live before expiring (< 0 = permanent).
void rt_decal3d_set_lifetime(void *obj, double seconds) {
    if (!obj)
        return;
    rt_decal3d *d = (rt_decal3d *)obj;
    d->lifetime = seconds;
    d->max_lifetime = seconds;
}

/// @brief Advance the decal's lifetime timer and apply fade-out in the last 20%.
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

/// @brief Check if the decal's lifetime has elapsed (permanent decals never expire).
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

/// @brief Render a decal onto the canvas (no-op for expired decals).
/// Lazily builds the quad mesh and material on first draw, then re-uses them.
/// Material alpha is refreshed each frame to reflect the current fade level.
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

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
