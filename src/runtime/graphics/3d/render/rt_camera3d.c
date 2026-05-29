//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_camera3d.c
// Purpose: Viper.Graphics3D.Camera3D — perspective camera with view/projection.
//   Uses existing Mat4 perspective/lookAt math from rt_mat4.c.
//
// Key invariants:
//   - Matrices stored as double[16] row-major (matching Mat4 convention)
//   - OpenGL NDC convention: Z in [-1, 1]
//   - Right-handed: +X right, +Y up, +Z toward viewer
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, rt_mat4.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdint.h>
#include <string.h>

#define CAMERA3D_WORLD_ABS_MAX 1000000000000.0
#define CAMERA3D_CLIP_MAX 1000000000000.0
#define CAMERA3D_ASPECT_MAX 1000000.0
#define CAMERA3D_ORTHO_SIZE_MAX 1000000000.0
#define CAMERA3D_FLOAT_ABS_MAX 3.40282346638528859812e38

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
#include "rt_trap.h"

/* Access existing Vec3 and Mat4 public API */
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

static void build_ortho(double *m,
                        double left,
                        double right,
                        double bottom,
                        double top,
                        double near_val,
                        double far_val);

/// @brief Return `value` when finite, else `fallback`. Scalar boundary sanitizer.
static double finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp `value` into `[-max_abs, max_abs]`, substituting `fallback` when not finite.
static double clamp_abs_or(double value, double fallback, double max_abs) {
    value = finite_or(value, fallback);
    if (value > max_abs)
        return max_abs;
    if (value < -max_abs)
        return -max_abs;
    return value;
}

/// @brief True if `value` is finite and within ±CAMERA3D_FLOAT_ABS_MAX (safe to narrow to float).
static int camera_value_fits_float(double value) {
    return isfinite(value) && value >= -CAMERA3D_FLOAT_ABS_MAX && value <= CAMERA3D_FLOAT_ABS_MAX;
}

/// @brief Clamp `value` to `[0, +∞)`, substituting `fallback` when not finite.
/// @details Used for camera knobs (FOV, clip distances, exposure) where negatives would
///   invert the geometry or exponent and NaN would propagate into the view matrix.
static double sanitize_nonnegative(double value, double fallback) {
    value = finite_or(value, fallback);
    if (value < 0.0)
        return 0.0;
    return value > CAMERA3D_WORLD_ABS_MAX ? CAMERA3D_WORLD_ABS_MAX : value;
}

/// @brief Component-wise fallback for a 3-vector — each non-finite lane uses `fallback[i]`.
/// @details Used to recover camera position / target / up when the caller hands in a
///   Vec3 containing NaNs (e.g., from an earlier divide-by-zero); substituting known-good
///   fallbacks keeps the view matrix construction from producing a non-invertible matrix.
static void sanitize_vec3(double v[3], const double fallback[3]) {
    if (!v || !fallback)
        return;
    for (int i = 0; i < 3; i++)
        v[i] = clamp_abs_or(v[i], fallback[i], CAMERA3D_WORLD_ABS_MAX);
}

/// @brief Clamp aspect ratio away from zero. Values ≤ 1e-6 (including negatives and
/// zero) fall back to 1.0 so `build_perspective`'s `f / aspect` divide never produces
/// infinity or a negative scale. Used at every projection-building site.
static double sanitize_aspect(double aspect) {
    if (!isfinite(aspect) || aspect <= 1e-6)
        return 1.0;
    return aspect > CAMERA3D_ASPECT_MAX ? CAMERA3D_ASPECT_MAX : aspect;
}

/// @brief Guard the near/far clip planes against degenerate configurations. Forces near
/// ≥ 0.1 (prevents z-buffer precision collapse at very small near) and ensures far
/// exceeds near by at least ~1e-4 — defaulting far to near + 1000 when the caller passes
/// a degenerate pair. Both arguments are in-out so caller state stays valid.
static void sanitize_clip_planes(double *near_val, double *far_val) {
    if (!near_val || !far_val)
        return;
    if (!isfinite(*near_val) || *near_val <= 1e-4)
        *near_val = 0.1;
    if (*near_val > CAMERA3D_CLIP_MAX)
        *near_val = 0.1;
    if (!isfinite(*far_val) || *far_val <= *near_val + 1e-4)
        *far_val = *near_val + 1000.0;
    if (*far_val > CAMERA3D_CLIP_MAX)
        *far_val = CAMERA3D_CLIP_MAX;
    if (*far_val <= *near_val + 1e-4) {
        *near_val = 0.1;
        *far_val = 1000.1;
    }
}

/// @brief Clamp vertical FOV to the usable `[1°, 179°]` range. Below 1° the perspective
/// `tan(fov/2)` divisor approaches zero and the projection blows up; above 179° the
/// forward basis effectively flips. Real-world content lives in ~30–100° so this clamp
/// only kicks in for malformed input.
static double sanitize_fov(double fov_deg) {
    if (!isfinite(fov_deg))
        return 60.0;
    if (fov_deg < 1.0)
        return 1.0;
    if (fov_deg > 179.0)
        return 179.0;
    return fov_deg;
}

/// @brief Clamp orthographic view-volume half-size away from zero — same logic as
/// `sanitize_aspect` but for the ortho height parameter. Prevents a zero-size ortho
/// projection from collapsing into a divide-by-zero during matrix construction.
static double sanitize_ortho_size(double size) {
    if (!isfinite(size) || size <= 1e-6)
        return 1.0;
    return size > CAMERA3D_ORTHO_SIZE_MAX ? CAMERA3D_ORTHO_SIZE_MAX : size;
}

/// @brief Normalize the (`*x`,`*y`,`*z`) triple in place, reverting to the
///        fallback direction when any lane is non-finite or the vector length
///        is ~zero. Keeps camera basis vectors unit-length and well-defined.
static void camera_normalize_vec3_or(
    double *x, double *y, double *z, double fallback_x, double fallback_y, double fallback_z) {
    double vx = finite_or(x ? *x : fallback_x, fallback_x);
    double vy = finite_or(y ? *y : fallback_y, fallback_y);
    double vz = finite_or(z ? *z : fallback_z, fallback_z);
    double len = sqrt(vx * vx + vy * vy + vz * vz);
    if (!isfinite(len) || len <= 1e-12) {
        vx = fallback_x;
        vy = fallback_y;
        vz = fallback_z;
    } else {
        vx /= len;
        vy /= len;
        vz /= len;
    }
    if (x)
        *x = vx;
    if (y)
        *y = vy;
    if (z)
        *z = vz;
}

