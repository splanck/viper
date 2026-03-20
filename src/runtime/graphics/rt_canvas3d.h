//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_canvas3d.h
// Purpose: Runtime bridge for Viper.Graphics3D — Canvas3D, Mesh3D, Camera3D,
//   Material3D, and Light3D types. Provides 3D rendering via a software
//   rasterizer (Phase 1) with GPU backend abstraction (Phase 2+).
//
// Key invariants:
//   - All object pointers are opaque handles returned by *_new functions.
//   - Canvas3D.Begin/End must bracket all DrawMesh calls (no nesting).
//   - All math stays double precision at API boundary; rasterizer uses float.
//   - Counter-clockwise (CCW) winding is front-facing.
//
// Ownership/Lifetime:
//   - Canvas3D owns the 3D rendering context; GC finalizer cleans up.
//   - Mesh3D owns vertex/index arrays; GC finalizer frees them.
//   - Camera3D, Material3D, Light3D contain only scalar fields (no finalizer).
//
// Links: plans/3d/01-software-renderer.md, src/runtime/graphics/rt_graphics.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Canvas3D — 3D rendering surface
    //=========================================================================

    void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h);
    void rt_canvas3d_clear(void *obj, double r, double g, double b);
    void rt_canvas3d_begin(void *obj, void *camera);
    void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
    void rt_canvas3d_end(void *obj);
    void rt_canvas3d_flip(void *obj);
    int64_t rt_canvas3d_poll(void *obj);
    int8_t rt_canvas3d_should_close(void *obj);
    void rt_canvas3d_set_wireframe(void *obj, int8_t enabled);
    void rt_canvas3d_set_backface_cull(void *obj, int8_t enabled);
    int64_t rt_canvas3d_get_width(void *obj);
    int64_t rt_canvas3d_get_height(void *obj);
    int64_t rt_canvas3d_get_fps(void *obj);
    int64_t rt_canvas3d_get_delta_time(void *obj);
    void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms);
    void rt_canvas3d_set_light(void *obj, int64_t index, void *light);
    void rt_canvas3d_set_ambient(void *obj, double r, double g, double b);
    void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color);
    void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size);
    rt_string rt_canvas3d_get_backend(void *obj);
    void *rt_canvas3d_screenshot(void *obj);

    //=========================================================================
    // CubeMap3D — 6-face cube map for skybox + reflections
    //=========================================================================

    void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz);
    void rt_canvas3d_set_skybox(void *canvas, void *cubemap);
    void rt_canvas3d_clear_skybox(void *canvas);
    void rt_material3d_set_env_map(void *obj, void *cubemap);
    void rt_material3d_set_reflectivity(void *obj, double r);
    double rt_material3d_get_reflectivity(void *obj);

    //=========================================================================
    // RenderTarget3D — offscreen rendering target
    //=========================================================================

    void *rt_rendertarget3d_new(int64_t width, int64_t height);
    int64_t rt_rendertarget3d_get_width(void *obj);
    int64_t rt_rendertarget3d_get_height(void *obj);
    void *rt_rendertarget3d_as_pixels(void *obj);

    void rt_canvas3d_set_render_target(void *canvas, void *target);
    void rt_canvas3d_reset_render_target(void *canvas);

    //=========================================================================
    // Mesh3D — 3D mesh with vertices and triangle indices
    //=========================================================================

    void *rt_mesh3d_new(void);
    void *rt_mesh3d_new_box(double sx, double sy, double sz);
    void *rt_mesh3d_new_sphere(double radius, int64_t segments);
    void *rt_mesh3d_new_plane(double sx, double sz);
    void *rt_mesh3d_new_cylinder(double r, double h, int64_t segments);
    void *rt_mesh3d_from_obj(rt_string path);
    int64_t rt_mesh3d_get_vertex_count(void *obj);
    int64_t rt_mesh3d_get_triangle_count(void *obj);
    void rt_mesh3d_add_vertex(void *obj,
                              double x,
                              double y,
                              double z,
                              double nx,
                              double ny,
                              double nz,
                              double u,
                              double v);
    void rt_mesh3d_add_triangle(void *obj, int64_t v0, int64_t v1, int64_t v2);
    void rt_mesh3d_recalc_normals(void *obj);
    void *rt_mesh3d_clone(void *obj);
    void rt_mesh3d_transform(void *obj, void *mat4);
    void rt_mesh3d_calc_tangents(void *obj);

    //=========================================================================
    // Camera3D — perspective camera with view/projection matrices
    //=========================================================================

    void *rt_camera3d_new(double fov, double aspect, double near_val, double far_val);
    void rt_camera3d_look_at(void *obj, void *eye, void *target, void *up);
    void rt_camera3d_orbit(void *obj, void *target, double distance, double yaw, double pitch);
    double rt_camera3d_get_fov(void *obj);
    void rt_camera3d_set_fov(void *obj, double fov);
    void *rt_camera3d_get_position(void *obj);
    void rt_camera3d_set_position(void *obj, void *pos);
    void *rt_camera3d_get_forward(void *obj);
    void *rt_camera3d_get_right(void *obj);
    void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh);

    //=========================================================================
    // Material3D — surface appearance (color, texture, shininess)
    //=========================================================================

    void *rt_material3d_new(void);
    void *rt_material3d_new_color(double r, double g, double b);
    void *rt_material3d_new_textured(void *pixels);
    void rt_material3d_set_color(void *obj, double r, double g, double b);
    void rt_material3d_set_texture(void *obj, void *pixels);
    void rt_material3d_set_shininess(void *obj, double s);
    void rt_material3d_set_unlit(void *obj, int8_t unlit);
    void rt_material3d_set_alpha(void *obj, double alpha);
    double rt_material3d_get_alpha(void *obj);
    void rt_material3d_set_normal_map(void *obj, void *pixels);
    void rt_material3d_set_specular_map(void *obj, void *pixels);
    void rt_material3d_set_emissive_map(void *obj, void *pixels);
    void rt_material3d_set_emissive_color(void *obj, double r, double g, double b);

    //=========================================================================
    // Light3D — directional, point, or ambient light source
    //=========================================================================

    void *rt_light3d_new_directional(void *direction, double r, double g, double b);
    void *rt_light3d_new_point(void *position, double r, double g, double b, double attenuation);
    void *rt_light3d_new_ambient(double r, double g, double b);
    void rt_light3d_set_intensity(void *obj, double intensity);
    void rt_light3d_set_color(void *obj, double r, double g, double b);

    /// @brief Register a temporary buffer to be freed at the end of the current frame.
    void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

    /* Screen-space HUD overlay */
    void rt_canvas3d_draw_rect2d(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
    void rt_canvas3d_draw_crosshair(void *canvas, int64_t color, int64_t size);
    void rt_canvas3d_draw_text2d(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color);

    /* Debug gizmos */
    void rt_canvas3d_draw_aabb_wire(void *canvas, void *min_v, void *max_v, int64_t color);
    void rt_canvas3d_draw_sphere_wire(void *canvas, void *center, double radius, int64_t color);
    void rt_canvas3d_draw_debug_ray(void *canvas, void *origin, void *dir,
                                     double length, int64_t color);
    void rt_canvas3d_draw_axis(void *canvas, void *origin, double scale);

    /* Fog */
    void rt_canvas3d_set_fog(void *canvas, double near_dist, double far_dist,
                              double r, double g, double b);
    void rt_canvas3d_clear_fog(void *canvas);

    /* Shadows */
    void rt_canvas3d_enable_shadows(void *canvas, int64_t resolution);
    void rt_canvas3d_disable_shadows(void *canvas);
    void rt_canvas3d_set_shadow_bias(void *canvas, double bias);

    /* Occlusion culling */
    void rt_canvas3d_set_occlusion_culling(void *canvas, int8_t enabled);

    /* Instanced rendering + Terrain */
    void rt_canvas3d_draw_instanced(void *canvas, void *batch);
    void rt_canvas3d_draw_terrain(void *canvas, void *terrain);

    /* Camera shake + smooth follow */
    void rt_camera3d_shake(void *cam, double intensity, double duration, double decay);
    void rt_camera3d_smooth_follow(void *cam, void *target, double distance,
                                    double height, double speed, double dt);
    void rt_camera3d_smooth_look_at(void *cam, void *target, double speed, double dt);

    /* FPS camera */
    void rt_camera3d_fps_init(void *cam);
    void rt_camera3d_fps_update(void *cam,
                                double yaw_delta,
                                double pitch_delta,
                                double move_fwd,
                                double move_right,
                                double move_up,
                                double speed,
                                double dt);
    double rt_camera3d_get_yaw(void *cam);
    double rt_camera3d_get_pitch(void *cam);
    void rt_camera3d_set_yaw(void *cam, double yaw);
    void rt_camera3d_set_pitch(void *cam, double pitch);

#ifdef __cplusplus
}
#endif
