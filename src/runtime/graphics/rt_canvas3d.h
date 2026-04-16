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
extern "C" {
#endif

//=========================================================================
// Canvas3D — 3D rendering surface
//=========================================================================

/// @brief Create a new 3D canvas window with the given title and pixel dimensions.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h);
/// @brief Clear the back buffer to the given RGB color (each channel 0.0–1.0).
void rt_canvas3d_clear(void *obj, double r, double g, double b);
/// @brief Begin a 3D draw pass with the given camera (must be paired with `_end`).
void rt_canvas3d_begin(void *obj, void *camera);
/// @brief Begin a 2D screen-space overlay pass (orthographic, no depth test).
void rt_canvas3d_begin_2d(void *obj);
/// @brief Draw a screen-space filled rectangle inside the current 2D pass.
void rt_canvas3d_draw_rect_3d(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
/// @brief Draw screen-space text inside the current 2D pass.
void rt_canvas3d_draw_text_3d(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color);
/// @brief Submit a mesh+transform+material draw inside the active 3D pass.
void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
/// @brief End the current 3D or 2D pass and flush queued draws.
void rt_canvas3d_end(void *obj);
/// @brief Present the back buffer to the window (vsync-controlled by backend).
void rt_canvas3d_flip(void *obj);
/// @brief Pump events and update per-frame input state.
/// @return Event type code of the last event processed, or 0 if no events.
int64_t rt_canvas3d_poll(void *obj);
/// @brief True when the user has requested window close (clicked X or Alt-F4).
int8_t rt_canvas3d_should_close(void *obj);
/// @brief Toggle wireframe rendering for subsequent mesh draws.
void rt_canvas3d_set_wireframe(void *obj, int8_t enabled);
/// @brief Toggle backface culling (CCW = front-facing).
void rt_canvas3d_set_backface_cull(void *obj, int8_t enabled);
/// @brief Get the canvas width in pixels.
int64_t rt_canvas3d_get_width(void *obj);
/// @brief Get the canvas height in pixels.
int64_t rt_canvas3d_get_height(void *obj);
/// @brief Get the rolling-average FPS measured over recent frames.
int64_t rt_canvas3d_get_fps(void *obj);
/// @brief Get the wall-clock milliseconds since the previous Flip (first frame returns 0).
int64_t rt_canvas3d_get_delta_time(void *obj);
/// @brief Cap the per-frame delta time (smooths spikes after pause/breakpoint).
void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms);
/// @brief Bind or clear a Light3D slot; the canvas retains the assigned light until replaced.
void rt_canvas3d_set_light(void *obj, int64_t index, void *light);
/// @brief Set the ambient light color applied to all lit materials.
void rt_canvas3d_set_ambient(void *obj, double r, double g, double b);
/// @brief Draw a debug 3D line between two Vec3 points.
void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color);
/// @brief Draw a debug 3D point (square) at the given position with pixel size.
void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size);
/// @brief Get the active backend name ("d3d11", "metal", "opengl", or "software").
rt_string rt_canvas3d_get_backend(void *obj);
/// @brief Capture the current back-buffer contents into a fresh Pixels object.
void *rt_canvas3d_screenshot(void *obj);

//=========================================================================
// CubeMap3D — 6-face cube map for skybox + reflections
//=========================================================================

/// @brief Build a cubemap from six Pixels faces (px = +X, nx = -X, etc.).
void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz);
/// @brief Bind a cubemap as the scene skybox (rendered behind opaque geometry).
void rt_canvas3d_set_skybox(void *canvas, void *cubemap);
/// @brief Remove the skybox; subsequent clears use the regular clear color.
void rt_canvas3d_clear_skybox(void *canvas);
/// @brief Bind a cubemap as a material's environment reflection map.
void rt_material3d_set_env_map(void *obj, void *cubemap);
/// @brief Set how strongly the material reflects its env map (0.0–1.0).
void rt_material3d_set_reflectivity(void *obj, double r);
/// @brief Read the current reflectivity scalar.
double rt_material3d_get_reflectivity(void *obj);

