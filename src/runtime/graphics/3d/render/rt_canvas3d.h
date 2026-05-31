//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d.h
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
// Links: plans/3d/01-software-renderer.md, src/runtime/graphics/common/rt_graphics.h
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
/// @brief Resize the canvas and active backend output targets.
void rt_canvas3d_resize(void *obj, int64_t w, int64_t h);
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
/// @return 1 while the canvas remains open, 0 after a close request or invalid handle.
int64_t rt_canvas3d_poll(void *obj);
/// @brief Dequeue one event type captured by the most recent Poll call, or 0 if none.
int64_t rt_canvas3d_poll_event(void *obj);
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
/// @brief Get the backing window width, ignoring any bound render target.
int64_t rt_canvas3d_get_window_width(void *obj);
/// @brief Get the backing window height, ignoring any bound render target.
int64_t rt_canvas3d_get_window_height(void *obj);
/// @brief Get the active output width (window, or current render target when bound).
int64_t rt_canvas3d_get_active_output_width(void *obj);
/// @brief Get the active output height (window, or current render target when bound).
int64_t rt_canvas3d_get_active_output_height(void *obj);
/// @brief Get the rolling-average FPS measured over recent frames.
int64_t rt_canvas3d_get_fps(void *obj);
/// @brief Get the wall-clock milliseconds since the previous Flip (first frame returns 0).
int64_t rt_canvas3d_get_delta_time(void *obj);
/// @brief Get the wall-clock seconds since the previous Flip (first frame returns 0.0).
double rt_canvas3d_get_delta_time_sec(void *obj);
/// @brief Cap the per-frame delta time (smooths spikes after pause/breakpoint).
void rt_canvas3d_set_dt_max(void *obj, int64_t max_ms);
/// @brief Apply a backend-safe quality profile (0 performance, 1 balanced, 2 cinematic).
void rt_canvas3d_set_quality(void *obj, int64_t quality);
/// @brief Last quality profile requested through SetQuality.
int64_t rt_canvas3d_get_quality_requested(void *obj);
/// @brief Quality profile actually active after backend fallback.
int64_t rt_canvas3d_get_quality_active(void *obj);
/// @brief True when the active quality profile was degraded for backend safety.
int8_t rt_canvas3d_get_quality_fallback(void *obj);
/// @brief Reason for the last quality fallback, or an empty string.
rt_string rt_canvas3d_get_quality_fallback_reason(void *obj);
/// @brief Select live, synthetic, or live+synthetic input for this canvas (0, 1, or 2).
void rt_canvas3d_set_input_source(void *obj, int64_t mode);
/// @brief Queue a synthetic keyboard transition for the next synthetic input frame.
void rt_canvas3d_push_synthetic_key(void *obj, int64_t key, int8_t down);
/// @brief Queue a synthetic mouse sample for the next synthetic input frame.
void rt_canvas3d_push_synthetic_mouse(
    void *obj, double dx, double dy, int64_t buttons, double wheel);
/// @brief Drop queued synthetic input and release keys/buttons held by the synthetic source.
void rt_canvas3d_clear_synthetic_input(void *obj);
/// @brief Select live wall-clock or fixed synthetic delta-time source (0 or 1).
void rt_canvas3d_set_clock_source(void *obj, int64_t mode);
/// @brief Set the fixed synthetic delta time in seconds.
void rt_canvas3d_set_synthetic_delta_time_sec(void *obj, double dt);
/// @brief Advance one deterministic input/timing frame without pumping platform events.
void rt_canvas3d_advance_synthetic_frame(void *obj);
/// @brief Bind or clear a Light3D slot; the canvas retains the assigned light until replaced.
void rt_canvas3d_set_light(void *obj, int64_t index, void *light);
/// @brief Clear all retained canvas light slots.
void rt_canvas3d_clear_lights(void *obj);
/// @brief Install a conservative key/fill/ambient setup for readable default scenes.
void rt_canvas3d_set_default_lighting(void *obj);
/// @brief Count currently assigned canvas light slots.
int64_t rt_canvas3d_get_light_count(void *obj);
/// @brief Enable clustered/forward+ lighting when the backend advertises support.
void rt_canvas3d_set_clustered_lighting(void *obj, int8_t enabled);
/// @brief Current maximum active light count for the selected lighting path.
int64_t rt_canvas3d_get_max_active_lights(void *obj);
/// @brief Set the ambient light color applied to all lit materials.
void rt_canvas3d_set_ambient(void *obj, double r, double g, double b);
/// @brief Draw a debug 3D line between two Vec3 points.
void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color);
void rt_canvas3d_draw_line3d_raw(void *obj, const double *from, const double *to, int64_t color);
/// @brief Draw a debug 3D point (square) at the given position with pixel size.
void rt_canvas3d_draw_point3d(void *obj, void *pos, int64_t color, int64_t size);
/// @brief Get the active backend name ("d3d11", "metal", "opengl", or "software").
rt_string rt_canvas3d_get_backend(void *obj);

