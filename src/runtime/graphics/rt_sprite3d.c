//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_sprite3d.c
// Purpose: 3D sprite — camera-facing billboard with sprite sheet frames.
//
// Key invariants:
//   - Billboard computed from camera view matrix right/up vectors.
//   - Quad built per-frame (4 verts, 2 tris) with UV from frame rect.
//   - Anchor offset applied before billboard expansion.
//   - Material is cached and only rebuilt when the texture changes.
//
// Ownership/Lifetime:
//   - Sprite3D is GC-managed; finalizer releases texture, mesh, and material.
//   - Per-frame mesh is parked on the canvas's temp-object queue.
//
// Links: rt_sprite3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_sprite3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_clear(void *m);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void rt_canvas3d_draw_mesh_matrix(
    void *canvas, void *mesh, const double *transform, void *material);
extern void rt_canvas3d_add_temp_object(void *canvas, void *value);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_texture(void *m, void *tex);
extern void rt_material3d_set_unlit(void *m, int8_t u);

typedef struct {
    void *vptr;
    void *texture;
    double position[3];
    double scale_wh[2]; /* width, height in world units */
    double anchor[2];   /* pivot [0,1], default (0.5, 0.5) */
    int32_t frame_x, frame_y, frame_w, frame_h;
    int32_t tex_w, tex_h;
    void *cached_mesh;     /* Reused each frame (billboard changes with camera) */
    void *cached_material; /* Created once, reused until texture changes */
    void *cached_texture;  /* Track texture for material invalidation */
} rt_sprite3d;