//=========================================================================
// RenderTarget3D — offscreen rendering target
//=========================================================================

/// @brief Allocate an offscreen render target of the given pixel dimensions.
void *rt_rendertarget3d_new(int64_t width, int64_t height);
/// @brief Width of the render target in pixels.
int64_t rt_rendertarget3d_get_width(void *obj);
/// @brief Height of the render target in pixels.
int64_t rt_rendertarget3d_get_height(void *obj);
/// @brief Get a Pixels view of the render target's color attachment (CPU readback).
void *rt_rendertarget3d_as_pixels(void *obj);

/// @brief Redirect subsequent draws to @p target instead of the swapchain.
void rt_canvas3d_set_render_target(void *canvas, void *target);
/// @brief Restore the swapchain as the active draw target.
void rt_canvas3d_reset_render_target(void *canvas);

//=========================================================================
// Mesh3D — 3D mesh with vertices and triangle indices
//=========================================================================

/// @brief Allocate an empty Mesh3D (no vertices, no triangles).
void *rt_mesh3d_new(void);
/// @brief Reset vertex and index counts to 0 without freeing the backing arrays.
void rt_mesh3d_clear(void *obj);
/// @brief Build a unit-cube-style box mesh of size (sx, sy, sz) with normals and UVs.
void *rt_mesh3d_new_box(double sx, double sy, double sz);
/// @brief Build a UV-sphere mesh with the given radius and longitude segment count.
void *rt_mesh3d_new_sphere(double radius, int64_t segments);
/// @brief Build a flat XZ plane of size (sx, sz) facing +Y with a single quad.
void *rt_mesh3d_new_plane(double sx, double sz);
/// @brief Build a cylinder of radius @p r and height @p h with @p segments around the axis.
void *rt_mesh3d_new_cylinder(double r, double h, int64_t segments);
/// @brief Load a triangle mesh from a Wavefront .obj file (positions/normals/UVs).
void *rt_mesh3d_from_obj(rt_string path);
/// @brief Load a triangle mesh from a binary STL file (positions + per-face normals).
void *rt_mesh3d_from_stl(rt_string path);
/// @brief Number of unique vertices currently in the mesh.
int64_t rt_mesh3d_get_vertex_count(void *obj);
/// @brief Number of triangles currently in the mesh (== indices / 3).
int64_t rt_mesh3d_get_triangle_count(void *obj);
/// @brief Append a vertex with position, normal, and UV. Returns the new vertex index.
void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v);
/// @brief Append a triangle by referencing three previously-added vertex indices (CCW = front).
void rt_mesh3d_add_triangle(void *obj, int64_t v0, int64_t v1, int64_t v2);
/// @brief Recompute smooth per-vertex normals from triangle face normals (overwrites existing).
void rt_mesh3d_recalc_normals(void *obj);
/// @brief Deep copy the mesh (independent storage; safe to mutate the clone).
void *rt_mesh3d_clone(void *obj);
/// @brief Transform every vertex position (and rotate normals) by the given Mat4.
void rt_mesh3d_transform(void *obj, void *mat4);
/// @brief Compute per-vertex tangent vectors from UVs (required for normal mapping).
void rt_mesh3d_calc_tangents(void *obj);

//=========================================================================
// Camera3D — perspective camera with view/projection matrices
//=========================================================================