#define RT_CANVAS3D_BACKEND_CAP_SOFTWARE 0x0001LL
#define RT_CANVAS3D_BACKEND_CAP_GPU 0x0002LL
#define RT_CANVAS3D_BACKEND_CAP_RENDER_TARGET 0x0004LL
#define RT_CANVAS3D_BACKEND_CAP_WINDOW_READBACK 0x0008LL
#define RT_CANVAS3D_BACKEND_CAP_SHADOWS 0x0010LL
#define RT_CANVAS3D_BACKEND_CAP_SKYBOX 0x0020LL
#define RT_CANVAS3D_BACKEND_CAP_HARDWARE_INSTANCING 0x0040LL
#define RT_CANVAS3D_BACKEND_CAP_POSTFX 0x0080LL
#define RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX 0x0100LL
#define RT_CANVAS3D_BACKEND_CAP_POSTFX_OVERLAY 0x0200LL
#define RT_CANVAS3D_BACKEND_CAP_FINAL_SCREENSHOT 0x0400LL
#define RT_CANVAS3D_BACKEND_CAP_GPU_POSTFX_OVERLAY 0x0800LL
#define RT_CANVAS3D_BACKEND_CAP_CLUSTERED_LIGHTING 0x1000LL
#define RT_CANVAS3D_BACKEND_CAP_SHADOW_CSM 0x2000LL
#define RT_CANVAS3D_BACKEND_CAP_OCCLUSION 0x4000LL
#define RT_CANVAS3D_BACKEND_CAP_HLOD 0x8000LL
#define RT_CANVAS3D_BACKEND_CAP_BC7 0x10000LL
#define RT_CANVAS3D_BACKEND_CAP_ASTC 0x20000LL
#define RT_CANVAS3D_BACKEND_CAP_ETC2 0x40000LL

