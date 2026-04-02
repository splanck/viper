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
//
// Links: rt_sprite3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_sprite3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_clear(void *m);
extern void rt_mesh3d_add_vertex(
    void *m, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *m, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mat4_identity(void);
extern void rt_canvas3d_draw_mesh(void *canvas, void *mesh, void *transform, void *material);
extern void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
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

static void sprite3d_finalizer(void *obj) {
    (void)obj;
}

/// @brief Create a 3D billboard sprite that always faces the camera.
/// @details Sprites are textured quads rendered in 3D space that orient toward
///          the camera using its view matrix right/up vectors. Commonly used for
///          particles, distant trees, UI indicators, etc. The sprite supports
///          spritesheet frames (SetFrame), anchor point control, and non-uniform scale.
/// @param texture Pixels handle for the sprite image (borrowed, not owned).
/// @return Opaque sprite handle, or NULL on failure.
void *rt_sprite3d_new(void *texture) {
    rt_sprite3d *s = (rt_sprite3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sprite3d));
    if (!s) {
        rt_trap("Sprite3D.New: allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->texture = texture;
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
        s->tex_w = (int32_t)rt_pixels_width(texture);
        s->tex_h = (int32_t)rt_pixels_height(texture);
        s->frame_w = s->tex_w;
        s->frame_h = s->tex_h;
    }

    rt_obj_set_finalizer(s, sprite3d_finalizer);
    return s;
}

/// @brief Set the world-space position where the sprite is rendered.
void rt_sprite3d_set_position(void *obj, double x, double y, double z) {
    if (!obj)
        return;
    rt_sprite3d *s = (rt_sprite3d *)obj;
    s->position[0] = x;
    s->position[1] = y;
    s->position[2] = z;
}

/// @brief Set the width and height scale of the sprite in world units.
void rt_sprite3d_set_scale(void *obj, double w, double h) {
    if (!obj)
        return;
    rt_sprite3d *s = (rt_sprite3d *)obj;
    s->scale_wh[0] = w;
    s->scale_wh[1] = h;
}

/// @brief Set the anchor point (0,0 = bottom-left, 0.5,0.5 = center, 1,1 = top-right).
void rt_sprite3d_set_anchor(void *obj, double ax, double ay) {
    if (!obj)
        return;
    rt_sprite3d *s = (rt_sprite3d *)obj;
    s->anchor[0] = ax;
    s->anchor[1] = ay;
}

/// @brief Set the spritesheet sub-rectangle to display (for animated sprites).
void rt_sprite3d_set_frame(void *obj, int64_t fx, int64_t fy, int64_t fw, int64_t fh) {
    if (!obj)
        return;
    rt_sprite3d *s = (rt_sprite3d *)obj;
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
    rt_sprite3d *s = (rt_sprite3d *)obj;
    rt_camera3d *cam = (rt_camera3d *)camera;

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
    if (!s->cached_material || s->cached_texture != s->texture) {
        s->cached_material = rt_material3d_new();
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
                         nx, ny, nz, u0, v1);
    rt_mesh3d_add_vertex(mesh,
                         cx + rx * hw - ux * hh,
                         cy + ry * hw - uy * hh,
                         cz + rz * hw - uz * hh,
                         nx, ny, nz, u1, v1);
    rt_mesh3d_add_vertex(mesh,
                         cx + rx * hw + ux * hh,
                         cy + ry * hw + uy * hh,
                         cz + rz * hw + uz * hh,
                         nx, ny, nz, u1, v0);
    rt_mesh3d_add_vertex(mesh,
                         cx - rx * hw + ux * hh,
                         cy - ry * hw + uy * hh,
                         cz - rz * hw + uz * hh,
                         nx, ny, nz, u0, v0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);

    /* Register with canvas temp buffer list so GC doesn't collect mid-frame */
    rt_canvas3d_add_temp_buffer(canvas, mesh);
    rt_canvas3d_add_temp_buffer(canvas, s->cached_material);

    rt_canvas3d_draw_mesh(canvas, mesh, rt_mat4_identity(), s->cached_material);
}

#endif /* VIPER_ENABLE_GRAPHICS */