/// @brief Build an OpenGL-style perspective projection matrix into `m` (row-major).
/// `f = 1 / tan(fov/2)` sets vertical scale; horizontal divides by aspect. Writes depth
/// in the `[-1, 1]` NDC convention. With Viper's row-major, column-vector transform
/// path, `m[14] = -1` makes clip.w = -view.z for perspective division. Zeroes the matrix first so
/// unused slots stay at 0 — callers can assume a fully initialised 4×4.
static void build_perspective(double *m, double fov_deg, double aspect, double near, double far) {
    memset(m, 0, 16 * sizeof(double));
    double fov_rad = fov_deg * (M_PI / 180.0);
    double f = 1.0 / tan(fov_rad * 0.5);
    double nf = 1.0 / (near - far);

    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) * nf; /* OpenGL NDC: Z in [-1,1] */
    m[11] = 2.0 * far * near * nf;
    m[14] = -1.0;
    /* m[15] = 0 */
}

/// @brief Build a right-handed look-at view matrix into `m`. Forward = normalize(eye -
/// target); right = normalize(cross(up, forward)); true-up = cross(forward, right).
/// Handles two degenerate cases: (1) eye == target (flen ≈ 0) falls back to +Z forward
/// while preserving camera translation — avoids producing an identity with the
/// translation stripped; (2) up parallel to forward (rlen ≈ 0) picks an orthogonal
/// fallback axis based on which world axis dominates the forward vector. Final
/// fallback is the world +X basis if both attempts collapse. Matches
/// `rt_mat4_look_at` bit-for-bit so the Canvas3D projection and Camera3D forward
/// vectors stay in lockstep.
static void build_look_at(double *m, const double *eye, const double *target, const double *up) {
    static const double fallback_eye[3] = {0.0, 0.0, 0.0};
    static const double fallback_target[3] = {0.0, 0.0, -1.0};
    static const double fallback_up_vec[3] = {0.0, 1.0, 0.0};
    double clean_eye[3] = {eye ? eye[0] : fallback_eye[0],
                           eye ? eye[1] : fallback_eye[1],
                           eye ? eye[2] : fallback_eye[2]};
    double clean_target[3] = {target ? target[0] : fallback_target[0],
                              target ? target[1] : fallback_target[1],
                              target ? target[2] : fallback_target[2]};
    double clean_up[3] = {up ? up[0] : fallback_up_vec[0],
                          up ? up[1] : fallback_up_vec[1],
                          up ? up[2] : fallback_up_vec[2]};

    sanitize_vec3(clean_eye, fallback_eye);
    sanitize_vec3(clean_target, fallback_target);
    sanitize_vec3(clean_up, fallback_up_vec);
    eye = clean_eye;
    target = clean_target;
    up = clean_up;

    /* Forward = normalize(eye - target) — right-handed.
     * If eye == target (flen ≈ 0), fall back to the default forward axis
     * while preserving the camera translation. */
    double fx = eye[0] - target[0];
    double fy = eye[1] - target[1];
    double fz = eye[2] - target[2];
    double flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (!isfinite(flen) || flen < 1e-12) {
        fx = 0.0;
        fy = 0.0;
        fz = 1.0;
        flen = 1.0;
    }
    fx /= flen;
    fy /= flen;
    fz /= flen;

    /* Right = normalize(cross(up, forward)) */
    double rx = up[1] * fz - up[2] * fy;
    double ry = up[2] * fx - up[0] * fz;
    double rz = up[0] * fy - up[1] * fx;
    double rlen = sqrt(rx * rx + ry * ry + rz * rz);
    if (!isfinite(rlen) || rlen <= 1e-12) {
        /* When forward is near-vertical (|fy| > 0.99) the world-Y up hint is
         * degenerate, so fall back to world +Z; otherwise use world +Y. */
        const double fallback_up[3] = {
            0.0, fabs(fy) > 0.99 ? 0.0 : 1.0, fabs(fy) > 0.99 ? 1.0 : 0.0};
        rx = fallback_up[1] * fz - fallback_up[2] * fy;
        ry = fallback_up[2] * fx - fallback_up[0] * fz;
        rz = fallback_up[0] * fy - fallback_up[1] * fx;
        rlen = sqrt(rx * rx + ry * ry + rz * rz);
    }
    if (isfinite(rlen) && rlen > 1e-12) {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
    } else {
        rx = 1.0;
        ry = 0.0;
        rz = 0.0;
    }

    /* True up = cross(forward, right) */
    double ux = fy * rz - fz * ry;
    double uy = fz * rx - fx * rz;
    double uz = fx * ry - fy * rx;

    memset(m, 0, 16 * sizeof(double));
    m[0] = rx;
    m[1] = ry;
    m[2] = rz;
    m[3] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
    m[4] = ux;
    m[5] = uy;
    m[6] = uz;
    m[7] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    m[8] = fx;
    m[9] = fy;
    m[10] = fz;
    m[11] = -(fx * eye[0] + fy * eye[1] + fz * eye[2]);
    m[15] = 1.0;
}

/// @brief Recompute the camera's projection matrix from current FOV / aspect / clip / ortho size.
/// @details Called whenever any projection input changes. Routes to
///          `build_ortho` or `build_perspective` based on `cam->is_ortho`,
///          re-sanitising the inputs first so callers can pass any value
///          and still produce a valid projection (degenerate aspects or
///          clip planes are clamped).
static void rebuild_projection(rt_camera3d *cam) {
    if (!cam)
        return;
    cam->aspect = sanitize_aspect(cam->aspect);
    sanitize_clip_planes(&cam->near_plane, &cam->far_plane);
    if (cam->is_ortho) {
        double half_h = sanitize_ortho_size(cam->ortho_size);
        double half_w = half_h * cam->aspect;
        cam->ortho_size = half_h;
        build_ortho(
            cam->projection, -half_w, half_w, -half_h, half_h, cam->near_plane, cam->far_plane);
    } else {
        cam->fov = sanitize_fov(cam->fov);
        build_perspective(cam->projection, cam->fov, cam->aspect, cam->near_plane, cam->far_plane);
    }
}

/// @brief Update the camera's aspect ratio to match the active output and rebuild projection.
/// @details Called by Canvas3D / RenderTarget3D `Begin` when the render
///          surface's dimensions change (window resize or render-target
///          rebind). No-op when the new aspect matches within 1e-9, so
///          repeated calls with the same dimensions don't churn the
///          projection matrix.
void rt_camera3d_sync_render_aspect(void *obj, double aspect) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    double sanitized_aspect;

    if (!cam)
        return;
    sanitized_aspect = sanitize_aspect(aspect);
    if (fabs(cam->aspect - sanitized_aspect) <= 1e-9)
        return;
    cam->aspect = sanitized_aspect;
    rebuild_projection(cam);
}