/// @brief Create a perspective camera (FOV in degrees, aspect = width/height, near/far clip planes).
void *rt_camera3d_new(double fov, double aspect, double near_val, double far_val);
/// @brief Create an orthographic camera (vertical world-units, aspect, near/far).
void *rt_camera3d_new_ortho(double size, double aspect, double near_val, double far_val);
/// @brief True if the camera was created via `_new_ortho` (no perspective foreshortening).
int8_t rt_camera3d_is_ortho(void *cam);
/// @brief Aim the camera at a target point with an explicit up direction.
void rt_camera3d_look_at(void *obj, void *eye, void *target, void *up);
/// @brief Position the camera on a sphere around @p target at the given yaw/pitch in degrees.
void rt_camera3d_orbit(void *obj, void *target, double distance, double yaw, double pitch);
/// @brief Get the field of view in degrees (perspective cameras only).
double rt_camera3d_get_fov(void *obj);
/// @brief Set the field of view in degrees.
void rt_camera3d_set_fov(void *obj, double fov);
/// @brief Get the camera world-space position as a Vec3.
void *rt_camera3d_get_position(void *obj);
/// @brief Move the camera to the given world-space position (Vec3).
void rt_camera3d_set_position(void *obj, void *pos);
/// @brief Get the unit forward vector (the direction the camera is facing).
void *rt_camera3d_get_forward(void *obj);
/// @brief Get the unit right vector (perpendicular to forward and up).
void *rt_camera3d_get_right(void *obj);
/// @brief Cast a world-space ray from screen pixel (sx, sy) given screen size (sw, sh).
/// Returns a Ray suitable for picking and intersection tests.
void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh);

//=========================================================================
// Material3D — surface appearance (color, texture, shininess)
//=========================================================================

#define RT_MATERIAL3D_WORKFLOW_LEGACY 0
#define RT_MATERIAL3D_WORKFLOW_PBR 1

#define RT_MATERIAL3D_ALPHA_MODE_OPAQUE 0
#define RT_MATERIAL3D_ALPHA_MODE_MASK 1
#define RT_MATERIAL3D_ALPHA_MODE_BLEND 2

