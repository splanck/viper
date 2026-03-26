//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_camera3d.c
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

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdint.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_trap(const char *msg);

/* Access existing Vec3 and Mat4 public API */
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);

/* Build perspective projection matrix (matches rt_mat4_perspective) */
static void build_perspective(double *m, double fov_deg, double aspect, double near, double far)
{
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

/* Build view matrix (matches rt_mat4_look_at) */
static void build_look_at(double *m, const double *eye, const double *target, const double *up)
{
    /* Forward = normalize(eye - target) — right-handed.
     * If eye == target (flen ≈ 0), return identity matrix to avoid
     * producing a degenerate view matrix with NaN/zero rows. */
    double fx = eye[0] - target[0];
    double fy = eye[1] - target[1];
    double fz = eye[2] - target[2];
    double flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (flen < 1e-12)
    {
        /* eye == target: produce identity view matrix */
        memset(m, 0, 16 * sizeof(double));
        m[0] = m[5] = m[10] = m[15] = 1.0;
        return;
    }
    {
        fx /= flen;
        fy /= flen;
        fz /= flen;
    }

    /* Right = normalize(cross(up, forward)) */
    double rx = up[1] * fz - up[2] * fy;
    double ry = up[2] * fx - up[0] * fz;
    double rz = up[0] * fy - up[1] * fx;
    double rlen = sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen > 1e-12)
    {
        rx /= rlen;
        ry /= rlen;
        rz /= rlen;
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

void *rt_camera3d_new(double fov, double aspect, double near_val, double far_val)
{
    rt_camera3d *cam = (rt_camera3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_camera3d));
    if (!cam)
    {
        rt_trap("Camera3D.New: memory allocation failed");
        return NULL;
    }
    cam->vptr = NULL;
    cam->fov = fov;
    cam->aspect = aspect;
    cam->near_plane = near_val;
    cam->far_plane = far_val;

    build_perspective(cam->projection, fov, aspect, near_val, far_val);

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

    return cam;
}

void rt_camera3d_look_at(void *obj, void *eye_v, void *target_v, void *up_v)
{
    if (!obj || !eye_v || !target_v || !up_v)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    double eye[3] = {rt_vec3_x(eye_v), rt_vec3_y(eye_v), rt_vec3_z(eye_v)};
    double target[3] = {rt_vec3_x(target_v), rt_vec3_y(target_v), rt_vec3_z(target_v)};
    double up[3] = {rt_vec3_x(up_v), rt_vec3_y(up_v), rt_vec3_z(up_v)};

    cam->eye[0] = eye[0];
    cam->eye[1] = eye[1];
    cam->eye[2] = eye[2];

    build_look_at(cam->view, eye, target, up);
}

void rt_camera3d_orbit(void *obj, void *target_v, double distance, double yaw, double pitch)
{
    if (!obj || !target_v)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    double tx = rt_vec3_x(target_v);
    double ty = rt_vec3_y(target_v);
    double tz = rt_vec3_z(target_v);

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

    cam->eye[0] = eye[0];
    cam->eye[1] = eye[1];
    cam->eye[2] = eye[2];

    build_look_at(cam->view, eye, target, up);
}

double rt_camera3d_get_fov(void *obj)
{
    if (!obj)
        return 0.0;
    return ((rt_camera3d *)obj)->fov;
}

void rt_camera3d_set_fov(void *obj, double fov)
{
    if (!obj)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    cam->fov = fov;
    build_perspective(cam->projection, fov, cam->aspect, cam->near_plane, cam->far_plane);
}

void *rt_camera3d_get_position(void *obj)
{
    if (!obj)
        return NULL;
    rt_camera3d *cam = (rt_camera3d *)obj;
    return rt_vec3_new(cam->eye[0], cam->eye[1], cam->eye[2]);
}

void rt_camera3d_set_position(void *obj, void *pos)
{
    if (!obj || !pos)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    cam->eye[0] = rt_vec3_x(pos);
    cam->eye[1] = rt_vec3_y(pos);
    cam->eye[2] = rt_vec3_z(pos);
}

void *rt_camera3d_get_forward(void *obj)
{
    if (!obj)
        return NULL;
    rt_camera3d *cam = (rt_camera3d *)obj;
    /* Forward = -row2 of view matrix (negated because view looks along -Z) */
    return rt_vec3_new(-cam->view[8], -cam->view[9], -cam->view[10]);
}

void *rt_camera3d_get_right(void *obj)
{
    if (!obj)
        return NULL;
    rt_camera3d *cam = (rt_camera3d *)obj;
    /* Right = row0 of view matrix */
    return rt_vec3_new(cam->view[0], cam->view[1], cam->view[2]);
}

/* Invert a 4x4 row-major matrix. Returns 0 on success, -1 if singular. */
/// @brief Invert a 4x4 row-major matrix using cofactor expansion.
/// Each inv[i] is the cofactor (signed minor) of the transpose, so
/// the adjugate matrix is built column-by-column. The determinant is
/// computed from the first row and its cofactors. Returns -1 if singular.
static int mat4d_invert(const double *m, double *out)
{
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
    if (fabs(det) < 1e-12)
        return -1;

    double inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
    return 0;
}

void *rt_camera3d_screen_to_ray(void *obj, int64_t sx, int64_t sy, int64_t sw, int64_t sh)
{
    if (!obj || sw <= 0 || sh <= 0)
        return rt_vec3_new(0.0, 0.0, -1.0);

    rt_camera3d *cam = (rt_camera3d *)obj;

    /* Screen → NDC */
    double ndc_x = (2.0 * (double)sx / (double)sw) - 1.0;
    double ndc_y = 1.0 - (2.0 * (double)sy / (double)sh); /* Y-flip */

    /* Build VP = projection * view, then invert */
    double vp[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            vp[r * 4 + c] = cam->projection[r * 4 + 0] * cam->view[0 * 4 + c] +
                            cam->projection[r * 4 + 1] * cam->view[1 * 4 + c] +
                            cam->projection[r * 4 + 2] * cam->view[2 * 4 + c] +
                            cam->projection[r * 4 + 3] * cam->view[3 * 4 + c];

    double inv_vp[16];
    if (mat4d_invert(vp, inv_vp) != 0)
        return rt_vec3_new(0.0, 0.0, -1.0);

    /* Unproject NDC point at near plane (z=-1) to world space */
    double p[4] = {ndc_x, ndc_y, -1.0, 1.0};
    double world[4];
    world[0] = inv_vp[0] * p[0] + inv_vp[1] * p[1] + inv_vp[2] * p[2] + inv_vp[3] * p[3];
    world[1] = inv_vp[4] * p[0] + inv_vp[5] * p[1] + inv_vp[6] * p[2] + inv_vp[7] * p[3];
    world[2] = inv_vp[8] * p[0] + inv_vp[9] * p[1] + inv_vp[10] * p[2] + inv_vp[11] * p[3];
    world[3] = inv_vp[12] * p[0] + inv_vp[13] * p[1] + inv_vp[14] * p[2] + inv_vp[15] * p[3];

    if (fabs(world[3]) > 1e-12)
    {
        world[0] /= world[3];
        world[1] /= world[3];
        world[2] /= world[3];
    }

    /* Ray direction = normalize(worldPoint - eye) */
    double dx = world[0] - cam->eye[0];
    double dy = world[1] - cam->eye[1];
    double dz = world[2] - cam->eye[2];
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-12)
    {
        dx /= len;
        dy /= len;
        dz /= len;
    }

    return rt_vec3_new(dx, dy, dz);
}

/*==========================================================================
 * FPS camera controller
 *=========================================================================*/

void rt_camera3d_fps_init(void *obj)
{
    if (!obj)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    /* Extract yaw/pitch from current view matrix forward direction.
     * Forward = -row2 of view matrix (negated because view Z points away). */
    double fx = -cam->view[8];
    double fy = -cam->view[9];
    double fz = -cam->view[10];
    cam->fps_yaw = atan2(fx, -fz) * (180.0 / M_PI);
    cam->fps_pitch = asin(fmax(-1.0, fmin(1.0, fy))) * (180.0 / M_PI);
    if (cam->fps_pitch > 89.0)
        cam->fps_pitch = 89.0;
    if (cam->fps_pitch < -89.0)
        cam->fps_pitch = -89.0;
}

void rt_camera3d_fps_update(void *obj,
                            double yaw_delta,
                            double pitch_delta,
                            double move_fwd,
                            double move_right,
                            double move_up,
                            double speed,
                            double dt)
{
    if (!obj)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    /* Accumulate yaw/pitch from mouse deltas */
    cam->fps_yaw += yaw_delta;
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
    cam->eye[0] += fwd_x * move_fwd * move_scale + right_x * move_right * move_scale;
    cam->eye[1] += fwd_y * move_fwd * move_scale + move_up * move_scale;
    cam->eye[2] += fwd_z * move_fwd * move_scale + right_z * move_right * move_scale;

    /* Rebuild view matrix via LookAt */
    double target[3] = {cam->eye[0] + fwd_x, cam->eye[1] + fwd_y, cam->eye[2] + fwd_z};
    double up[3] = {0.0, 1.0, 0.0};
    build_look_at(cam->view, cam->eye, target, up);
}

double rt_camera3d_get_yaw(void *obj)
{
    return obj ? ((rt_camera3d *)obj)->fps_yaw : 0.0;
}

double rt_camera3d_get_pitch(void *obj)
{
    return obj ? ((rt_camera3d *)obj)->fps_pitch : 0.0;
}

void rt_camera3d_set_yaw(void *obj, double yaw)
{
    if (obj)
        ((rt_camera3d *)obj)->fps_yaw = yaw;
}

void rt_camera3d_set_pitch(void *obj, double pitch)
{
    if (!obj)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    cam->fps_pitch = pitch;
    if (cam->fps_pitch > 89.0)
        cam->fps_pitch = 89.0;
    if (cam->fps_pitch < -89.0)
        cam->fps_pitch = -89.0;
}

/*==========================================================================
 * Camera shake
 *=========================================================================*/

static void apply_shake(rt_camera3d *cam, double dt)
{
    if (cam->shake_duration <= 0.0)
    {
        cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
        return;
    }
    cam->shake_duration -= dt;
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

void rt_camera3d_shake(void *obj, double intensity, double duration, double decay)
{
    if (!obj)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    cam->shake_intensity = intensity;
    cam->shake_duration = duration;
    cam->shake_decay = decay > 0.0 ? decay : 5.0;
}

/*==========================================================================
 * Smooth follow (third-person camera)
 *=========================================================================*/

void rt_camera3d_smooth_follow(
    void *obj, void *target_pos, double distance, double height, double speed, double dt)
{
    if (!obj || !target_pos)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    double tx = rt_vec3_x(target_pos);
    double ty = rt_vec3_y(target_pos) + height;
    double tz = rt_vec3_z(target_pos);

    /* Desired position: behind target using current yaw */
    double yaw_rad = cam->fps_yaw * (M_PI / 180.0);
    double desired[3] = {tx - sin(yaw_rad) * distance, ty, tz + cos(yaw_rad) * distance};

    /* Framerate-independent exponential damping */
    double t = 1.0 - exp(-speed * dt);
    cam->eye[0] += (desired[0] - cam->eye[0]) * t;
    cam->eye[1] += (desired[1] - cam->eye[1]) * t;
    cam->eye[2] += (desired[2] - cam->eye[2]) * t;

    apply_shake(cam, dt);
    cam->eye[0] += cam->shake_offset[0];
    cam->eye[1] += cam->shake_offset[1];
    cam->eye[2] += cam->shake_offset[2];

    double look_at[3] = {tx, ty - height * 0.3, tz};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look_at, up);
}

/*==========================================================================
 * Smooth look-at (gradual rotation toward target)
 *=========================================================================*/

void rt_camera3d_smooth_look_at(void *obj, void *target, double speed, double dt)
{
    if (!obj || !target)
        return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    /* Current forward from view matrix */
    double cur_fwd[3] = {-cam->view[8], -cam->view[9], -cam->view[10]};

    /* Desired forward */
    double dx = rt_vec3_x(target) - cam->eye[0];
    double dy = rt_vec3_y(target) - cam->eye[1];
    double dz = rt_vec3_z(target) - cam->eye[2];
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-8)
    {
        dx /= len;
        dy /= len;
        dz /= len;
    }

    /* Exponential lerp toward desired */
    double t = 1.0 - exp(-speed * dt);
    double new_fwd[3] = {cur_fwd[0] + (dx - cur_fwd[0]) * t,
                         cur_fwd[1] + (dy - cur_fwd[1]) * t,
                         cur_fwd[2] + (dz - cur_fwd[2]) * t};
    len = sqrt(new_fwd[0] * new_fwd[0] + new_fwd[1] * new_fwd[1] + new_fwd[2] * new_fwd[2]);
    if (len > 1e-8)
    {
        new_fwd[0] /= len;
        new_fwd[1] /= len;
        new_fwd[2] /= len;
    }

    double look[3] = {cam->eye[0] + new_fwd[0], cam->eye[1] + new_fwd[1], cam->eye[2] + new_fwd[2]};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look, up);
}

#endif /* VIPER_ENABLE_GRAPHICS */