/// @brief Build the projection matrix a render pass should use for the given aspect.
/// @details Unlike `rt_camera3d_sync_render_aspect`, this does not mutate the
///          camera object. Canvas3D uses it so the same camera can be reused
///          across outputs with different aspect ratios without permanently
///          rewriting `cam->aspect` / `cam->projection`.
void rt_camera3d_get_render_projection(void *obj, double aspect_override, float *out_projection) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    double projection[16];
    double aspect;
    double near_plane;
    double far_plane;

    if (!cam || !out_projection)
        return;

    aspect = sanitize_aspect(aspect_override > 1e-6 ? aspect_override : cam->aspect);
    near_plane = cam->near_plane;
    far_plane = cam->far_plane;
    sanitize_clip_planes(&near_plane, &far_plane);

    if (cam->is_ortho) {
        double half_h = sanitize_ortho_size(cam->ortho_size);
        double half_w = half_h * aspect;
        build_ortho(projection, -half_w, half_w, -half_h, half_h, near_plane, far_plane);
    } else {
        build_perspective(projection, sanitize_fov(cam->fov), aspect, near_plane, far_plane);
    }

    for (int i = 0; i < 16; i++) {
        double identity = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0 : 0.0;
        out_projection[i] =
            (float)(camera_value_fits_float(projection[i]) ? projection[i] : identity);
    }
}

/// @brief Recover the FPS yaw/pitch state from the current view matrix.
/// @details Called after `LookAt` or other view-replacing ops so the next
///          FPS-style mouse delta starts from the correct heading. Forward
///          is row 2 of the view matrix (negated because view stores the
///          inverse camera basis), so yaw = atan2(fx, -fz) and pitch =
///          asin(fy). Pitch is clamped to ±89° so the next rebuild can't
///          produce a degenerate look-at where forward becomes parallel
///          to the world up vector.
static void camera_sync_fps_angles_from_view(rt_camera3d *cam) {
    double fx;
    double fy;
    double fz;

    if (!cam)
        return;
    fx = finite_or(-cam->view[8], 0.0);
    fy = finite_or(-cam->view[9], 0.0);
    fz = finite_or(-cam->view[10], -1.0);
    cam->fps_yaw = atan2(fx, -fz) * (180.0 / M_PI);
    cam->fps_pitch = asin(fmax(-1.0, fmin(1.0, fy))) * (180.0 / M_PI);
    if (cam->fps_pitch > 89.0)
        cam->fps_pitch = 89.0;
    if (cam->fps_pitch < -89.0)
        cam->fps_pitch = -89.0;
}

/// @brief Rebuild the view matrix from the cached FPS yaw/pitch and eye position.
/// @details Constructs a target one unit ahead of the eye along the
///          spherical-coordinate forward (yaw rotates in the XZ plane,
///          pitch tilts up/down) and feeds it to `build_look_at`. World
///          up is fixed +Y. Called every frame an FPS controller mutates
///          yaw/pitch via `RotateYaw` / `RotatePitch` / `LookDelta`.
static void camera_rebuild_fps_view(rt_camera3d *cam) {
    double yaw_rad;
    double pitch_rad;
    double cp;
    double target[3];
    double up[3] = {0.0, 1.0, 0.0};

    if (!cam)
        return;
    cam->fps_yaw = finite_or(cam->fps_yaw, 0.0);
    cam->fps_pitch = finite_or(cam->fps_pitch, 0.0);
    yaw_rad = cam->fps_yaw * (M_PI / 180.0);
    pitch_rad = cam->fps_pitch * (M_PI / 180.0);
    cp = cos(pitch_rad);
    target[0] = cam->eye[0] + sin(yaw_rad) * cp;
    target[1] = cam->eye[1] + sin(pitch_rad);
    target[2] = cam->eye[2] - cos(yaw_rad) * cp;
    build_look_at(cam->view, cam->eye, target, up);
}

/// @brief Re-derive the view matrix using the eye plus the current shake offset.
/// @details Reads forward from the existing view (row 2 negated), shifts
///          eye by the current shake delta, then builds a fresh look-at
///          targeting eye+forward so heading stays identical — only
///          translation jitters. The shake offset itself is updated
///          per-frame by `apply_shake`.
static void camera_apply_shake_to_view(rt_camera3d *cam) {
    double forward[3];
    double eye[3];
    double target[3];
    double up[3];

    if (!cam)
        return;

    forward[0] = finite_or(-cam->view[8], 0.0);
    forward[1] = finite_or(-cam->view[9], 0.0);
    forward[2] = finite_or(-cam->view[10], -1.0);
    up[0] = finite_or(cam->view[4], 0.0);
    up[1] = finite_or(cam->view[5], 1.0);
    up[2] = finite_or(cam->view[6], 0.0);
    eye[0] = finite_or(cam->eye[0], 0.0) + finite_or(cam->shake_offset[0], 0.0);
    eye[1] = finite_or(cam->eye[1], 0.0) + finite_or(cam->shake_offset[1], 0.0);
    eye[2] = finite_or(cam->eye[2], 0.0) + finite_or(cam->shake_offset[2], 0.0);
    target[0] = eye[0] + forward[0];
    target[1] = eye[1] + forward[1];
    target[2] = eye[2] + forward[2];
    build_look_at(cam->view, eye, target, up);
}

/// @brief Create a perspective camera with the given field of view and clipping planes.
/// @details Uses a right-handed coordinate system (+X right, +Y up, +Z toward viewer)
///          with OpenGL NDC convention (Z in [-1, 1]). The camera starts at the
///          origin looking along -Z. View and projection matrices are stored as
///          double[16] row-major, matching Mat4 conventions.
/// @param fov      Vertical field of view in degrees.
/// @param aspect   Width/height aspect ratio.
/// @param near_val Near clipping plane distance.
/// @param far_val  Far clipping plane distance.
/// @return Opaque camera handle, or NULL on failure.
void *rt_camera3d_new(double fov, double aspect, double near_val, double far_val) {
    rt_camera3d *cam =
        (rt_camera3d *)rt_obj_new_i64(RT_G3D_CAMERA3D_CLASS_ID, (int64_t)sizeof(rt_camera3d));
    if (!cam) {
        rt_trap("Camera3D.New: memory allocation failed");
        return NULL;
    }
    cam->vptr = NULL;
    cam->fov = sanitize_fov(fov);
    cam->aspect = sanitize_aspect(aspect);
    cam->near_plane = near_val;
    cam->far_plane = far_val;

    /* Default identity view (camera at origin looking along -Z) */
    memset(cam->view, 0, 16 * sizeof(double));
    cam->view[0] = cam->view[5] = cam->view[10] = cam->view[15] = 1.0;
    cam->eye[0] = cam->eye[1] = cam->eye[2] = 0.0;
    cam->fps_yaw = 0.0;
    cam->fps_pitch = 0.0;
    cam->shake_intensity = 0.0;
    cam->shake_duration = 0.0;
    cam->shake_decay = 5.0;
    cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
    cam->shake_seed = 0x12345678;
    cam->is_ortho = 0;
    cam->ortho_size = 10.0;
    cam->pick_cache_valid = 0;
    rebuild_projection(cam);

    return cam;
}