/// @brief Create a default white legacy-shaded material.
void *rt_material3d_new(void);
/// @brief Create a flat-color legacy material with the given diffuse color.
void *rt_material3d_new_color(double r, double g, double b);
/// @brief Create a textured legacy material from a Pixels object (used as albedo).
void *rt_material3d_new_textured(void *pixels);
/// @brief Create a PBR-workflow material with the given base color (default metallic=0, roughness=0.5).
void *rt_material3d_new_pbr(double r, double g, double b);
/// @brief Deep copy a material (independent storage and texture refs).
void *rt_material3d_clone(void *obj);
/// @brief Create a per-instance variant sharing the same shader but with mutable params.
void *rt_material3d_make_instance(void *obj);
/// @brief Set the diffuse / base color (legacy or PBR depending on workflow).
void rt_material3d_set_color(void *obj, double r, double g, double b);
/// @brief Set the diffuse texture (legacy workflow); aliased to albedo for PBR.
void rt_material3d_set_texture(void *obj, void *pixels);
/// @brief Set the PBR albedo (base color) texture.
void rt_material3d_set_albedo_map(void *obj, void *pixels);
/// @brief Set the legacy specular shininess exponent (higher = sharper highlights).
void rt_material3d_set_shininess(void *obj, double s);
/// @brief Mark the material as unlit (skip lighting calculations entirely).
void rt_material3d_set_unlit(void *obj, int8_t unlit);
/// @brief Switch shading model (0 = legacy Phong, 1 = PBR metallic-roughness).
void rt_material3d_set_shading_model(void *obj, int64_t model);
/// @brief Write a value to a backend-specific custom shader parameter slot.
void rt_material3d_set_custom_param(void *obj, int64_t index, double value);
/// @brief Set the material alpha multiplier (1.0 = opaque, 0.0 = invisible).
void rt_material3d_set_alpha(void *obj, double alpha);
/// @brief Read the material alpha.
double rt_material3d_get_alpha(void *obj);
/// @brief Set the PBR metallic factor (0.0 = dielectric, 1.0 = pure metal).
void rt_material3d_set_metallic(void *obj, double value);
/// @brief Read the PBR metallic factor.
double rt_material3d_get_metallic(void *obj);
/// @brief Set the PBR roughness factor (0.0 = mirror, 1.0 = fully rough).
void rt_material3d_set_roughness(void *obj, double value);
/// @brief Read the PBR roughness factor.
double rt_material3d_get_roughness(void *obj);
/// @brief Set the ambient-occlusion factor (0.0 = full shadow, 1.0 = no occlusion).
void rt_material3d_set_ao(void *obj, double value);
/// @brief Read the AO factor.
double rt_material3d_get_ao(void *obj);
/// @brief Set the HDR emissive intensity multiplier (0 disables emission).
void rt_material3d_set_emissive_intensity(void *obj, double value);
/// @brief Read the emissive intensity multiplier.
double rt_material3d_get_emissive_intensity(void *obj);
/// @brief Bind a tangent-space normal map texture (requires `_calc_tangents` on the mesh).
void rt_material3d_set_normal_map(void *obj, void *pixels);
/// @brief Bind a packed metallic-roughness map (R = AO, G = roughness, B = metallic per glTF).
void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels);
/// @brief Bind a separate ambient-occlusion texture.
void rt_material3d_set_ao_map(void *obj, void *pixels);
/// @brief Bind a legacy specular highlight texture.
void rt_material3d_set_specular_map(void *obj, void *pixels);
/// @brief Bind an emissive texture (multiplied by emissive_intensity).
void rt_material3d_set_emissive_map(void *obj, void *pixels);
/// @brief Set the base emissive tint color.
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b);
/// @brief Scale the normal-map effect (0 = flat, 1 = full strength, >1 = exaggerated).
void rt_material3d_set_normal_scale(void *obj, double value);
/// @brief Read the normal scale.
double rt_material3d_get_normal_scale(void *obj);
/// @brief Set alpha mode: 0=Opaque, 1=Mask (alpha test), 2=Blend (transparent).
void rt_material3d_set_alpha_mode(void *obj, int64_t mode);
/// @brief Read the alpha mode.
int64_t rt_material3d_get_alpha_mode(void *obj);
/// @brief Toggle two-sided rendering (disables backface culling for this material).
void rt_material3d_set_double_sided(void *obj, int8_t enabled);
/// @brief True if the material is configured for double-sided rendering.
int8_t rt_material3d_get_double_sided(void *obj);

//=========================================================================
// Light3D — directional, point, or ambient light source
//=========================================================================

/// @brief Create a directional light shining along @p direction with the given color (sun-like).
void *rt_light3d_new_directional(void *direction, double r, double g, double b);
/// @brief Create a point light at @p position with linear distance attenuation factor.
void *rt_light3d_new_point(void *position, double r, double g, double b, double attenuation);
/// @brief Create an ambient light contribution (illuminates all surfaces equally).
void *rt_light3d_new_ambient(double r, double g, double b);
/// @brief Create a spot light with inner/outer cone angles in degrees (smooth edge between).
void *rt_light3d_new_spot(void *position,
                          void *direction,
                          double r,
                          double g,
                          double b,
                          double attenuation,
                          double inner_angle,
                          double outer_angle);
/// @brief Multiply the light color by an intensity scalar (HDR-friendly).
void rt_light3d_set_intensity(void *obj, double intensity);
/// @brief Replace the light color (without altering intensity).
void rt_light3d_set_color(void *obj, double r, double g, double b);

/// @brief Register a temporary buffer to be freed at the end of the current frame.
void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);

/* Screen-space HUD overlay */
/// @brief Draw a screen-space filled rectangle as a HUD element.
void rt_canvas3d_draw_rect2d(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);
/// @brief Draw a centered crosshair gizmo (4 lines around the canvas center).
void rt_canvas3d_draw_crosshair(void *canvas, int64_t color, int64_t size);
/// @brief Draw screen-space text on top of the rendered scene.
void rt_canvas3d_draw_text2d(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color);

