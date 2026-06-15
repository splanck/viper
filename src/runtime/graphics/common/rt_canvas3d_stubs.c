//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_canvas3d_stubs.c
/// @brief Graphics-disabled Canvas3D, camera, light, mesh, material, and
/// primitive rendering stubs.
///
/// @details This split source keeps the Canvas3D-facing unavailable-backend
/// API surface separate from asset, physics, scene, and media stubs while
/// preserving the original trap and fallback behavior.
///
// File: src/runtime/graphics/common/rt_canvas3d_stubs.c
// Purpose: Graphics-disabled Canvas3D, camera, render-target, and post-processing entry points.
//
// Key invariants:
//   - Compiled only for graphics-disabled runtime builds.
//   - Stateful graphics APIs fail with the shared InvalidOperation trap.
//   - Backend-independent query helpers keep their documented fallback values.
//
// Ownership/Lifetime:
//   - Stub entry points allocate no graphics resources and retain no handles.
//
// Links: src/runtime/graphics/common/rt_graphics_stubs_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_stubs_internal.h"

//=============================================================================
// Graphics 3D stubs — Canvas3D, Mesh3D, Camera3D, Material3D, Light3D
//=============================================================================

/// @brief Stub for `CubeMap3D.New` — would normally allocate a six-
///        face cubemap from the given Pixels surfaces. Used for skyboxes
///        and environment-map reflections; all six faces should share
///        the same dimensions.
///
/// Trapping stub: cubemaps are sampled by skybox draws and PBR
/// reflection passes — a NULL return would crash the renderer.
///
/// @param px Pixels handle for the +X face (right) (ignored).
/// @param nx Pixels handle for the -X face (left) (ignored).
/// @param py Pixels handle for the +Y face (top) (ignored).
/// @param ny Pixels handle for the -Y face (bottom) (ignored).
/// @param pz Pixels handle for the +Z face (front) (ignored).
/// @param nz Pixels handle for the -Z face (back) (ignored).
///
/// @return Never returns normally.
void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz) {
    (void)px;
    (void)nx;
    (void)py;
    (void)ny;
    (void)pz;
    (void)nz;
    rt_graphics_unavailable_("CubeMap3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Set the skybox of the canvas3d.
void rt_canvas3d_set_skybox(void *c, void *cm) {
    (void)c;
    (void)cm;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetSkybox: graphics support not compiled in");
}

/// @brief Clear the skybox of the canvas3d.
void rt_canvas3d_clear_skybox(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ClearSkybox: graphics support not compiled in");
}

/// @brief Stub for `RenderTarget3D.New` — would normally allocate an
///        offscreen `(w x h)` 3D render target (color + depth).
///
/// Trapping stub: render targets cannot be faked headlessly because they
/// are explicit GPU-side allocations the caller will try to bind/sample.
///
/// @param w Target width in pixels (ignored).
/// @param h Target height in pixels (ignored).
///
/// @return Never returns normally.
void *rt_rendertarget3d_new(int64_t w, int64_t h) {
    (void)w;
    (void)h;
    rt_graphics_unavailable_("RenderTarget3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `RenderTarget3D.NewHdr` — would normally allocate an
///        HDR offscreen `(w x h)` 3D render target (RGBA16F color + depth).
///
/// Trapping stub: HDR render targets cannot be faked headlessly because they
/// are explicit GPU-side allocations the caller will try to bind/sample.
void *rt_rendertarget3d_new_hdr(int64_t w, int64_t h) {
    (void)w;
    (void)h;
    rt_graphics_unavailable_("RenderTarget3D.NewHdr: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `RenderTarget3D.Width`.
///
/// Silent stub returning `0`. Reachable only via a NULL handle that the
/// caller obtained from a different source (real RenderTarget3Ds cannot be
/// created in this build because `rt_rendertarget3d_new` traps).
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `0`.
int64_t rt_rendertarget3d_get_width(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `RenderTarget3D.Height`.
///
/// Silent stub returning `0`. See `rt_rendertarget3d_get_width` for the
/// reachability note.
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `0`.
int64_t rt_rendertarget3d_get_height(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `RenderTarget3D.IsHdr`.
///
/// Silent stub returning `0`. Reachable only through a NULL handle in
/// graphics-disabled builds.
int32_t rt_rendertarget3d_get_is_hdr(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `RenderTarget3D.AsPixels` — would normally return a
///        Pixels view of the render target's color attachment.
///
/// Silent stub returning NULL. Callers should null-check before drawing.
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `NULL`.
void *rt_rendertarget3d_as_pixels(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Canvas3D.SetRenderTarget` — would normally redirect
///        subsequent 3D draws into the given offscreen target instead of
///        the on-screen framebuffer.
///
/// Trapping stub: state mutators on a non-existent backend are
/// harmless to swallow.
///
/// @param c Canvas3D handle (ignored).
/// @param t RenderTarget3D handle (ignored).
void rt_canvas3d_set_render_target(void *c, void *t) {
    (void)c;
    (void)t;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetRenderTarget: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.ResetRenderTarget` — would normally restore
///        on-screen rendering after a `SetRenderTarget` call.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_reset_render_target(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ResetRenderTarget: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.New` — would normally create a 3D-capable
///        canvas window with an attached depth buffer and a backend
///        (Metal / D3D11 / OpenGL / software auto-selected).
///
/// Trapping stub: there is no plausible no-op behavior — callers expect a
/// usable handle they can issue draw calls against.
///
/// @param title Window title (ignored).
/// @param w     Width in pixels (ignored).
/// @param h     Height in pixels (ignored).
///
/// @return Never returns normally.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h) {
    (void)title;
    (void)w;
    (void)h;
    rt_graphics_unavailable_("Canvas3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Canvas3D.Clear` — would normally clear the color and
///        depth buffers to the given RGB and `1.0` respectively.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param r Clear color red component, 0..1 (ignored).
/// @param g Clear color green component, 0..1 (ignored).
/// @param b Clear color blue component, 0..1 (ignored).
void rt_canvas3d_clear(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.Clear: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Resize` — would normally resize the canvas
///        and its active backend output targets to the new dimensions.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param w New width in pixels (ignored).
/// @param h New height in pixels (ignored).
void rt_canvas3d_resize(void *o, int64_t w, int64_t h) {
    (void)o;
    (void)w;
    (void)h;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.Resize: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Begin` — would normally start a 3D render
///        pass with the given Camera3D and prepare per-frame uniforms.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param c Camera3D handle (ignored).
void rt_canvas3d_begin(void *o, void *c) {
    (void)o;
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.Begin: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawMesh` — would normally issue a single
///        textured/material-bound mesh draw with a model-space transform.
///
/// Trapping stub.
///
/// @param o  Canvas3D handle (ignored).
/// @param m  Mesh3D handle (ignored).
/// @param t  Transform3D handle (ignored).
/// @param mt Material3D handle (ignored).
void rt_canvas3d_draw_mesh(void *o, void *m, void *t, void *mt) {
    (void)o;
    (void)m;
    (void)t;
    (void)mt;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawMesh: graphics support not compiled in");
}

void rt_canvas3d_draw_mesh_wind(
    void *o, void *m, void *t, void *mt, double dx, double dz, double s, double ph) {
    (void)o;
    (void)m;
    (void)t;
    (void)mt;
    (void)dx;
    (void)dz;
    (void)s;
    (void)ph;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawMeshWind: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.End` — would normally finalize the current
///        render pass and submit accumulated draw commands to the backend.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
void rt_canvas3d_end(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.End: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Flip` — would normally present the back
///        buffer to the screen.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
void rt_canvas3d_flip(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.Flip: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Poll` — would normally pump the OS event
///        queue and update input state for this frame.
///
/// Silent stub returning `0` (closed/unavailable).
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_poll(void *o) {
    (void)o;
    return 0;
}

int64_t rt_canvas3d_poll_event(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.ShouldClose` — would normally report whether
///        the OS window has received a close request.
///
/// Silent stub returning `0` (never closes). Note the divergence from the
/// 2D `rt_canvas_should_close` stub which traps; this one is silent so
/// 3D-headless smoke probes can run a few iterations of a game loop.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int8_t rt_canvas3d_should_close(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.SetWireframe` — would normally enable or
///        disable wireframe rasterization for subsequent draws.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param e Non-zero to enable wireframe (ignored).
void rt_canvas3d_set_wireframe(void *o, int8_t e) {
    (void)o;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetWireframe: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.SetBackfaceCull` — would normally enable or
///        disable backface culling for subsequent draws.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param e Non-zero to enable backface culling (ignored).
void rt_canvas3d_set_backface_cull(void *o, int8_t e) {
    (void)o;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetBackfaceCull: graphics support not compiled in");
}

/// @brief Get the width of the canvas3d.
int64_t rt_canvas3d_get_width(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the height of the canvas3d.
int64_t rt_canvas3d_get_height(void *o) {
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_window_width(void *o) {
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_window_height(void *o) {
    (void)o;
    return 0;
}

void rt_canvas3d_set_fullscreen(void *o, int8_t e) {
    (void)o;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetFullscreen: graphics support not compiled in");
}

int8_t rt_canvas3d_is_fullscreen(void *o) {
    (void)o;
    return 0;
}

void rt_canvas3d_toggle_fullscreen(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ToggleFullscreen: graphics support not compiled in");
}

void rt_canvas3d_draw_image2d(void *o, int64_t x, int64_t y, int64_t w, int64_t h, void *p) {
    (void)o;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)p;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawImage2D: graphics support not compiled in");
}

int64_t rt_canvas3d_get_active_output_width(void *o) {
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_active_output_height(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the fps of the canvas3d.
int64_t rt_canvas3d_get_fps(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the delta time of the canvas3d.
int64_t rt_canvas3d_get_delta_time(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the delta time of the canvas3d in seconds.
double rt_canvas3d_get_delta_time_sec(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Canvas3D.SetDtMax` — would normally cap the maximum
///        delta-time value reported to the game loop on slow frames so a
///        single hitch can't tunnel through physics in one step.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param m Maximum delta-time in milliseconds (ignored).
void rt_canvas3d_set_dt_max(void *o, int64_t m) {
    (void)o;
    (void)m;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetDTMax: graphics support not compiled in");
}

void rt_canvas3d_set_quality(void *o, int64_t quality) {
    (void)o;
    (void)quality;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetQuality: graphics support not compiled in");
}

int64_t rt_canvas3d_get_quality_requested(void *o) {
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_quality_active(void *o) {
    (void)o;
    return 0;
}

int8_t rt_canvas3d_get_quality_fallback(void *o) {
    (void)o;
    return 0;
}

rt_string rt_canvas3d_get_quality_fallback_reason(void *o) {
    (void)o;
    return rt_string_from_bytes("", 0);
}

void rt_canvas3d_set_input_source(void *o, int64_t mode) {
    (void)o;
    (void)mode;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetInputSource: graphics support not compiled in");
}

void rt_canvas3d_push_synthetic_key(void *o, int64_t key, int8_t down) {
    (void)o;
    (void)key;
    (void)down;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.PushSyntheticKey: graphics support not compiled in");
}

void rt_canvas3d_push_synthetic_mouse(
    void *o, double dx, double dy, int64_t buttons, double wheel) {
    (void)o;
    (void)dx;
    (void)dy;
    (void)buttons;
    (void)wheel;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.PushSyntheticMouse: graphics support not compiled in");
}

void rt_canvas3d_clear_synthetic_input(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ClearSyntheticInput: graphics support not compiled in");
}

void rt_canvas3d_set_clock_source(void *o, int64_t mode) {
    (void)o;
    (void)mode;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetClockSource: graphics support not compiled in");
}

void rt_canvas3d_set_synthetic_delta_time_sec(void *o, double dt) {
    (void)o;
    (void)dt;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetSyntheticDeltaTimeSec: graphics support not compiled in");
}

void rt_canvas3d_advance_synthetic_frame(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.AdvanceSyntheticFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.SetLight` — would normally bind a Light3D to
///        slot `i` of the per-frame light array (up to `VGFX3D_MAX_LIGHTS` lights).
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param i Light slot index (ignored).
/// @param l Light3D handle, or NULL to clear the slot (ignored).
void rt_canvas3d_set_light(void *o, int64_t i, void *l) {
    (void)o;
    (void)i;
    (void)l;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetLight: graphics support not compiled in");
}

void rt_canvas3d_clear_lights(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ClearLights: graphics support not compiled in");
}

void rt_canvas3d_set_default_lighting(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetDefaultLighting: graphics support not compiled in");
}

int64_t rt_canvas3d_get_light_count(void *o) {
    (void)o;
    return 0;
}

void rt_canvas3d_set_clustered_lighting(void *o, int8_t enabled) {
    (void)o;
    (void)enabled;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetClusteredLighting: graphics support not compiled in");
}

int64_t rt_canvas3d_get_max_active_lights(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.SetAmbient` — would normally set the global
///        ambient illumination color used in the lighting equation.
///
/// Trapping stub.
///
/// @param o Canvas3D handle (ignored).
/// @param r Ambient red, 0..1 (ignored).
/// @param g Ambient green, 0..1 (ignored).
/// @param b Ambient blue, 0..1 (ignored).
void rt_canvas3d_set_ambient(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetAmbient: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawLine3D` — would normally draw a 3D line
///        segment between two world-space points.
///
/// Trapping stub. Used for debug visualization (gizmos, raycast hits)
/// in the real backend.
///
/// @param o Canvas3D handle (ignored).
/// @param f Vec3 start point handle (ignored).
/// @param t Vec3 end point handle (ignored).
/// @param c Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_line3d(void *o, void *f, void *t, int64_t c) {
    (void)o;
    (void)f;
    (void)t;
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawLine3D: graphics support not compiled in");
}

void rt_canvas3d_draw_line3d_raw(void *o, const double *f, const double *t, int64_t c) {
    (void)o;
    (void)f;
    (void)t;
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawLine3DRaw: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawPoint3D` — would normally draw a 3D
///        point sprite at the given world-space position.
///
/// Trapping stub. Used for debug visualization.
///
/// @param o Canvas3D handle (ignored).
/// @param p Vec3 point handle (ignored).
/// @param c Packed 0xAARRGGBB color (ignored).
/// @param s Point sprite size in screen pixels (ignored).
void rt_canvas3d_draw_point3d(void *o, void *p, int64_t c, int64_t s) {
    (void)o;
    (void)p;
    (void)c;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawPoint3D: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Backend` — would normally return the name of
///        the active 3D backend ("metal", "d3d11", "opengl", or "software").
///
/// Silent stub returning NULL so that backend-detection code can branch
/// without needing to handle a trap.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_canvas3d_get_backend(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Canvas3D.BackendFallback` — would normally report
///        whether runtime backend creation fell back to software.
///
/// Silent stub returning false.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int8_t rt_canvas3d_get_backend_fallback(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.BackendCapabilities` — would normally return
///        a bitmask describing backend feature hooks.
///
/// Silent stub returning 0 (no capabilities).
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_backend_capabilities(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.BackendSupports` — would normally test a named
///        backend capability such as "shadows" or "hardware_instancing".
///
/// Silent stub returning false.
///
/// @param o Canvas3D handle (ignored).
/// @param capability Capability name (ignored).
///
/// @return `0`.
int8_t rt_canvas3d_backend_supports(void *o, rt_string capability) {
    (void)o;
    (void)capability;
    return 0;
}

/// @brief Stub for `Canvas3D.DrawCount` — latest main 3D draw-submission telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_draw_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.OccludedDrawCount` — latest visibility skip telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_occluded_draw_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.FrustumCulledDrawCount` — latest frustum-reject telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_frustum_culled_draw_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.CpuOccludedDrawCount` — latest CPU occlusion reject telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_cpu_occluded_draw_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.OcclusionCandidateCount` — latest CPU occlusion workload telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_occlusion_candidate_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.TextureUploadBytes` — latest texture upload telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_texture_upload_bytes(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.SetTextureUploadBudget` — would normally limit
///        backend texture uploads per frame.
///
/// Trapping stub in disabled-graphics builds.
///
/// @param o     Canvas3D handle (ignored).
/// @param bytes Budget in bytes; negative means unlimited (ignored).
void rt_canvas3d_set_texture_upload_budget(void *o, int64_t bytes) {
    (void)o;
    (void)bytes;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetTextureUploadBudget: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.TextureUploadPendingBytes` — queued upload telemetry.
///
/// Silent stub returning 0.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_get_texture_upload_pending_bytes(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.Screenshot` — would normally read back the
///        current 3D framebuffer into a fresh Pixels surface.
///
/// Trapping stub: without a graphics backend there is no framebuffer to read,
/// so returning NULL would hide an unavailable operation.
///
/// @param o Canvas3D handle (ignored).
///
/// @return Never returns normally; the fallback NULL only satisfies the C type checker.
void *rt_canvas3d_screenshot(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_RET("Canvas3D.Screenshot: graphics support not compiled in", NULL);
}

/// @brief Stub for `Camera3D.New` — would normally create a perspective
///        camera with vertical field of view `f` (radians), aspect ratio
///        `a`, near plane `n`, and far plane `fa`. Position is `(0, 0, 0)`,
///        looking along -Z, with +Y up; reposition with `LookAt`.
///
/// Trapping stub: cameras are referenced by `Canvas3D.Begin` and similar
/// draw calls — a NULL return would crash later.
///
/// @param f  Vertical FOV in radians (ignored).
/// @param a  Aspect ratio, width/height (ignored).
/// @param n  Near plane distance, world units (ignored).
/// @param fa Far plane distance (ignored).
///
/// @return Never returns normally.
void *rt_camera3d_new(double f, double a, double n, double fa) {
    (void)f;
    (void)a;
    (void)n;
    (void)fa;
    rt_graphics_unavailable_("Camera3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Camera3D.NewHorizontalFov` — would normally create a perspective camera from
///        a horizontal FOV, converting it to the renderer's vertical projection aperture using the
///        supplied aspect ratio.
///
/// Trapping stub: cameras are referenced by `Canvas3D.Begin` and similar draw calls.
///
/// @param f  Horizontal FOV in degrees (ignored).
/// @param a  Aspect ratio, width/height (ignored).
/// @param n  Near plane distance, world units (ignored).
/// @param fa Far plane distance (ignored).
///
/// @return Never returns normally.
void *rt_camera3d_new_horizontal_fov(double f, double a, double n, double fa) {
    (void)f;
    (void)a;
    (void)n;
    (void)fa;
    rt_graphics_unavailable_("Camera3D.NewHorizontalFov: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Camera3D.NewOrtho` — would normally create an
///        orthographic camera with vertical "size" `s` (the half-height
///        of the orthographic view volume in world units), aspect `a`,
///        near `n`, far `fa`. Used for isometric / strategy / 2.5D games.
///
/// @param s  Vertical view-volume half-height in world units (ignored).
/// @param a  Aspect ratio, width/height (ignored).
/// @param n  Near plane distance (ignored).
/// @param fa Far plane distance (ignored).
///
/// @return Never returns normally.
void *rt_camera3d_new_ortho(double s, double a, double n, double fa) {
    (void)s;
    (void)a;
    (void)n;
    (void)fa;
    rt_graphics_unavailable_("Camera3D.NewOrtho: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Camera3D.IsOrtho` — true for orthographic cameras
///        (created via `NewOrtho`), false for perspective cameras.
///
/// Silent stub returning `0` (perspective default).
///
/// @param o Camera3D handle (ignored).
///
/// @return `0`.
int8_t rt_camera3d_is_ortho(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Camera3D.LookAt` — orient the camera so it sits at
///        eye position `e` looking at world-space target `t`, with `u`
///        as the up reference (typically `(0, 1, 0)`).
///
/// Trapping stub. The real implementation builds the view matrix
/// from `e`, the forward vector `(t - e).normalized()`, and an
/// orthonormal basis derived from `u`.
///
/// @param o Camera3D handle (ignored).
/// @param e Vec3 eye position (ignored).
/// @param t Vec3 look-at target (ignored).
/// @param u Vec3 up reference (ignored).
void rt_camera3d_look_at(void *o, void *e, void *t, void *u) {
    (void)o;
    (void)e;
    (void)t;
    (void)u;
    RT_GRAPHICS_TRAP_VOID("Camera3D.LookAt: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.Orbit` — position the camera at distance
///        `d` from world-space target `t`, with yaw `y` and pitch `p`
///        (radians). Always looks at `t`.
///
/// Trapping stub. Convenience for orbital / RTS camera rigs.
///
/// @param o Camera3D handle (ignored).
/// @param t Vec3 orbit target (ignored).
/// @param d Distance from target (ignored).
/// @param y Yaw in radians (azimuth around target) (ignored).
/// @param p Pitch in radians (elevation above XZ plane) (ignored).
void rt_camera3d_orbit(void *o, void *t, double d, double y, double p) {
    (void)o;
    (void)t;
    (void)d;
    (void)y;
    (void)p;
    RT_GRAPHICS_TRAP_VOID("Camera3D.Orbit: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.FOV` — get the camera's vertical field of
///        view in radians.
///
/// Silent stub returning `0.0`.
///
/// @param o Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_fov(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Camera3D.SetFOV` — adjust the vertical field of
///        view; the projection matrix is recomputed lazily on next use.
///
/// Trapping stub.
///
/// @param o Camera3D handle (ignored).
/// @param f Vertical FOV in radians (ignored).
void rt_camera3d_set_fov(void *o, double f) {
    (void)o;
    (void)f;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetFOV: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.SetHorizontalFov` — would normally convert a horizontal FOV to the
///        camera's stored vertical FOV and rebuild the projection matrix.
///
/// Trapping stub.
///
/// @param o Camera3D handle (ignored).
/// @param f Horizontal FOV in degrees (ignored).
void rt_camera3d_set_horizontal_fov(void *o, double f) {
    (void)o;
    (void)f;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetHorizontalFov: graphics support not compiled in");
}

double rt_camera3d_get_near_plane(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Camera3D.EffectiveNearPlane` — return the sanitized near clip plane.
/// @details Graphics-disabled builds have no camera projection state, so this returns 0.0.
double rt_camera3d_get_effective_near_plane(void *o) {
    (void)o;
    return 0.0;
}

void rt_camera3d_set_near_plane(void *o, double n) {
    (void)o;
    (void)n;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetNearPlane: graphics support not compiled in");
}

double rt_camera3d_get_far_plane(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Camera3D.EffectiveFarPlane` — return the sanitized far clip plane.
/// @details Graphics-disabled builds have no camera projection state, so this returns 0.0.
double rt_camera3d_get_effective_far_plane(void *o) {
    (void)o;
    return 0.0;
}

void rt_camera3d_set_far_plane(void *o, double f) {
    (void)o;
    (void)f;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetFarPlane: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.Position` — get the camera's eye position
///        as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_position(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.SetPosition` — move the camera's eye to
///        the given Vec3 position. Forward direction is preserved.
///
/// Trapping stub.
///
/// @param o Camera3D handle (ignored).
/// @param p Vec3 position (ignored).
void rt_camera3d_set_position(void *o, void *p) {
    (void)o;
    (void)p;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetPosition: graphics support not compiled in");
}

/// @brief Stub for the internal render-aspect sync used by higher-level
///        Game3D window resize/tick helpers.
///
/// Trapping stub.
void rt_camera3d_sync_render_aspect(void *o, double aspect) {
    (void)o;
    (void)aspect;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SyncRenderAspect: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.Forward` — get the camera's normalized
///        forward direction as a Vec3 (the "look" axis).
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_forward(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.Right` — get the camera's normalized right
///        direction as a Vec3 (the "strafe" axis, perpendicular to
///        Forward and Up).
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_right(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.ScreenToRay` — would normally build a
///        world-space picking ray from a screen-pixel coordinate.
///        `(sx, sy)` is the screen pixel; `(sw, sh)` is the viewport
///        size. The ray's origin is the camera eye and direction passes
///        through the corresponding clip-space point.
///
/// Silent stub returning NULL.
///
/// @param o  Camera3D handle (ignored).
/// @param sx Screen pixel x (ignored).
/// @param sy Screen pixel y (ignored).
/// @param sw Viewport width in pixels (ignored).
/// @param sh Viewport height in pixels (ignored).
///
/// @return `NULL`.
void *rt_camera3d_screen_to_ray(void *o, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    (void)o;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    return NULL;
}

/// @brief Stub for `Camera3D.ScreenToRayOrigin`.
/// @return `NULL`.
void *rt_camera3d_screen_to_ray_origin(void *o, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    (void)o;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    return NULL;
}

/// @brief Stub for `Canvas3D.DrawMeshSkinned` — variant of `DrawMesh`
///        that applies skeletal skinning. The vertex shader fetches the
///        bone palette from `p` (AnimPlayer3D) and computes the final
///        per-vertex transform from the four bone-weight pairs.
///
/// Trapping stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param m   Mesh3D handle with bone weights set (ignored).
/// @param t   Transform3D handle (ignored).
/// @param mat Material3D handle (ignored).
/// @param p   AnimPlayer3D handle providing the bone palette (ignored).
void rt_canvas3d_draw_mesh_skinned(void *c, void *m, void *t, void *mat, void *p) {
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)p;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawMeshSkinned: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawMeshMorphed` — variant of `DrawMesh`
///        that applies the bound MorphTarget3D's currently-weighted
///        shapes to vertex positions/normals before transformation.
///
/// Trapping stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param m   Mesh3D handle (ignored).
/// @param t   Transform3D handle (ignored).
/// @param mat Material3D handle (ignored).
/// @param mt  MorphTarget3D handle (ignored).
void rt_canvas3d_draw_mesh_morphed(void *c, void *m, void *t, void *mat, void *mt) {
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)mt;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawMeshMorphed: graphics support not compiled in");
}

/* PostFX3D stubs */

/// @brief Stub for `PostFX3D.New` — would normally create an empty
///        post-processing chain that can be attached to a Canvas3D's
///        offscreen render path.
///
/// Trapping stub: a NULL chain would crash on the first effect-add call.
///
/// @return Never returns normally.
void *rt_postfx3d_new(void) {
    rt_graphics_unavailable_("PostFX3D.New: graphics support not compiled in");
    return NULL;
}

void *rt_postfx3d_new_quality(void *canvas, int64_t quality) {
    (void)canvas;
    (void)quality;
    rt_graphics_unavailable_("PostFX3D.NewQuality: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `PostFX3D.AddBloom` — append a bloom (high-pass
///        threshold + Gaussian blur + composite) pass to the chain.
///
/// Trapping stub.
///
/// @param o PostFX3D handle (ignored).
/// @param t Brightness threshold (luminance above which pixels bloom) (ignored).
/// @param i Bloom intensity multiplier (ignored).
/// @param b Number of Gaussian blur iterations (ignored).
void rt_postfx3d_add_bloom(void *o, double t, double i, int64_t b) {
    (void)o;
    (void)t;
    (void)i;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddBloom: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddTonemap` — append an HDR-to-LDR tonemap
///        pass: 0=off, 1=Reinhard, 2=ACES filmic.
///
/// Trapping stub. Required to flatten HDR scene output for display
/// on standard SDR monitors.
///
/// @param o PostFX3D handle (ignored).
/// @param m Tonemap operator index (ignored).
/// @param e Exposure multiplier applied before mapping (ignored).
void rt_postfx3d_add_tonemap(void *o, int64_t m, double e) {
    (void)o;
    (void)m;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddTonemap: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddFXAA` — append a Fast Approximate
///        Anti-Aliasing pass (cheap edge smoothing without MSAA).
///
/// Trapping stub.
///
/// @param o PostFX3D handle (ignored).
void rt_postfx3d_add_fxaa(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddFXAA: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddColorGrade` — append brightness /
///        contrast / saturation color grading.
///
/// Trapping stub.
///
/// @param o PostFX3D handle (ignored).
/// @param b Brightness adjustment (ignored).
/// @param c Contrast adjustment (ignored).
/// @param s Saturation adjustment (ignored).
void rt_postfx3d_add_color_grade(void *o, double b, double c, double s) {
    (void)o;
    (void)b;
    (void)c;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddColorGrade: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddVignette` — append a circular vignette
///        darkening pass.
///
/// Trapping stub. Useful for cinematic / dramatic framing.
///
/// @param o PostFX3D handle (ignored).
/// @param r Vignette inner radius (where darkening begins) (ignored).
/// @param s Falloff strength (ignored).
void rt_postfx3d_add_vignette(void *o, double r, double s) {
    (void)o;
    (void)r;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddVignette: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.SetEnabled` — globally enable or disable
///        the post-processing chain. When disabled the scene renders
///        directly to the swap-chain framebuffer.
///
/// Trapping stub.
///
/// @param o PostFX3D handle (ignored).
/// @param e Non-zero to enable post-processing (ignored).
void rt_postfx3d_set_enabled(void *o, int8_t e) {
    (void)o;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.SetEnabled: graphics support not compiled in");
}

/// @brief Get the enabled of the postfx3d.
int8_t rt_postfx3d_get_enabled(void *o) {
    (void)o;
    return 0;
}

/// @brief Remove all entries from the postfx3d.
void rt_postfx3d_clear(void *o) {
    (void)o;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.Clear: graphics support not compiled in");
}

/// @brief Return the count of elements in the postfx3d.
int64_t rt_postfx3d_get_effect_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Set the post fx of the canvas3d.
void rt_canvas3d_set_post_fx(void *c, void *fx) {
    (void)c;
    (void)fx;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetPostFX: graphics support not compiled in");
}

/// @brief Apply the to canvas of the postfx3d.
void rt_postfx3d_apply_to_canvas(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.ApplyToCanvas: graphics support not compiled in");
}

/* FPS Camera stubs */

/// @brief Stub for `Camera3D.FPSInit` — initialize an FPS-style camera
///        controller (yaw/pitch tracked separately, no roll).
///
/// Trapping stub.
///
/// @param c Camera3D handle (ignored).
void rt_camera3d_fps_init(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Camera3D.FPSInit: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.FPSUpdate` — advance the FPS camera by one
///        tick. Updates yaw/pitch from mouse delta and translates along
///        the local axes from WASD input.
///
/// Trapping stub. Parameter shape:
///   `(camera, mouseDx, mouseDy, dt, fwd, back, left, right)`
///
/// @param c Camera3D handle (ignored).
/// @param a Mouse delta x (ignored).
/// @param b Mouse delta y (ignored).
/// @param d Delta time in seconds (ignored).
/// @param e Forward input axis (ignored).
/// @param f Back input axis (ignored).
/// @param g Left input axis (ignored).
/// @param h Right input axis (ignored).
void rt_camera3d_fps_update(
    void *c, double a, double b, double d, double e, double f, double g, double h) {
    (void)c;
    (void)a;
    (void)b;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    RT_GRAPHICS_TRAP_VOID("Camera3D.FPSUpdate: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.Yaw` — get the FPS camera's yaw (horizontal
///        rotation, in radians).
///
/// Silent stub returning `0.0`.
///
/// @param c Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_yaw(void *c) {
    (void)c;
    return 0.0;
}

/// @brief Stub for `Camera3D.Pitch` — get the FPS camera's pitch
///        (vertical rotation, in radians; positive looks up).
///
/// Silent stub returning `0.0`.
///
/// @param c Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_pitch(void *c) {
    (void)c;
    return 0.0;
}

/// @brief Stub for `Camera3D.SetYaw` — set the FPS camera's yaw rotation.
///
/// Trapping stub.
///
/// @param c Camera3D handle (ignored).
/// @param v Yaw in radians (ignored).
void rt_camera3d_set_yaw(void *c, double v) {
    (void)c;
    (void)v;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetYaw: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.SetPitch` — set the FPS camera's pitch
///        rotation. The real implementation clamps to `[-π/2 + ε, π/2 - ε]`
///        to prevent gimbal lock.
///
/// Trapping stub.
///
/// @param c Camera3D handle (ignored).
/// @param v Pitch in radians (ignored).
void rt_camera3d_set_pitch(void *c, double v) {
    (void)c;
    (void)v;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SetPitch: graphics support not compiled in");
}

/* HUD overlay stubs */

/// @brief Stub for `Canvas3D.DrawRect2D` — would normally draw a 2D
///        screen-space rectangle as a HUD overlay (after the 3D scene
///        renders). Coordinates are in screen pixels.
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param w  Width in pixels (ignored).
/// @param h  Height in pixels (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_rect2d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawRect2D: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawCrosshair` — would normally draw a
///        small crosshair at the center of the screen (FPS reticle).
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
/// @param sz Crosshair arm length in pixels (ignored).
void rt_canvas3d_draw_crosshair(void *c, int64_t cl, int64_t sz) {
    (void)c;
    (void)cl;
    (void)sz;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawCrosshair: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawText2D` — would normally draw 8x8 bitmap
///        text in screen space as a HUD overlay.
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param t  Text string (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_text2d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawText2D: graphics support not compiled in");
}

/* Debug gizmo stubs */

/// @brief Stub for `Canvas3D.DrawAABBWire` — would normally draw the
///        12 edges of an axis-aligned bounding box in world space (debug
///        visualization for collider/cull bounds).
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_aabb_wire(void *c, void *mn, void *mx, int64_t cl) {
    (void)c;
    (void)mn;
    (void)mx;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawAABBWire: graphics support not compiled in");
}

void rt_canvas3d_draw_aabb_wire_raw(void *c, const double *mn, const double *mx, int64_t cl) {
    (void)c;
    (void)mn;
    (void)mx;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawAABBWireRaw: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawSphereWire` — would normally draw a
///        wireframe sphere (3 great-circle rings) at world position `ctr`
///        with radius `r`.
///
/// Trapping stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param ctr Vec3 sphere center (ignored).
/// @param r   Sphere radius (ignored).
/// @param cl  Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_sphere_wire(void *c, void *ctr, double r, int64_t cl) {
    (void)c;
    (void)ctr;
    (void)r;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawSphereWire: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawDebugRay` — would normally draw a
///        finite ray starting at world position `o` in direction `d` with
///        length `l` (debug visualization for raycasts).
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param o  Vec3 ray origin (ignored).
/// @param d  Vec3 ray direction (ignored).
/// @param l  Ray length in world units (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_debug_ray(void *c, void *o, void *d, double l, int64_t cl) {
    (void)c;
    (void)o;
    (void)d;
    (void)l;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawDebugRay: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawAxis` — would normally draw the world-
///        axes gizmo at world position `o` with length `s` (red=X, green=Y,
///        blue=Z).
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param o Vec3 origin (ignored).
/// @param s Axis arm length in world units (ignored).
void rt_canvas3d_draw_axis(void *c, void *o, double s) {
    (void)c;
    (void)o;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawAxis: graphics support not compiled in");
}

/* Fog stubs */

/// @brief Stub for `Canvas3D.SetFog` — enable linear distance fog
///        between near distance `n` and far distance `f`. Pixels beyond
///        `f` are tinted entirely with `(r, g, b)`; pixels at `n` are
///        unaffected; in between is linearly interpolated.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param n Fog start distance (ignored).
/// @param f Fog end distance (ignored).
/// @param r Fog tint red, 0..1 (ignored).
/// @param g Fog tint green, 0..1 (ignored).
/// @param b Fog tint blue, 0..1 (ignored).
void rt_canvas3d_set_fog(void *c, double n, double f, double r, double g, double b) {
    (void)c;
    (void)n;
    (void)f;
    (void)r;
    (void)g;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetFog: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.ClearFog` — disable linear distance fog
///        for subsequent draws.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_clear_fog(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.ClearFog: graphics support not compiled in");
}

/* Shadow stubs */

/// @brief Stub for `Canvas3D.EnableShadows` — enable shadow mapping at
///        the given resolution `(r x r)` for the shadow framebuffer.
///        Higher values give crisper shadows but cost more memory and
///        fillrate.
///
/// Trapping stub. Real implementation runs a depth-only pass per
/// shadow-casting light and samples the depth map during the lit pass
/// with PCF filtering.
///
/// @param c Canvas3D handle (ignored).
/// @param r Shadow map resolution per side in pixels (ignored).
void rt_canvas3d_enable_shadows(void *c, int64_t r) {
    (void)c;
    (void)r;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.EnableShadows: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DisableShadows` — turn off shadow mapping
///        entirely. Cheaper but loses contact shadows / depth cues.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_disable_shadows(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DisableShadows: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.SetShadowBias` — depth-bias offset added
///        to shadow-map samples to prevent self-shadow acne. Higher values
///        reduce acne but introduce "Peter Panning" (shadow detached from
///        the caster).
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param b Shadow bias depth offset (ignored).
void rt_canvas3d_set_shadow_bias(void *c, double b) {
    (void)c;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetShadowBias: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.SetShadowSlopeBias` — would normally apply a slope-scaled
///        rasterization bias to shadow-map caster draws.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param b Shadow slope bias (ignored).
void rt_canvas3d_set_shadow_slope_bias(void *c, double b) {
    (void)c;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetShadowSlopeBias: graphics support not compiled in");
}

void rt_canvas3d_set_shadow_cascades(void *c, int64_t count) {
    (void)c;
    (void)count;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetShadowCascades: graphics support not compiled in");
}

/* Camera shake/follow stubs */

/// @brief Stub for `Camera3D.Shake` — start a camera-shake effect.
///        Intensity `i` is the maximum offset magnitude (world units),
///        duration `d` is the total shake time (seconds), and decay `dc`
///        controls how quickly the intensity falls off.
///
/// Trapping stub. The real implementation uses Perlin-noise-driven
/// per-axis offsets that taper off over the duration.
///
/// @param c  Camera3D handle (ignored).
/// @param i  Initial intensity (ignored).
/// @param d  Total duration in seconds (ignored).
/// @param dc Decay rate, 0..1 (higher = faster falloff) (ignored).
void rt_camera3d_shake(void *c, double i, double d, double dc) {
    (void)c;
    (void)i;
    (void)d;
    (void)dc;
    RT_GRAPHICS_TRAP_VOID("Camera3D.Shake: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.SmoothFollow` — third-person follow
///        controller. Tracks target Vec3 `t` at distance `d` behind, with
///        height offset `h`, smoothing speed `s`, advanced over `dt`
///        seconds. Camera looks at the target each tick.
///
/// Trapping stub.
///
/// @param c  Camera3D handle (ignored).
/// @param t  Vec3 follow target (ignored).
/// @param d  Distance behind target (ignored).
/// @param h  Height above target (ignored).
/// @param s  Smoothing speed (higher = snappier) (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_camera3d_smooth_follow(void *c, void *t, double d, double h, double s, double dt) {
    (void)c;
    (void)t;
    (void)d;
    (void)h;
    (void)s;
    (void)dt;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SmoothFollow: graphics support not compiled in");
}

/// @brief Stub for `Camera3D.SmoothLookAt` — would normally interpolate
///        the camera's view direction toward `t` over `dt` seconds at speed
///        `s` (good for cinematic camera handovers).
///
/// Trapping stub.
///
/// @param c  Camera3D handle (ignored).
/// @param t  Vec3 look-at target (ignored).
/// @param s  Smoothing speed; higher = snappier (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_camera3d_smooth_look_at(void *c, void *t, double s, double dt) {
    (void)c;
    (void)t;
    (void)s;
    (void)dt;
    RT_GRAPHICS_TRAP_VOID("Camera3D.SmoothLookAt: graphics support not compiled in");
}

/* InstanceBatch3D stubs */

/// @brief Stub for `InstanceBatch3D.New` — would normally create an
///        instanced-rendering batch that draws many copies of the same
///        Mesh3D + Material3D pair in a single GPU draw call. Each
///        instance has its own per-instance transform.
///
/// Trapping stub: the returned batch is later used by draw APIs, so a NULL
/// batch would move the failure away from the unsupported constructor.
///
/// @param m  Mesh3D handle (the geometry to instance) (ignored).
/// @param mt Material3D handle (shared by all instances) (ignored).
///
/// @return Never returns normally; the fallback NULL only satisfies the C type checker.
void *rt_instbatch3d_new(void *m, void *mt) {
    (void)m;
    (void)mt;
    RT_GRAPHICS_TRAP_RET("InstanceBatch3D.New: graphics support not compiled in", NULL);
}

/// @brief Stub for `InstanceBatch3D.Add` — append a new instance with
///        the given transform. Returns the assigned slot index implicitly
///        (insertion order). The batch grows as needed.
///
/// Trapping stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param t Transform3D handle for this instance (ignored).
void rt_instbatch3d_add(void *b, void *t) {
    (void)b;
    (void)t;
    RT_GRAPHICS_TRAP_VOID("InstanceBatch3D.Add: graphics support not compiled in");
}

/// @brief Stub for `InstanceBatch3D.Remove` — remove the instance at
///        slot `i` via swap-with-last (O(1) but reorders remaining
///        instances).
///
/// Trapping stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param i Instance slot index (ignored).
void rt_instbatch3d_remove(void *b, int64_t i) {
    (void)b;
    (void)i;
    RT_GRAPHICS_TRAP_VOID("InstanceBatch3D.Remove: graphics support not compiled in");
}

/// @brief Stub for `InstanceBatch3D.Set` — overwrite the transform of
///        an existing instance at slot `i`. Used for animating instance
///        positions every frame (e.g., a cloud of bullets).
///
/// Trapping stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param i Instance slot index (ignored).
/// @param t Transform3D handle for the new transform (ignored).
void rt_instbatch3d_set(void *b, int64_t i, void *t) {
    (void)b;
    (void)i;
    (void)t;
    RT_GRAPHICS_TRAP_VOID("InstanceBatch3D.Set: graphics support not compiled in");
}

/// @brief Stub for `InstanceBatch3D.Clear` — remove all instances. The
///        underlying buffers are kept allocated for cheap re-fill.
///
/// Trapping stub.
///
/// @param b InstanceBatch3D handle (ignored).
void rt_instbatch3d_clear(void *b) {
    (void)b;
    RT_GRAPHICS_TRAP_VOID("InstanceBatch3D.Clear: graphics support not compiled in");
}

/// @brief Stub for `InstanceBatch3D.Count` — number of instances
///        currently in the batch.
///
/// Silent stub returning `0`.
///
/// @param b InstanceBatch3D handle (ignored).
///
/// @return `0`.
int64_t rt_instbatch3d_count(void *b) {
    (void)b;
    return 0;
}

/// @brief Stub for `Canvas3D.DrawInstanced` — render every instance in
///        the batch in a single GPU draw call (when GPU instancing is
///        available) or as N individual draws (software fallback).
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param b InstanceBatch3D handle (ignored).
void rt_canvas3d_draw_instanced(void *c, void *b) {
    (void)c;
    (void)b;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawInstanced: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawTerrain` — render the Terrain3D using
///        LOD selection, frustum culling, splat-map sampling, and skirt
///        triangles.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param t Terrain3D handle (ignored).
void rt_canvas3d_draw_terrain(void *c, void *t) {
    (void)c;
    (void)t;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawTerrain: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawMeshBlended` — variant of `DrawMesh`
///        that uses an AnimBlend3D's blended pose for skinning instead
///        of a single AnimPlayer3D's pose.
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param m  Mesh3D handle (ignored).
/// @param t  Transform3D handle (ignored).
/// @param mt Material3D handle (ignored).
/// @param bl AnimBlend3D handle providing the blended pose (ignored).
void rt_canvas3d_draw_mesh_blended(void *c, void *m, void *t, void *mt, void *bl) {
    (void)c;
    (void)m;
    (void)t;
    (void)mt;
    (void)bl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawMeshBlended: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawDecal` — would normally render the
///        given Decal3D as a projected-texture pass on top of the scene.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param d Decal3D handle (ignored).
void rt_canvas3d_draw_decal(void *c, void *d) {
    (void)c;
    (void)d;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawDecal: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawSprite3D` — would normally render the
///        given Sprite3D as a camera-facing textured quad at its world
///        position.
///
/// Trapping stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param s   Sprite3D handle (ignored).
/// @param cam Camera3D handle (used for billboard orientation) (ignored).
void rt_canvas3d_draw_sprite3d(void *c, void *s, void *cam) {
    (void)c;
    (void)s;
    (void)cam;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawSprite3D: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawWater` — render the Water3D surface
///        with refraction, reflection (when env-map bound), and lighting.
///
/// Trapping stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param w   Water3D handle (ignored).
/// @param cam Camera3D handle (ignored).
void rt_canvas3d_draw_water(void *c, void *w, void *cam) {
    (void)c;
    (void)w;
    (void)cam;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawWater: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawVegetation` — render every visible
///        blade in a single instanced GPU draw call (when the backend
///        supports `submit_draw_instanced`) or as N individual draws
///        on the software fallback.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param v Vegetation3D handle (ignored).
void rt_canvas3d_draw_vegetation(void *c, void *v) {
    (void)c;
    (void)v;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawVegetation: graphics support not compiled in");
}

/* PostFX F5-F7 stubs */

/// @brief Stub for `PostFX3D.AddSSAO` — append a Screen-Space Ambient
///        Occlusion pass to the chain. Darkens crevices and concave
///        regions based on geometry from the depth buffer.
///
/// Trapping stub.
///
/// @param p PostFX3D handle (ignored).
/// @param r Sample radius in screen pixels (ignored).
/// @param i Effect intensity (ignored).
/// @param s Sample count per fragment (higher = better quality, slower) (ignored).
void rt_postfx3d_add_ssao(void *p, double r, double i, int64_t s) {
    (void)p;
    (void)r;
    (void)i;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddSSAO: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddDOF` — append a Depth-of-Field blur
///        pass. Pixels at distances other than `f` (focus distance) are
///        blurred with circle-of-confusion radius proportional to depth
///        delta and aperture `a`.
///
/// Trapping stub.
///
/// @param p PostFX3D handle (ignored).
/// @param f Focus distance in world units (ignored).
/// @param a Aperture / blur strength (ignored).
/// @param m Maximum blur radius in screen pixels (ignored).
void rt_postfx3d_add_dof(void *p, double f, double a, double m) {
    (void)p;
    (void)f;
    (void)a;
    (void)m;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddDOF: graphics support not compiled in");
}

/// @brief Stub for `PostFX3D.AddMotionBlur` — append a per-pixel motion
///        blur pass driven by per-fragment motion vectors (`currClip` vs
///        `prevClip` from the GBuffer).
///
/// Trapping stub.
///
/// @param p PostFX3D handle (ignored).
/// @param i Blur intensity multiplier (ignored).
/// @param s Sample count along motion vector (ignored).
void rt_postfx3d_add_motion_blur(void *p, double i, int64_t s) {
    (void)p;
    (void)i;
    (void)s;
    RT_GRAPHICS_TRAP_VOID("PostFX3D.AddMotionBlur: graphics support not compiled in");
}

/// @brief Stub for `vgfx3d_postfx_get_snapshot` — backend-facing accessor
///        that copies the active PostFX3D parameters into an opaque
///        snapshot struct. Decouples GPU backends from PostFX3D's private
///        layout so the same snapshot can drive Metal / D3D11 / OpenGL.
///
/// Silent stub returning `0` (nothing copied) and ignoring `out`.
///
/// @param postfx PostFX3D handle (ignored).
/// @param out    Snapshot destination (ignored).
///
/// @return `0`.
int vgfx3d_postfx_get_snapshot(void *postfx, vgfx3d_postfx_snapshot_t *out) {
    (void)postfx;
    (void)out;
    return 0;
}

/* Opaque front-to-back sorting stub */

/// @brief Stub for `Canvas3D.SetFrustumCulling` — when enabled, opaque
///        meshes can be frustum-rejected and sorted front-to-back before
///        fragment shading.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param e Non-zero to enable (ignored).
void rt_canvas3d_set_frustum_culling(void *c, int8_t e) {
    (void)c;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetFrustumCulling: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.SetOcclusionCulling` — enable frustum rejection
///        plus conservative CPU occlusion skips.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
/// @param e Non-zero to enable (ignored).
void rt_canvas3d_set_occlusion_culling(void *c, int8_t e) {
    (void)c;
    (void)e;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.SetOcclusionCulling: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.Begin2D` — switch into 2D overlay mode for
///        screen-space draws (HUD / UI). Disables depth testing and binds
///        an orthographic projection matrix sized to the viewport.
///
/// Trapping stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_begin_2d(void *c) {
    (void)c;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.Begin2D: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawRect3D` — would normally draw a 2D rect
///        in screen space (after `Begin2D`). The `_3d` suffix is historical
///        — the call is screen-space, not world-space.
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param w  Width in pixels (ignored).
/// @param h  Height in pixels (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_rect_3d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawRect3D: graphics support not compiled in");
}

/// @brief Stub for `Canvas3D.DrawText3D` — would normally draw screen-space
///        text in the 8x8 bitmap font during a 2D overlay pass.
///
/// Trapping stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param t  Glyph source string (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_text_3d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
    RT_GRAPHICS_TRAP_VOID("Canvas3D.DrawText3D: graphics support not compiled in");
}