/// @brief Construct a row-major orthographic projection matrix into `m`.
/// @details Maps the axis-aligned view volume `[left,right] × [bottom,top] ×
///   [near,far]` to the OpenGL-style clip cube `[-1, 1]^3`. Z is flipped
///   because view space uses -Z forward while NDC uses +Z forward, giving
///   `-2 / (far - near)` on the diagonal. The translation column centers the
///   volume on the origin: `(r+l)/(r-l)` is negated to move "left" to -1.
///   Degenerate inputs (any axis spanning less than 1e-12) zero the output
///   rather than generating NaN/Inf so downstream matrix multiplies stay
///   finite — callers can detect the collapse via a post-build check if
///   needed.
static void build_ortho(double *m,
                        double left,
                        double right,
                        double bottom,
                        double top,
                        double near_val,
                        double far_val) {
    memset(m, 0, 16 * sizeof(double));
    double rl = right - left;
    double tb = top - bottom;
    double fn = far_val - near_val;
    if (fabs(rl) < 1e-12 || fabs(tb) < 1e-12 || fabs(fn) < 1e-12)
        return;
    m[0] = 2.0 / rl;
    m[5] = 2.0 / tb;
    m[10] = -2.0 / fn;
    m[3] = -(right + left) / rl;
    m[7] = -(top + bottom) / tb;
    m[11] = -(far_val + near_val) / fn;
    m[15] = 1.0;
}

/// @brief Create an orthographic camera (parallel projection, no perspective foreshortening).
/// @details Useful for 2D games rendered in a 3D pipeline, UI overlays, shadow maps,
///          and minimap views. The size parameter controls the vertical extent.
/// @param size     Half-height of the orthographic view volume.
/// @param aspect   Width/height aspect ratio.
/// @param near_val Near clipping plane distance.
/// @param far_val  Far clipping plane distance.
/// @return Opaque camera handle, or NULL on failure.
void *rt_camera3d_new_ortho(double size, double aspect, double near_val, double far_val) {
    rt_camera3d *cam =
        (rt_camera3d *)rt_obj_new_i64(RT_G3D_CAMERA3D_CLASS_ID, (int64_t)sizeof(rt_camera3d));
    if (!cam) {
        rt_trap("Camera3D.NewOrtho: memory allocation failed");
        return NULL;
    }
    cam->vptr = NULL;
    cam->fov = 0;
    cam->aspect = sanitize_aspect(aspect);
    cam->near_plane = near_val;
    cam->far_plane = far_val;
    cam->is_ortho = 1;
    cam->ortho_size = sanitize_ortho_size(size);

    /* Default identity view */
    memset(cam->view, 0, 16 * sizeof(double));
    cam->view[0] = cam->view[5] = cam->view[10] = cam->view[15] = 1.0;
    cam->eye[0] = cam->eye[1] = cam->eye[2] = 0.0;
    cam->fps_yaw = 0.0;
    cam->fps_pitch = 0.0;
    cam->shake_intensity = 0.0;
    cam->shake_duration = 0.0;
    cam->shake_decay = 5.0;
    cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
    cam->shake_seed = 0x12345678;
    rebuild_projection(cam);

    return cam;
}

/// @brief Return non-zero when the camera uses an orthographic projection.
/// @details Lets renderers branch on perspective vs ortho without poking
///          into private struct fields. Returns 0 for null obj or for
///          perspective cameras built via `rt_camera3d_new`.
int8_t rt_camera3d_is_ortho(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    return cam ? cam->is_ortho : 0;
}

/// @brief Position the camera and orient it to look at a target point.
/// @details Builds the view matrix using the standard look-at construction:
///          forward = normalize(eye - target), right = cross(up, forward),
///          true_up = cross(forward, right). Uses right-handed coordinates.
void rt_camera3d_look_at(void *obj, void *eye_v, void *target_v, void *up_v) {
    if (!obj || !rt_g3d_is_vec3(eye_v) || !rt_g3d_is_vec3(target_v) || !rt_g3d_is_vec3(up_v))
        return;
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;

    static const double fallback_eye[3] = {0.0, 0.0, 0.0};
    static const double fallback_target[3] = {0.0, 0.0, -1.0};
    static const double fallback_up[3] = {0.0, 1.0, 0.0};
    double eye[3] = {rt_vec3_x(eye_v), rt_vec3_y(eye_v), rt_vec3_z(eye_v)};
    double target[3] = {rt_vec3_x(target_v), rt_vec3_y(target_v), rt_vec3_z(target_v)};
    double up[3] = {rt_vec3_x(up_v), rt_vec3_y(up_v), rt_vec3_z(up_v)};

    sanitize_vec3(eye, fallback_eye);
    sanitize_vec3(target, fallback_target);
    sanitize_vec3(up, fallback_up);

    cam->eye[0] = eye[0];
    cam->eye[1] = eye[1];
    cam->eye[2] = eye[2];

    build_look_at(cam->view, eye, target, up);
    camera_sync_fps_angles_from_view(cam);
    camera_apply_shake_to_view(cam);
}