/// @brief Return an RT_CANVAS3D_BACKEND_CAP_* bitmask for the active backend.
int64_t rt_canvas3d_get_backend_capabilities(void *obj);
/// @brief Return whether the active backend supports a named capability.
int8_t rt_canvas3d_backend_supports(void *obj, rt_string capability);
/// @brief Number of main 3D draw submissions queued by the latest ended frame.
int64_t rt_canvas3d_get_draw_count(void *obj);
/// @brief Number of draw submissions rejected by visibility culling in the latest scene draw.
int64_t rt_canvas3d_get_occluded_draw_count(void *obj);
/// @brief Number of opaque draw candidates tested by the CPU occlusion grid in the latest frame.
int64_t rt_canvas3d_get_occlusion_candidate_count(void *obj);
/// @brief Texture payload bytes uploaded to backend storage in the latest ended frame.
int64_t rt_canvas3d_get_texture_upload_bytes(void *obj);
/// @brief Set the backend texture upload byte budget for each frame; negative disables the budget.
void rt_canvas3d_set_texture_upload_budget(void *obj, int64_t bytes);
/// @brief Texture payload bytes still waiting for backend texture upload budget.
int64_t rt_canvas3d_get_texture_upload_pending_bytes(void *obj);
/// @brief Capture the current back-buffer contents into a fresh Pixels object.
void *rt_canvas3d_screenshot(void *obj);
/// @brief Begin recording a final overlay pass composited after post-FX during finalization.
void rt_canvas3d_begin_overlay(void *obj);
/// @brief Finish recording the final overlay pass started by BeginOverlay.
void rt_canvas3d_end_overlay(void *obj);
/// @brief Discard any recorded final overlay commands for the current frame.
void rt_canvas3d_clear_overlay(void *obj);
/// @brief Apply post-FX and final overlay exactly once, without presenting.
void rt_canvas3d_finalize_frame(void *obj);
/// @brief Capture finalized pixels, finalizing first if needed.
void *rt_canvas3d_screenshot_final(void *obj);
/// @brief Return whether the current frame has already been finalized.
int8_t rt_canvas3d_get_frame_finalized(void *obj);

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
/// @brief Allocate an HDR offscreen render target with RGBA16F color storage.
void *rt_rendertarget3d_new_hdr(int64_t width, int64_t height);
/// @brief Width of the render target in pixels.
int64_t rt_rendertarget3d_get_width(void *obj);
/// @brief Height of the render target in pixels.
int64_t rt_rendertarget3d_get_height(void *obj);
/// @brief Return non-zero when the render target stores HDR color internally.
int32_t rt_rendertarget3d_get_is_hdr(void *obj);
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
/// @brief Reserve backing storage for at least vertex_count vertices and triangle_count triangles.
void rt_mesh3d_reserve(void *obj, int64_t vertex_count, int64_t triangle_count);
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
/// @brief True when the mesh payload is resident and drawable.
int8_t rt_mesh3d_get_resident(void *obj);
/// @brief Mark the mesh payload resident/nonresident without releasing the Mesh3D handle.
void rt_mesh3d_set_resident(void *obj, int8_t resident);
/// @brief Estimated bytes for the currently resident vertex/index payload.
int64_t rt_mesh3d_get_resident_bytes(void *obj);
/// @brief Append a vertex with position, normal, and UV.
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