/* Debug gizmos */
/// @brief Draw an axis-aligned bounding box outline between min and max corners.
void rt_canvas3d_draw_aabb_wire(void *canvas, void *min_v, void *max_v, int64_t color);
/// @brief Draw a wireframe sphere as great circles (3 orthogonal rings).
void rt_canvas3d_draw_sphere_wire(void *canvas, void *center, double radius, int64_t color);
/// @brief Draw a ray with optional arrowhead, capped at @p length world units.
void rt_canvas3d_draw_debug_ray(
    void *canvas, void *origin, void *dir, double length, int64_t color);
/// @brief Draw an XYZ axis gizmo at @p origin (X=red, Y=green, Z=blue).
void rt_canvas3d_draw_axis(void *canvas, void *origin, double scale);

/* Fog */
/// @brief Enable distance fog with linear falloff between @p near_dist and @p far_dist.
void rt_canvas3d_set_fog(
    void *canvas, double near_dist, double far_dist, double r, double g, double b);
/// @brief Disable fog.
void rt_canvas3d_clear_fog(void *canvas);

/* Shadows */
/// @brief Enable shadow-map rendering at the given square resolution (typical: 1024 or 2048).
void rt_canvas3d_enable_shadows(void *canvas, int64_t resolution);
/// @brief Disable shadow rendering.
void rt_canvas3d_disable_shadows(void *canvas);
/// @brief Set the shadow depth bias to combat shadow acne (typical: 0.001–0.005).
void rt_canvas3d_set_shadow_bias(void *canvas, double bias);

/* Opaque depth-order hint: enables front-to-back sorting for opaque draws.
 * This is not full occlusion-query or Hi-Z visibility culling. */
/// @brief Toggle front-to-back sorting hint for opaque draws (helps early-Z reject overdraw).
void rt_canvas3d_set_occlusion_culling(void *canvas, int8_t enabled);

/* Instanced rendering + Terrain */
/// @brief Submit a Mesh3DBatch for GPU-instanced rendering (one draw call per batch).
void rt_canvas3d_draw_instanced(void *canvas, void *batch);
/// @brief Submit a Terrain3D for chunked heightmap rendering.
void rt_canvas3d_draw_terrain(void *canvas, void *terrain);

/* Camera shake + smooth follow */
/// @brief Apply transient camera shake (decays over @p duration seconds with rate @p decay).
void rt_camera3d_shake(void *cam, double intensity, double duration, double decay);
/// @brief Smoothly orbit the camera at fixed distance/height behind @p target each frame.
void rt_camera3d_smooth_follow(
    void *cam, void *target, double distance, double height, double speed, double dt);
/// @brief Interpolate the camera's look direction toward @p target each frame.
void rt_camera3d_smooth_look_at(void *cam, void *target, double speed, double dt);

/* FPS camera */
/// @brief Initialize FPS-camera state (yaw=0, pitch=0) for first-person controls.
void rt_camera3d_fps_init(void *cam);
/// @brief Apply FPS controls: mouse delta rotates yaw/pitch, keys translate the camera.
void rt_camera3d_fps_update(void *cam,
                            double yaw_delta,
                            double pitch_delta,
                            double move_fwd,
                            double move_right,
                            double move_up,
                            double speed,
                            double dt);
/// @brief Read the current FPS-camera yaw in degrees.
double rt_camera3d_get_yaw(void *cam);
/// @brief Read the current FPS-camera pitch in degrees.
double rt_camera3d_get_pitch(void *cam);
/// @brief Set the FPS-camera yaw in degrees and rebuild the view immediately.
void rt_camera3d_set_yaw(void *cam, double yaw);
/// @brief Set the FPS-camera pitch in degrees (clamped internally to +/-89) and rebuild the view.
void rt_camera3d_set_pitch(void *cam, double pitch);

#ifdef __cplusplus
}
#endif