/// @brief Position the camera on a spherical orbit around a target point.
/// @details Computes eye position from spherical coordinates (yaw, pitch, distance)
///          relative to the target, then builds a look-at view matrix. Useful for
///          third-person cameras and object inspection views.
void rt_camera3d_orbit(void *obj, void *target_v, double distance, double yaw, double pitch) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || !rt_g3d_is_vec3(target_v))
        return;

    double tx = clamp_abs_or(rt_vec3_x(target_v), 0.0, CAMERA3D_WORLD_ABS_MAX);
    double ty = clamp_abs_or(rt_vec3_y(target_v), 0.0, CAMERA3D_WORLD_ABS_MAX);
    double tz = clamp_abs_or(rt_vec3_z(target_v), 0.0, CAMERA3D_WORLD_ABS_MAX);

    distance = sanitize_nonnegative(distance, 0.0);
    yaw = finite_or(yaw, 0.0);
    pitch = finite_or(pitch, 0.0);

    /* Clamp pitch to avoid gimbal lock */
    if (pitch > 89.0)
        pitch = 89.0;
    if (pitch < -89.0)
        pitch = -89.0;

    double yaw_rad = yaw * (M_PI / 180.0);
    double pitch_rad = pitch * (M_PI / 180.0);
    double cp = cos(pitch_rad), sp = sin(pitch_rad);
    double cy = cos(yaw_rad), sy = sin(yaw_rad);

    double eye[3];
    eye[0] = tx + distance * cp * sy;
    eye[1] = ty + distance * sp;
    eye[2] = tz + distance * cp * cy;

    double up[3] = {0.0, 1.0, 0.0};
    double target[3] = {tx, ty, tz};

    cam->eye[0] = clamp_abs_or(eye[0], 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[1] = clamp_abs_or(eye[1], 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[2] = clamp_abs_or(eye[2], 0.0, CAMERA3D_WORLD_ABS_MAX);
    eye[0] = cam->eye[0];
    eye[1] = cam->eye[1];
    eye[2] = cam->eye[2];

    build_look_at(cam->view, eye, target, up);
    camera_sync_fps_angles_from_view(cam);
    camera_apply_shake_to_view(cam);
}

/// @brief Get the vertical field of view in degrees.
double rt_camera3d_get_fov(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    return cam ? cam->fov : 0.0;
}

/// @brief Change the field of view and rebuild the projection matrix.
void rt_camera3d_set_fov(void *obj, double fov) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;
    if (cam->is_ortho)
        return;
    cam->fov = sanitize_fov(fov);
    rebuild_projection(cam);
}

/// @brief Return the camera's eye position as a freshly allocated Vec3.
/// @details The eye is the *unshaken* position — shake offset is applied
///          per-frame to the view matrix but never mutates `cam->eye`,
///          so callers see the logical camera location (good for HUDs,
///          attaching audio listeners, raycasts from the player).
void *rt_camera3d_get_position(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return NULL;
    return rt_vec3_new(
        finite_or(cam->eye[0], 0.0), finite_or(cam->eye[1], 0.0), finite_or(cam->eye[2], 0.0));
}

/// @brief Set the camera's eye position and rebuild the view matrix.
void rt_camera3d_set_position(void *obj, void *pos) {
    double forward[3];
    double up[3];
    double target[3];

    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || !rt_g3d_is_vec3(pos))
        return;
    forward[0] = finite_or(-cam->view[8], 0.0);
    forward[1] = finite_or(-cam->view[9], 0.0);
    forward[2] = finite_or(-cam->view[10], -1.0);
    up[0] = finite_or(cam->view[4], 0.0);
    up[1] = finite_or(cam->view[5], 1.0);
    up[2] = finite_or(cam->view[6], 0.0);
    cam->eye[0] = clamp_abs_or(rt_vec3_x(pos), 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[1] = clamp_abs_or(rt_vec3_y(pos), 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[2] = clamp_abs_or(rt_vec3_z(pos), 0.0, CAMERA3D_WORLD_ABS_MAX);
    target[0] = cam->eye[0] + forward[0];
    target[1] = cam->eye[1] + forward[1];
    target[2] = cam->eye[2] + forward[2];
    build_look_at(cam->view, cam->eye, target, up);
    camera_apply_shake_to_view(cam);
}

/// @brief Extract the world-space forward unit vector the camera is currently pointing
/// at. Read directly from the view matrix's third row (negated because the view looks
/// along -Z by convention). Caller owns the returned Vec3. Useful for "fire a ray from
/// the camera" behaviors and for gameplay that needs the camera facing independent of
/// its `look_at` target.
void *rt_camera3d_get_forward(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return NULL;
    double x = finite_or(-cam->view[8], 0.0);
    double y = finite_or(-cam->view[9], 0.0);
    double z = finite_or(-cam->view[10], -1.0);
    camera_normalize_vec3_or(&x, &y, &z, 0.0, 0.0, -1.0);
    return rt_vec3_new(x, y, z);
}

/// @brief Extract the world-space right unit vector (the camera's screen-right axis).
/// Read directly from the view matrix's first row. Used together with forward and up
/// to build camera-relative movement (strafing, mouse-look yaw, etc.). Caller owns the
/// returned Vec3.
void *rt_camera3d_get_right(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return NULL;
    double x = finite_or(cam->view[0], 1.0);
    double y = finite_or(cam->view[1], 0.0);
    double z = finite_or(cam->view[2], 0.0);
    camera_normalize_vec3_or(&x, &y, &z, 1.0, 0.0, 0.0);
    return rt_vec3_new(x, y, z);
}

/* Invert a 4x4 row-major matrix. Returns 0 on success, -1 if singular. */
/// @brief Invert a 4x4 row-major matrix using cofactor expansion.
/// Each inv[i] is the cofactor (signed minor) of the transpose, so
/// the adjugate matrix is built column-by-column. The determinant is
/// computed from the first row and its cofactors. Returns -1 if singular.
static int mat4d_invert(const double *m, double *out) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!isfinite(det) || fabs(det) < 1e-12)
        return -1;

    double inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
    return 0;
}

static int camera_get_pick_inv_vp(rt_camera3d *cam, double aspect, double *out_inv_vp) {
    double near_plane;
    double far_plane;
    double fov;
    double ortho_size;
    double projection[16];
    double vp[16];
    if (!cam || !out_inv_vp)
        return 0;
    aspect = sanitize_aspect(aspect);
    near_plane = cam->near_plane;
    far_plane = cam->far_plane;
    sanitize_clip_planes(&near_plane, &far_plane);
    fov = sanitize_fov(cam->fov);
    ortho_size = sanitize_ortho_size(cam->ortho_size);
    if (cam->pick_cache_valid && cam->pick_cache_is_ortho == cam->is_ortho &&
        cam->pick_cache_aspect == aspect && cam->pick_cache_fov == fov &&
        cam->pick_cache_near == near_plane && cam->pick_cache_far == far_plane &&
        cam->pick_cache_ortho_size == ortho_size &&
        memcmp(cam->pick_cache_view, cam->view, sizeof(cam->view)) == 0) {
        memcpy(out_inv_vp, cam->pick_cache_inv_vp, sizeof(cam->pick_cache_inv_vp));
        return 1;
    }

    if (cam->is_ortho) {
        double half_w = ortho_size * aspect;
        build_ortho(projection, -half_w, half_w, -ortho_size, ortho_size, near_plane, far_plane);
    } else {
        build_perspective(projection, fov, aspect, near_plane, far_plane);
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            vp[r * 4 + c] = projection[r * 4 + 0] * cam->view[0 * 4 + c] +
                            projection[r * 4 + 1] * cam->view[1 * 4 + c] +
                            projection[r * 4 + 2] * cam->view[2 * 4 + c] +
                            projection[r * 4 + 3] * cam->view[3 * 4 + c];
        }
    }
    if (mat4d_invert(vp, out_inv_vp) != 0) {
        cam->pick_cache_valid = 0;
        return 0;
    }
    memcpy(cam->pick_cache_view, cam->view, sizeof(cam->view));
    memcpy(cam->pick_cache_inv_vp, out_inv_vp, sizeof(cam->pick_cache_inv_vp));
    cam->pick_cache_aspect = aspect;
    cam->pick_cache_fov = fov;
    cam->pick_cache_near = near_plane;
    cam->pick_cache_far = far_plane;
    cam->pick_cache_ortho_size = ortho_size;
    cam->pick_cache_is_ortho = cam->is_ortho;
    cam->pick_cache_valid = 1;
    return 1;
}