/// @brief Create a perspective camera (FOV in degrees, aspect = width/height, near/far clip
/// planes).
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
/// @brief Get/set the near clip-plane distance.
double rt_camera3d_get_near_plane(void *obj);
void rt_camera3d_set_near_plane(void *obj, double near_plane);
/// @brief Get/set the far clip-plane distance.
double rt_camera3d_get_far_plane(void *obj);
void rt_camera3d_set_far_plane(void *obj, double far_plane);
/// @brief Get the camera world-space position as a Vec3.
void *rt_camera3d_get_position(void *obj);
/// @brief Move the camera to the given world-space position (Vec3).
void rt_camera3d_set_position(void *obj, void *pos);
/// @brief Get the unit forward vector (the direction the camera is facing).
void *rt_camera3d_get_forward(void *obj);
/// @brief Get the unit right vector (perpendicular to forward and up).
void *rt_camera3d_get_right(void *obj);
/// @brief Return a normalized world-space picking direction for screen pixel (sx, sy).
/// Combine it with `ScreenToRayOrigin()` for perspective and orthographic picking.
/// Orthographic cameras return their forward direction (parallel rays).
void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh);
/// @brief Return the world-space origin for a screen-space picking ray.
void *rt_camera3d_screen_to_ray_origin(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh);

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
/// @brief Create a textured legacy material from Pixels or TextureAsset3D.
void *rt_material3d_new_textured(void *pixels);
/// @brief Create a PBR-workflow material with the given base color (default metallic=0,
/// roughness=0.5).
void *rt_material3d_new_pbr(double r, double g, double b);
/// @brief Deep copy a material (independent storage and texture refs).
void *rt_material3d_clone(void *obj);
/// @brief Create a per-instance variant sharing the same shader but with mutable params.
void *rt_material3d_make_instance(void *obj);
/// @brief Set the diffuse / base color (legacy or PBR depending on workflow).
void rt_material3d_set_color(void *obj, double r, double g, double b);
/// @brief Read the diffuse / base color as a Vec3.
void *rt_material3d_get_color(void *obj);
/// @brief Set the diffuse texture (legacy workflow); aliased to albedo for PBR.
void rt_material3d_set_texture(void *obj, void *pixels);
/// @brief Set the PBR albedo (base color) texture.
void rt_material3d_set_albedo_map(void *obj, void *pixels);
/// @brief Set the legacy specular shininess exponent (higher = sharper highlights).
void rt_material3d_set_shininess(void *obj, double s);
/// @brief Mark the material as unlit (skip lighting calculations entirely).
void rt_material3d_set_unlit(void *obj, int8_t unlit);
/// @brief True if unlit mode is enabled.
int8_t rt_material3d_get_unlit(void *obj);
/// @brief Switch shading model (0=Phong, 1=Toon, 2=PBR workflow, 3=Unlit, 4=Fresnel, 5=Emissive).
void rt_material3d_set_shading_model(void *obj, int64_t model);
/// @brief Read the current shading model.
int64_t rt_material3d_get_shading_model(void *obj);
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
/// @brief True when a base-color/albedo texture is bound.
int8_t rt_material3d_get_has_texture(void *obj);
/// @brief True when a normal map texture is bound.
int8_t rt_material3d_get_has_normal_map(void *obj);
/// @brief Bind a packed metallic-roughness map (R = AO, G = roughness, B = metallic per glTF).
void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels);
/// @brief True when a packed metallic-roughness texture is bound.
int8_t rt_material3d_get_has_metallic_roughness_map(void *obj);
/// @brief Bind a separate ambient-occlusion texture.
void rt_material3d_set_ao_map(void *obj, void *pixels);
/// @brief True when a separate ambient-occlusion texture is bound.
int8_t rt_material3d_get_has_ao_map(void *obj);
/// @brief Bind a legacy specular highlight texture.
void rt_material3d_set_specular_map(void *obj, void *pixels);
/// @brief True when a specular map texture is bound.
int8_t rt_material3d_get_has_specular_map(void *obj);
/// @brief Bind an emissive texture (multiplied by emissive_intensity).
void rt_material3d_set_emissive_map(void *obj, void *pixels);
/// @brief True when an emissive map texture is bound.
int8_t rt_material3d_get_has_emissive_map(void *obj);
/// @brief True when an environment cubemap is bound.
int8_t rt_material3d_get_has_env_map(void *obj);
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
/// @brief Get the light type (0=directional, 1=point, 2=ambient, 3=spot).
int64_t rt_light3d_get_type(void *obj);
/// @brief Get the light color as a Vec3.
void *rt_light3d_get_color(void *obj);
/// @brief Get the brightness multiplier.
double rt_light3d_get_intensity(void *obj);
/// @brief Enable or disable a light without removing it from its slot.
void rt_light3d_set_enabled(void *obj, int8_t enabled);
/// @brief True if the light contributes to backend light params.
int8_t rt_light3d_get_enabled(void *obj);
/// @brief Toggle whether this light is eligible for shadow-map selection.
void rt_light3d_set_casts_shadows(void *obj, int8_t enabled);
/// @brief True if this light may claim shadow-map slots.
int8_t rt_light3d_get_casts_shadows(void *obj);
/// @brief Get the normalized light direction as a Vec3.
void *rt_light3d_get_direction(void *obj);
/// @brief Get the light position as a Vec3.
void *rt_light3d_get_position(void *obj);
/// @brief Move the light to a new world position (Vec3).
void rt_light3d_set_position(void *obj, void *position);
/// @brief Re-aim the light; the direction (Vec3) is normalized.
void rt_light3d_set_direction(void *obj, void *direction);

/// @brief Register a temporary buffer to be freed at the end of the current frame.
/// @return 1 when ownership transfers to the canvas, 0 when the caller still owns `buffer`.
int rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);
/// @brief Remove a previously-registered temporary buffer; caller owns/free()s it again.
int rt_canvas3d_remove_temp_buffer(void *canvas, void *buffer);

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
void rt_canvas3d_draw_aabb_wire_raw(void *canvas, const double *min_v, const double *max_v, int64_t color);
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
/// @brief Request cascaded shadow maps; counts > 1 require backend support.
void rt_canvas3d_set_shadow_cascades(void *canvas, int64_t count);

/* Coarse CPU visibility: frustum rejection plus optional low-resolution
 * screen-space occlusion over front-to-back sorted opaque draws. */
/// @brief Toggle coarse CPU frustum rejection plus front-to-back opaque ordering.
void rt_canvas3d_set_frustum_culling(void *canvas, int8_t enabled);
/// @brief Toggle frustum rejection plus conservative CPU occlusion skips.
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