/// @brief Release a GC-tracked reference slot and null it.
/// @details Used for the three cached refs (`texture`, `cached_mesh`, `cached_material`)
///   when the sprite is finalized or when the material needs to be rebuilt because the
///   texture changed. Nulling the slot makes subsequent calls idempotent.
static void sprite3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Return @p value when finite, else @p fallback. Sanitizes Vec3 inputs.
static double sprite3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp @p value to [0, 1], substituting 0.5 for non-finite input. Used for anchor coords.
static double sprite3d_clamp01(double value) {
    if (!isfinite(value))
        return 0.5;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief GC finalizer — release the texture, billboard mesh, and cached material.
/// @details Sprite3D lazily caches three dependent objects: the billboard
///   mesh (regenerated per frame to face the camera), the material
///   (valid until the texture changes), and the source texture itself.
///   All three are ref-counted — releasing is safe even if other
///   objects still hold the texture. `cached_texture` is only a
///   tracking pointer (not a retained ref) so it's just nulled, not
///   released — the actual release happened through `texture`.
static void sprite3d_finalizer(void *obj) {
    rt_sprite3d *s = (rt_sprite3d *)obj;
    if (!s)
        return;
    sprite3d_release_ref(&s->texture);
    sprite3d_release_ref(&s->cached_mesh);
    sprite3d_release_ref(&s->cached_material);
    s->cached_texture = NULL;
}

/// @brief Create a 3D billboard sprite that always faces the camera.
/// @details Sprites are textured quads rendered in 3D space that orient toward
///          the camera using its view matrix right/up vectors. Commonly used for
///          particles, distant trees, UI indicators, etc. The sprite supports
///          spritesheet frames (SetFrame), anchor point control, and non-uniform scale.
/// @param texture Pixels handle for the sprite image. The sprite retains it so
///        lazy billboard material creation stays valid across frames.
/// @return Opaque sprite handle, or NULL on failure.
void *rt_sprite3d_new(void *texture) {
    rt_sprite3d *s =
        (rt_sprite3d *)rt_obj_new_i64(RT_G3D_SPRITE3D_CLASS_ID, (int64_t)sizeof(rt_sprite3d));
    if (!s) {
        rt_trap("Sprite3D.New: allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->texture = texture;
    rt_obj_retain_maybe(texture);
    s->position[0] = s->position[1] = s->position[2] = 0.0;
    s->scale_wh[0] = 1.0;
    s->scale_wh[1] = 1.0;
    s->anchor[0] = 0.5;
    s->anchor[1] = 0.5;
    s->frame_x = 0;
    s->frame_y = 0;
    s->frame_w = 0;
    s->frame_h = 0; /* 0 = use full texture */
    s->tex_w = 0;
    s->tex_h = 0;
    s->cached_mesh = NULL;
    s->cached_material = NULL;
    s->cached_texture = NULL;

    /* Try to get texture dimensions */
    if (texture) {
        rt_pixels_impl *pixels =
            rt_pixels_checked_impl(texture, "Sprite3D.New: expected Pixels texture");
        if (!pixels || pixels->width <= 0 || pixels->height <= 0 || pixels->width > INT32_MAX ||
            pixels->height > INT32_MAX) {
            sprite3d_release_ref(&s->texture);
            if (rt_obj_release_check0(s))
                rt_obj_free(s);
            rt_trap("Sprite3D.New: texture must be non-empty Pixels");
            return NULL;
        }
        s->tex_w = (int32_t)pixels->width;
        s->tex_h = (int32_t)pixels->height;
        s->frame_w = s->tex_w;
        s->frame_h = s->tex_h;
    }

    rt_obj_set_finalizer(s, sprite3d_finalizer);
    return s;
}

/// @brief Set the world-space position where the sprite is rendered.
void rt_sprite3d_set_position(void *obj, double x, double y, double z) {
    rt_sprite3d *s = (rt_sprite3d *)rt_g3d_checked_or_null(obj, RT_G3D_SPRITE3D_CLASS_ID);
    if (!s)
        return;
    s->position[0] = sprite3d_finite_or(x, 0.0);
    s->position[1] = sprite3d_finite_or(y, 0.0);
    s->position[2] = sprite3d_finite_or(z, 0.0);
}

/// @brief Set the width and height scale of the sprite in world units.
void rt_sprite3d_set_scale(void *obj, double w, double h) {
    rt_sprite3d *s = (rt_sprite3d *)rt_g3d_checked_or_null(obj, RT_G3D_SPRITE3D_CLASS_ID);
    if (!s)
        return;
    w = sprite3d_finite_or(w, 1.0);
    h = sprite3d_finite_or(h, 1.0);
    s->scale_wh[0] = w > 0.0 ? w : 1.0;
    s->scale_wh[1] = h > 0.0 ? h : 1.0;
}

/// @brief Set the anchor point (0,0 = bottom-left, 0.5,0.5 = center, 1,1 = top-right).
void rt_sprite3d_set_anchor(void *obj, double ax, double ay) {
    rt_sprite3d *s = (rt_sprite3d *)rt_g3d_checked_or_null(obj, RT_G3D_SPRITE3D_CLASS_ID);
    if (!s)
        return;
    s->anchor[0] = sprite3d_clamp01(ax);
    s->anchor[1] = sprite3d_clamp01(ay);
}

/// @brief Set the spritesheet sub-rectangle to display (for animated sprites).
void rt_sprite3d_set_frame(void *obj, int64_t fx, int64_t fy, int64_t fw, int64_t fh) {
    rt_sprite3d *s = (rt_sprite3d *)rt_g3d_checked_or_null(obj, RT_G3D_SPRITE3D_CLASS_ID);
    if (!s)
        return;
    if (fx < 0)
        fx = 0;
    if (fy < 0)
        fy = 0;
    if (fw <= 0)
        fw = s->tex_w > 0 ? s->tex_w : 1;
    if (fh <= 0)
        fh = s->tex_h > 0 ? s->tex_h : 1;
    if (fx > INT32_MAX)
        fx = INT32_MAX;
    if (fy > INT32_MAX)
        fy = INT32_MAX;
    if (fw > INT32_MAX)
        fw = INT32_MAX;
    if (fh > INT32_MAX)
        fh = INT32_MAX;
    if (s->tex_w > 0) {
        if (fx >= s->tex_w)
            fx = s->tex_w - 1;
        if (fw > s->tex_w - fx)
            fw = s->tex_w - fx;
    }
    if (s->tex_h > 0) {
        if (fy >= s->tex_h)
            fy = s->tex_h - 1;
        if (fh > s->tex_h - fy)
            fh = s->tex_h - fy;
    }
    s->frame_x = (int32_t)fx;
    s->frame_y = (int32_t)fy;
    s->frame_w = (int32_t)fw;
    s->frame_h = (int32_t)fh;
}

/// @brief Draw a 3D sprite as a camera-facing billboard on the canvas.
/// @details Constructs a billboard quad each frame using the camera's right and
///          up vectors, applies the anchor offset, and renders as a textured mesh.
///          The quad geometry is cached and reused between frames.
void rt_canvas3d_draw_sprite3d(void *canvas, void *obj, void *camera) {
    if (!canvas || !obj || !camera)
        return;
    rt_sprite3d *s = (rt_sprite3d *)rt_g3d_checked_or_null(obj, RT_G3D_SPRITE3D_CLASS_ID);
    rt_camera3d *cam = rt_camera3d_checked_or_stack(camera);
    if (!s || !cam)
        return;

    /* Extract right and up vectors from camera view matrix (row-major) */
    double rx = cam->view[0], ry = cam->view[1], rz = cam->view[2];
    double ux = cam->view[4], uy = cam->view[5], uz = cam->view[6];

    double hw = s->scale_wh[0] * 0.5;
    double hh = s->scale_wh[1] * 0.5;

    /* Anchor offset: shift center by (0.5 - anchor) in each axis */
    double ax = (0.5 - s->anchor[0]) * s->scale_wh[0];
    double ay = (0.5 - s->anchor[1]) * s->scale_wh[1];
    double cx = s->position[0] + rx * ax + ux * ay;
    double cy = s->position[1] + ry * ax + uy * ay;
    double cz = s->position[2] + rz * ax + uz * ay;

    /* UV from frame rect */
    double u0 = 0.0, v0 = 0.0, u1 = 1.0, v1 = 1.0;
    if (s->tex_w > 0 && s->tex_h > 0) {
        u0 = (double)s->frame_x / s->tex_w;
        v0 = (double)s->frame_y / s->tex_h;
        u1 = (double)(s->frame_x + s->frame_w) / s->tex_w;
        v1 = (double)(s->frame_y + s->frame_h) / s->tex_h;
    }

    /* Lazily create cached mesh and material (once, reused every frame) */
    if (!s->cached_mesh)
        s->cached_mesh = rt_mesh3d_new();
    if (!s->cached_mesh)
        return;
    if (!s->cached_material || s->cached_texture != s->texture) {
        sprite3d_release_ref(&s->cached_material);
        s->cached_material = rt_material3d_new();
        if (!s->cached_material) {
            s->cached_texture = NULL;
            return;
        }
        if (s->texture)
            rt_material3d_set_texture(s->cached_material, s->texture);
        rt_material3d_set_unlit(s->cached_material, 1);
        s->cached_texture = s->texture;
    }

    /* Rebuild billboard quad each frame (orientation changes with camera).
       Clear resets vertex/index counts without freeing the backing arrays. */
    void *mesh = s->cached_mesh;
    rt_mesh3d_clear(mesh);

    double nx = -(cam->view[8]), ny = -(cam->view[9]), nz = -(cam->view[10]); /* face camera */

    rt_mesh3d_add_vertex(mesh,
                         cx - rx * hw - ux * hh,
                         cy - ry * hw - uy * hh,
                         cz - rz * hw - uz * hh,
                         nx,
                         ny,
                         nz,
                         u0,
                         v1);
    rt_mesh3d_add_vertex(mesh,
                         cx + rx * hw - ux * hh,
                         cy + ry * hw - uy * hh,
                         cz + rz * hw - uz * hh,
                         nx,
                         ny,
                         nz,
                         u1,
                         v1);
    rt_mesh3d_add_vertex(mesh,
                         cx + rx * hw + ux * hh,
                         cy + ry * hw + uy * hh,
                         cz + rz * hw + uz * hh,
                         nx,
                         ny,
                         nz,
                         u1,
                         v0);
    rt_mesh3d_add_vertex(mesh,
                         cx - rx * hw + ux * hh,
                         cy - ry * hw + uy * hh,
                         cz - rz * hw + uz * hh,
                         nx,
                         ny,
                         nz,
                         u0,
                         v0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);

    /* Keep cached runtime objects alive until the deferred frame submission completes. */
    rt_canvas3d_add_temp_object(canvas, mesh);
    rt_canvas3d_add_temp_object(canvas, s->cached_material);

    {
        static const double identity[16] = {
            1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
        };
        rt_canvas3d_draw_mesh_matrix(canvas, mesh, identity, s->cached_material);
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