/// @brief Unproject a screen-space pixel into a world-space ray direction.
/// @details Standard picking ray construction:
///          1. Convert pixel (sx,sy) to normalized device coords with the
///             usual Y-flip (screen Y grows down, NDC Y grows up).
///          2. Build VP = projection * view, invert it.
///          3. Multiply NDC point at near plane (z=-1, w=1) by inv(VP)
///             to get a homogeneous world-space point; perspective-divide
///             to recover Cartesian coords.
///          4. Direction = normalize(world − rendered eye), where the
///             rendered eye includes the current shake offset so picks
///             align with what the player actually sees.
///          For ortho cameras, all rays are parallel (direction = view
///          forward), so the projection-inversion path is skipped and
///          the normalized forward axis is returned directly.
///          Returns (0,0,-1) on degenerate inputs (zero size, singular VP).
void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || sw <= 0 || sh <= 0)
        return rt_vec3_new(0.0, 0.0, -1.0);

    if (cam->is_ortho) {
        double fx = -cam->view[8];
        double fy = -cam->view[9];
        double fz = -cam->view[10];
        double len = sqrt(fx * fx + fy * fy + fz * fz);
        if (isfinite(len) && len > 1e-12)
            return rt_vec3_new(fx / len, fy / len, fz / len);
        return rt_vec3_new(0.0, 0.0, -1.0);
    }

    /* Screen → NDC */
    double ndc_x = (2.0 * (double)sx / (double)sw) - 1.0;
    double ndc_y = 1.0 - (2.0 * (double)sy / (double)sh); /* Y-flip */

    double aspect = sanitize_aspect((double)sw / (double)sh);
    double inv_vp[16];
    if (!camera_get_pick_inv_vp(cam, aspect, inv_vp))
        return rt_vec3_new(0.0, 0.0, -1.0);

    /* Unproject NDC point at near plane (z=-1) to world space */
    double p[4] = {ndc_x, ndc_y, -1.0, 1.0};
    double world[4];
    world[0] = inv_vp[0] * p[0] + inv_vp[1] * p[1] + inv_vp[2] * p[2] + inv_vp[3] * p[3];
    world[1] = inv_vp[4] * p[0] + inv_vp[5] * p[1] + inv_vp[6] * p[2] + inv_vp[7] * p[3];
    world[2] = inv_vp[8] * p[0] + inv_vp[9] * p[1] + inv_vp[10] * p[2] + inv_vp[11] * p[3];
    world[3] = inv_vp[12] * p[0] + inv_vp[13] * p[1] + inv_vp[14] * p[2] + inv_vp[15] * p[3];

    if (isfinite(world[3]) && fabs(world[3]) > 1e-12) {
        world[0] /= world[3];
        world[1] /= world[3];
        world[2] /= world[3];
    }

    /* Ray direction = normalize(worldPoint - rendered eye) */
    double origin_x = cam->eye[0] + cam->shake_offset[0];
    double origin_y = cam->eye[1] + cam->shake_offset[1];
    double origin_z = cam->eye[2] + cam->shake_offset[2];
    double dx = world[0] - origin_x;
    double dy = world[1] - origin_y;
    double dz = world[2] - origin_z;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (isfinite(len) && len > 1e-12) {
        dx /= len;
        dy /= len;
        dz /= len;
    } else {
        dx = 0.0;
        dy = 0.0;
        dz = -1.0;
    }

    return rt_vec3_new(dx, dy, dz);
}

/// @brief Return the world-space origin for a screen-space picking ray.
/// @details Perspective cameras originate at the rendered eye position. Orthographic
///          cameras originate at the unprojected near-plane point for the requested
///          pixel, because each screen pixel has a distinct parallel ray.
void *rt_camera3d_screen_to_ray_origin(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || sw <= 0 || sh <= 0)
        return rt_vec3_new(0.0, 0.0, 0.0);

    double eye_x = cam->eye[0] + cam->shake_offset[0];
    double eye_y = cam->eye[1] + cam->shake_offset[1];
    double eye_z = cam->eye[2] + cam->shake_offset[2];
    if (!cam->is_ortho)
        return rt_vec3_new(eye_x, eye_y, eye_z);

    double ndc_x = (2.0 * (double)sx / (double)sw) - 1.0;
    double ndc_y = 1.0 - (2.0 * (double)sy / (double)sh);
    double aspect = sanitize_aspect((double)sw / (double)sh);
    double inv_vp[16];
    if (!camera_get_pick_inv_vp(cam, aspect, inv_vp))
        return rt_vec3_new(eye_x, eye_y, eye_z);

    double p[4] = {ndc_x, ndc_y, -1.0, 1.0};
    double world[4];
    world[0] = inv_vp[0] * p[0] + inv_vp[1] * p[1] + inv_vp[2] * p[2] + inv_vp[3] * p[3];
    world[1] = inv_vp[4] * p[0] + inv_vp[5] * p[1] + inv_vp[6] * p[2] + inv_vp[7] * p[3];
    world[2] = inv_vp[8] * p[0] + inv_vp[9] * p[1] + inv_vp[10] * p[2] + inv_vp[11] * p[3];
    world[3] = inv_vp[12] * p[0] + inv_vp[13] * p[1] + inv_vp[14] * p[2] + inv_vp[15] * p[3];

    if (isfinite(world[3]) && fabs(world[3]) > 1e-12) {
        world[0] /= world[3];
        world[1] /= world[3];
        world[2] /= world[3];
    }
    if (!isfinite(world[0]) || !isfinite(world[1]) || !isfinite(world[2]))
        return rt_vec3_new(eye_x, eye_y, eye_z);
    return rt_vec3_new(world[0], world[1], world[2]);
}

/*==========================================================================
 * FPS camera controller
 *=========================================================================*/

/// @brief Initialize FPS-style camera state (yaw/pitch from current orientation).
void rt_camera3d_fps_init(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;
    camera_sync_fps_angles_from_view(cam);
}

/// @brief Update FPS camera from mouse look (dx/dy) and WASD movement (fwd/right/up).
/// @details Integrates mouse deltas into yaw/pitch (with pitch clamped to ±89°),
///          then computes the forward/right/up vectors and applies WASD movement.
///          The camera shake offset is added last if active.
void rt_camera3d_fps_update(void *obj,
                            double yaw_delta,
                            double pitch_delta,
                            double move_fwd,
                            double move_right,
                            double move_up,
                            double speed,
                            double dt) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;

    yaw_delta = finite_or(yaw_delta, 0.0);
    pitch_delta = finite_or(pitch_delta, 0.0);
    move_fwd = clamp_abs_or(move_fwd, 0.0, CAMERA3D_WORLD_ABS_MAX);
    move_right = clamp_abs_or(move_right, 0.0, CAMERA3D_WORLD_ABS_MAX);
    move_up = clamp_abs_or(move_up, 0.0, CAMERA3D_WORLD_ABS_MAX);
    speed = sanitize_nonnegative(speed, 0.0);
    dt = sanitize_nonnegative(dt, 0.0);
    cam->fps_yaw = finite_or(cam->fps_yaw, 0.0);
    cam->fps_pitch = finite_or(cam->fps_pitch, 0.0);

    /* Accumulate yaw/pitch from mouse deltas */
    cam->fps_yaw = fmod(cam->fps_yaw + yaw_delta, 360.0);
    if (!isfinite(cam->fps_yaw))
        cam->fps_yaw = 0.0;
    cam->fps_pitch += pitch_delta;
    if (cam->fps_pitch > 89.0)
        cam->fps_pitch = 89.0;
    if (cam->fps_pitch < -89.0)
        cam->fps_pitch = -89.0;

    /* Compute forward and right vectors from yaw/pitch */
    double yaw_rad = cam->fps_yaw * (M_PI / 180.0);
    double pitch_rad = cam->fps_pitch * (M_PI / 180.0);
    double cp = cos(pitch_rad);

    double fwd_x = sin(yaw_rad) * cp;
    double fwd_y = sin(pitch_rad);
    double fwd_z = -cos(yaw_rad) * cp;

    /* Right = cross(forward, world_up), normalized on XZ plane */
    double right_x = cos(yaw_rad);
    double right_z = sin(yaw_rad);

    /* Apply movement */
    double move_scale = speed * dt;
    if (!isfinite(move_scale) || move_scale > CAMERA3D_WORLD_ABS_MAX)
        move_scale = CAMERA3D_WORLD_ABS_MAX;
    cam->eye[0] = clamp_abs_or(cam->eye[0] + fwd_x * move_fwd * move_scale +
                                   right_x * move_right * move_scale,
                               0.0,
                               CAMERA3D_WORLD_ABS_MAX);
    cam->eye[1] = clamp_abs_or(cam->eye[1] + fwd_y * move_fwd * move_scale + move_up * move_scale,
                               0.0,
                               CAMERA3D_WORLD_ABS_MAX);
    cam->eye[2] = clamp_abs_or(cam->eye[2] + fwd_z * move_fwd * move_scale +
                                   right_z * move_right * move_scale,
                               0.0,
                               CAMERA3D_WORLD_ABS_MAX);

    /* Rebuild view matrix via LookAt */
    double target[3] = {cam->eye[0] + fwd_x, cam->eye[1] + fwd_y, cam->eye[2] + fwd_z};
    double up[3] = {0.0, 1.0, 0.0};
    build_look_at(cam->view, cam->eye, target, up);
    camera_apply_shake_to_view(cam);
}

/// @brief Return the FPS-controller yaw in degrees (heading around world Y).
double rt_camera3d_get_yaw(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    return cam ? cam->fps_yaw : 0.0;
}

/// @brief Return the FPS-controller pitch in degrees (look up/down).
double rt_camera3d_get_pitch(void *obj) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    return cam ? cam->fps_pitch : 0.0;
}

/// @brief Set yaw absolutely and rebuild the view (and shake) to match.
/// @details Unlike `fps_update`'s yaw delta, this lets gameplay code
///          snap heading directly (e.g. cutscene camera, teleport,
///          respawn). Caller is responsible for normalizing the angle
///          if they care about the stored value range.
void rt_camera3d_set_yaw(void *obj, double yaw) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;
    cam->fps_yaw = finite_or(yaw, 0.0);
    camera_rebuild_fps_view(cam);
    camera_apply_shake_to_view(cam);
}

/// @brief Set pitch absolutely (clamped to ±89°) and rebuild the view.
/// @details The clamp matches `fps_update` so manual pitch overrides
///          can never produce a degenerate look-at direction.
void rt_camera3d_set_pitch(void *obj, double pitch) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;
    cam->fps_pitch = finite_or(pitch, 0.0);
    if (cam->fps_pitch > 89.0)
        cam->fps_pitch = 89.0;
    if (cam->fps_pitch < -89.0)
        cam->fps_pitch = -89.0;
    camera_rebuild_fps_view(cam);
    camera_apply_shake_to_view(cam);
}

/*==========================================================================
 * Camera shake
 *=========================================================================*/

/// @brief Advance the shake state one frame and recompute the eye offset.
/// @details Bails out early once `shake_duration` reaches zero, also
///          clearing the offset so the camera snaps back to its true
///          eye position. While shake is active:
///          - Duration counts down by dt (linear time decay).
///          - Intensity follows an exponential decay
///            (intensity *= exp(-decay * dt)) so impulses ramp out
///            smoothly — `decay = 5` halves intensity in ~138 ms.
///          - Two consecutive xorshift32 draws produce a deterministic
///            uniform [-1, 1] pair (r1, r2). The Z component uses
///            r1*r2*0.3 so it's correlated with X/Y but smaller, biasing
///            shake toward the screen plane (preferred for first-person).
///          The seed lives on the camera, so two cameras shake
///          independently and a single camera replays identically across
///          runs given the same dt sequence.
static void apply_shake(rt_camera3d *cam, double dt) {
    if (!cam)
        return;
    dt = sanitize_nonnegative(dt, 0.0);
    cam->shake_duration = sanitize_nonnegative(cam->shake_duration, 0.0);
    cam->shake_intensity = sanitize_nonnegative(cam->shake_intensity, 0.0);
    cam->shake_decay = finite_or(cam->shake_decay, 5.0);
    if (cam->shake_decay <= 0.0)
        cam->shake_decay = 5.0;
    if (cam->shake_duration <= 0.0) {
        cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
        return;
    }
    cam->shake_duration -= dt;
    if (cam->shake_duration <= 0.0) {
        cam->shake_duration = 0.0;
        cam->shake_intensity = 0.0;
        cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
        return;
    }
    cam->shake_intensity *= exp(-cam->shake_decay * dt);

    /* Xorshift PRNG for deterministic random offsets */
    cam->shake_seed ^= cam->shake_seed << 13;
    cam->shake_seed ^= cam->shake_seed >> 17;
    cam->shake_seed ^= cam->shake_seed << 5;
    double r1 = ((double)(cam->shake_seed & 0xFFFF) / 65535.0) * 2.0 - 1.0;
    cam->shake_seed ^= cam->shake_seed << 13;
    cam->shake_seed ^= cam->shake_seed >> 17;
    cam->shake_seed ^= cam->shake_seed << 5;
    double r2 = ((double)(cam->shake_seed & 0xFFFF) / 65535.0) * 2.0 - 1.0;

    cam->shake_offset[0] = r1 * cam->shake_intensity;
    cam->shake_offset[1] = r2 * cam->shake_intensity;
    cam->shake_offset[2] = (r1 * r2) * cam->shake_intensity * 0.3;
}

/// @brief Per-frame entry point that advances shake decay and rebuilds the view.
/// @details Should be called once per frame from the renderer (or game
///          loop) with the frame delta. Splits the work between
///          `apply_shake` (advance state, compute offset) and
///          `camera_apply_shake_to_view` (rebuild view matrix using the
///          new offset) so other call sites that mutate the view can
///          re-apply the shake without re-rolling the PRNG.
void rt_camera3d_update_shake_for_frame(void *obj, double dt) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);

    if (!cam)
        return;
    apply_shake(cam, dt);
    camera_apply_shake_to_view(cam);
}

/// @brief Trigger a camera shake effect (exponentially decaying random offset).
/// @details The shake applies random XY offsets that decay over the given duration.
///          Used for explosions, impacts, and other feedback effects.
void rt_camera3d_shake(void *obj, double intensity, double duration, double decay) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam)
        return;
    cam->shake_intensity = sanitize_nonnegative(intensity, 0.0);
    cam->shake_duration = sanitize_nonnegative(duration, 0.0);
    cam->shake_decay = isfinite(decay) && decay > 0.0 ? decay : 5.0;
    apply_shake(cam, 0.0);
    camera_apply_shake_to_view(cam);
}

/*==========================================================================
 * Smooth follow (third-person camera)
 *=========================================================================*/

/// @brief Smoothly interpolate the camera toward a target position over time.
/// @details Uses exponential decay (lerp with speed * dt) for natural smoothing.
///          The camera maintains a configurable offset from the target.
void rt_camera3d_smooth_follow(
    void *obj, void *target_pos, double distance, double height, double speed, double dt) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || !rt_g3d_is_vec3(target_pos))
        return;

    height = clamp_abs_or(height, 0.0, CAMERA3D_WORLD_ABS_MAX);
    double tx = clamp_abs_or(rt_vec3_x(target_pos), 0.0, CAMERA3D_WORLD_ABS_MAX);
    double ty = clamp_abs_or(rt_vec3_y(target_pos), 0.0, CAMERA3D_WORLD_ABS_MAX) + height;
    double tz = clamp_abs_or(rt_vec3_z(target_pos), 0.0, CAMERA3D_WORLD_ABS_MAX);

    distance = sanitize_nonnegative(distance, 0.0);
    speed = sanitize_nonnegative(speed, 0.0);
    dt = sanitize_nonnegative(dt, 0.0);
    cam->fps_yaw = finite_or(cam->fps_yaw, 0.0);

    /* Desired position: behind target using current yaw */
    double yaw_rad = cam->fps_yaw * (M_PI / 180.0);
    double desired[3] = {tx - sin(yaw_rad) * distance, ty, tz + cos(yaw_rad) * distance};

    /* Framerate-independent exponential damping */
    double t = 1.0 - exp(-speed * dt);
    double base_eye[3] = {
        finite_or(cam->eye[0], 0.0), finite_or(cam->eye[1], 0.0), finite_or(cam->eye[2], 0.0)};
    cam->eye[0] =
        clamp_abs_or(base_eye[0] + (desired[0] - base_eye[0]) * t, 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[1] =
        clamp_abs_or(base_eye[1] + (desired[1] - base_eye[1]) * t, 0.0, CAMERA3D_WORLD_ABS_MAX);
    cam->eye[2] =
        clamp_abs_or(base_eye[2] + (desired[2] - base_eye[2]) * t, 0.0, CAMERA3D_WORLD_ABS_MAX);

    double look_at[3] = {tx, ty - height * 0.3, tz};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look_at, up);
    camera_sync_fps_angles_from_view(cam);
    camera_apply_shake_to_view(cam);
}

/*==========================================================================
 * Smooth look-at (gradual rotation toward target)
 *=========================================================================*/

/// @brief Smoothly rotate the camera toward a look-at target over time.
void rt_camera3d_smooth_look_at(void *obj, void *target, double speed, double dt) {
    rt_camera3d *cam = rt_camera3d_checked_or_stack(obj);
    if (!cam || !rt_g3d_is_vec3(target))
        return;

    /* Current forward from view matrix */
    double cur_fwd[3] = {finite_or(-cam->view[8], 0.0),
                         finite_or(-cam->view[9], 0.0),
                         finite_or(-cam->view[10], -1.0)};

    /* Desired forward */
    double dx = finite_or(rt_vec3_x(target), 0.0) - finite_or(cam->eye[0], 0.0);
    double dy = finite_or(rt_vec3_y(target), 0.0) - finite_or(cam->eye[1], 0.0);
    double dz = finite_or(rt_vec3_z(target), 0.0) - finite_or(cam->eye[2], 0.0);
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    speed = sanitize_nonnegative(speed, 0.0);
    dt = sanitize_nonnegative(dt, 0.0);
    if (isfinite(len) && len > 1e-8) {
        dx /= len;
        dy /= len;
        dz /= len;
    } else {
        dx = cur_fwd[0];
        dy = cur_fwd[1];
        dz = cur_fwd[2];
    }

    /* Exponential lerp toward desired */
    double t = 1.0 - exp(-speed * dt);
    double new_fwd[3] = {cur_fwd[0] + (dx - cur_fwd[0]) * t,
                         cur_fwd[1] + (dy - cur_fwd[1]) * t,
                         cur_fwd[2] + (dz - cur_fwd[2]) * t};
    len = sqrt(new_fwd[0] * new_fwd[0] + new_fwd[1] * new_fwd[1] + new_fwd[2] * new_fwd[2]);
    if (isfinite(len) && len > 1e-8) {
        new_fwd[0] /= len;
        new_fwd[1] /= len;
        new_fwd[2] /= len;
    }

    double look[3] = {cam->eye[0] + new_fwd[0], cam->eye[1] + new_fwd[1], cam->eye[2] + new_fwd[2]};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look, up);
    camera_sync_fps_angles_from_view(cam);
    camera_apply_shake_to_view(cam);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
