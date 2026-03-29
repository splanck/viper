//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_skeleton3d.c
// Purpose: Skeleton3D (bone hierarchy + bind pose), Animation3D (keyframe
//   clips), and AnimPlayer3D (playback, sampling, crossfade, palette output).
//
// Key invariants:
//   - Bones in topological order (parent_index < bone_index).
//   - Palette computation: local → global (multiply up hierarchy) → * inverse_bind.
//   - Keyframe sampling: binary search for bracket, SLERP rotation, lerp pos/scale.
//   - Crossfade: blend per-bone local transforms between two animations.
//
// Links: rt_skeleton3d.h, vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_skeleton3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_skinning.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int vgfx3d_backend_prefers_gpu_skinning(const char *backend_name, int32_t bone_count) {
    if (!backend_name || bone_count <= 0)
        return 0;
    if (strcmp(backend_name, "metal") == 0)
        return 1;
    if (strcmp(backend_name, "opengl") == 0)
        return bone_count <= 128;
    return 0;
}

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);
extern double rt_mat4_get(void *m, int64_t row, int64_t col);
extern void *rt_mat4_new(double m0,
                         double m1,
                         double m2,
                         double m3,
                         double m4,
                         double m5,
                         double m6,
                         double m7,
                         double m8,
                         double m9,
                         double m10,
                         double m11,
                         double m12,
                         double m13,
                         double m14,
                         double m15);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);

/* Canvas3D draw function (for skinned draw) */
extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);

#define VGFX3D_MAX_BONES 128

/*==========================================================================
 * Skeleton3D
 *=========================================================================*/

typedef struct {
    char name[64];
    int32_t parent_index;
    float bind_pose_local[16]; /* row-major local bind pose */
    float inverse_bind[16];    /* row-major inverse of global bind pose */
} vgfx3d_bone_t;

typedef struct {
    void *vptr;
    vgfx3d_bone_t *bones;
    int32_t bone_count;
} rt_skeleton3d;

/*==========================================================================
 * Animation3D
 *=========================================================================*/

typedef struct {
    float time;
    float position[3];
    float rotation[4]; /* quaternion (x, y, z, w) */
    float scale_xyz[3];
} vgfx3d_keyframe_t;

typedef struct {
    int32_t bone_index;
    vgfx3d_keyframe_t *keyframes;
    int32_t keyframe_count;
    int32_t keyframe_capacity;
} vgfx3d_anim_channel_t;

typedef struct {
    void *vptr;
    char name[64];
    vgfx3d_anim_channel_t *channels;
    int32_t channel_count;
    int32_t channel_capacity;
    float duration;
    int8_t looping;
} rt_animation3d;

/*==========================================================================
 * AnimPlayer3D
 *=========================================================================*/

typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;
    rt_animation3d *current;
    rt_animation3d *crossfade_from;
    float current_time;
    float crossfade_time;
    float crossfade_duration;
    float crossfade_from_time;
    float speed;
    int8_t playing;
    float *bone_palette;     /* bone_count * 16 floats */
    float *local_transforms; /* bone_count * 16 floats (workspace) */
    float *globals_buf;      /* bone_count * 16 floats (reused across frames) */
} rt_anim_player3d;

/*==========================================================================
 * Matrix math helpers (float, row-major)
 *=========================================================================*/

static void mat4f_mul_local(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static void mat4f_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/// @brief Invert a 4x4 float matrix. Returns 0 on success, -1 if singular.
static int mat4f_invert(const float *m, float *out) {
    float inv[16];
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

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-12f)
        return -1;
    float inv_det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
    return 0;
}

/// @brief Build TRS matrix from float arrays (row-major).
static void build_trs_float(const float *pos, const float *quat, const float *scl, float *out) {
    float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    out[0] = (1.0f - (yy + zz)) * scl[0];
    out[1] = (xy - wz) * scl[1];
    out[2] = (xz + wy) * scl[2];
    out[3] = pos[0];
    out[4] = (xy + wz) * scl[0];
    out[5] = (1.0f - (xx + zz)) * scl[1];
    out[6] = (yz - wx) * scl[2];
    out[7] = pos[1];
    out[8] = (xz - wy) * scl[0];
    out[9] = (yz + wx) * scl[1];
    out[10] = (1.0f - (xx + yy)) * scl[2];
    out[11] = pos[2];
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

/// @brief SLERP between two quaternions (float arrays, x,y,z,w).
static void quat_slerp_float(const float *a, const float *b, float t, float *out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float nb[4] = {b[0], b[1], b[2], b[3]};
    if (dot < 0.0f) {
        dot = -dot;
        nb[0] = -nb[0];
        nb[1] = -nb[1];
        nb[2] = -nb[2];
        nb[3] = -nb[3];
    }
    if (dot > 0.9995f) {
        /* Nearly identical: linear interpolation + normalize */
        for (int i = 0; i < 4; i++)
            out[i] = a[i] + t * (nb[i] - a[i]);
    } else {
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        for (int i = 0; i < 4; i++)
            out[i] = wa * a[i] + wb * nb[i];
    }
    /* Normalize */
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (len > 1e-8f)
        for (int i = 0; i < 4; i++)
            out[i] /= len;
}

/*==========================================================================
 * Skeleton3D implementation
 *=========================================================================*/

static void rt_skeleton3d_finalize(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    free(s->bones);
    s->bones = NULL;
}

void *rt_skeleton3d_new(void) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_skeleton3d));
    if (!s) {
        rt_trap("Skeleton3D.New: memory allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->bones = NULL;
    s->bone_count = 0;
    rt_obj_set_finalizer(s, rt_skeleton3d_finalize);
    return s;
}

int64_t rt_skeleton3d_add_bone(void *obj, rt_string name, int64_t parent_index, void *bind_mat4) {
    if (!obj)
        return -1;
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    if (s->bone_count >= VGFX3D_MAX_BONES) {
        rt_trap("Skeleton3D.AddBone: max 128 bones exceeded");
        return -1;
    }
    if (parent_index >= s->bone_count) {
        rt_trap("Skeleton3D.AddBone: parent_index must be less than bone count");
        return -1;
    }

    int32_t new_count = s->bone_count + 1;
    vgfx3d_bone_t *nb =
        (vgfx3d_bone_t *)realloc(s->bones, (size_t)new_count * sizeof(vgfx3d_bone_t));
    if (!nb)
        return -1;
    s->bones = nb;

    vgfx3d_bone_t *bone = &s->bones[s->bone_count];
    memset(bone, 0, sizeof(vgfx3d_bone_t));

    /* Copy name */
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(bone->name, cstr, len);
            bone->name[len] = '\0';
        }
    }

    bone->parent_index = (int32_t)parent_index;

    /* Copy bind pose from Mat4 (double → float) */
    if (bind_mat4) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                bone->bind_pose_local[r * 4 + c] = (float)rt_mat4_get(bind_mat4, r, c);
    } else {
        mat4f_identity(bone->bind_pose_local);
    }
    mat4f_identity(bone->inverse_bind);

    int64_t idx = s->bone_count;
    s->bone_count = new_count;
    return idx;
}

void rt_skeleton3d_compute_inverse_bind(void *obj) {
    if (!obj)
        return;
    rt_skeleton3d *s = (rt_skeleton3d *)obj;

    /* Compute global bind pose for each bone, then invert it */
    float *globals = (float *)malloc((size_t)s->bone_count * 16 * sizeof(float));
    if (!globals)
        return;

    for (int32_t i = 0; i < s->bone_count; i++) {
        if (s->bones[i].parent_index >= 0) {
            mat4f_mul_local(&globals[s->bones[i].parent_index * 16],
                            s->bones[i].bind_pose_local,
                            &globals[i * 16]);
        } else {
            memcpy(&globals[i * 16], s->bones[i].bind_pose_local, 16 * sizeof(float));
        }
        mat4f_invert(&globals[i * 16], s->bones[i].inverse_bind);
    }

    free(globals);
}

int64_t rt_skeleton3d_get_bone_count(void *obj) {
    return obj ? ((rt_skeleton3d *)obj)->bone_count : 0;
}

int64_t rt_skeleton3d_find_bone(void *obj, rt_string name) {
    if (!obj || !name)
        return -1;
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    const char *target = rt_string_cstr(name);
    if (!target)
        return -1;
    for (int32_t i = 0; i < s->bone_count; i++)
        if (strcmp(s->bones[i].name, target) == 0)
            return i;
    return -1;
}

rt_string rt_skeleton3d_get_bone_name(void *obj, int64_t index) {
    if (!obj)
        return rt_const_cstr("");
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    if (index < 0 || index >= s->bone_count)
        return rt_const_cstr("");
    return rt_const_cstr(s->bones[index].name);
}

void *rt_skeleton3d_get_bone_bind_pose(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    if (index < 0 || index >= s->bone_count)
        return NULL;
    const float *m = s->bones[index].bind_pose_local;
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/*==========================================================================
 * Animation3D implementation
 *=========================================================================*/

static void rt_animation3d_finalize(void *obj) {
    rt_animation3d *a = (rt_animation3d *)obj;
    for (int32_t i = 0; i < a->channel_count; i++)
        free(a->channels[i].keyframes);
    free(a->channels);
    a->channels = NULL;
}

void *rt_animation3d_new(rt_string name, double duration) {
    rt_animation3d *a = (rt_animation3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_animation3d));
    if (!a) {
        rt_trap("Animation3D.New: memory allocation failed");
        return NULL;
    }
    a->vptr = NULL;
    memset(a->name, 0, 64);
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(a->name, cstr, len);
        }
    }
    a->channels = NULL;
    a->channel_count = 0;
    a->channel_capacity = 0;
    a->duration = (float)duration;
    a->looping = 0;
    rt_obj_set_finalizer(a, rt_animation3d_finalize);
    return a;
}

void rt_animation3d_add_keyframe(
    void *obj, int64_t bone_index, double time, void *position, void *rotation, void *scale) {
    if (!obj)
        return;
    rt_animation3d *a = (rt_animation3d *)obj;

    /* Find or create channel for this bone */
    vgfx3d_anim_channel_t *ch = NULL;
    for (int32_t i = 0; i < a->channel_count; i++)
        if (a->channels[i].bone_index == (int32_t)bone_index) {
            ch = &a->channels[i];
            break;
        }

    if (!ch) {
        if (a->channel_count >= a->channel_capacity) {
            int32_t new_cap = a->channel_capacity == 0 ? 8 : a->channel_capacity * 2;
            vgfx3d_anim_channel_t *nc = (vgfx3d_anim_channel_t *)realloc(
                a->channels, (size_t)new_cap * sizeof(vgfx3d_anim_channel_t));
            if (!nc)
                return;
            a->channels = nc;
            a->channel_capacity = new_cap;
        }
        ch = &a->channels[a->channel_count++];
        memset(ch, 0, sizeof(vgfx3d_anim_channel_t));
        ch->bone_index = (int32_t)bone_index;
    }

    /* Add keyframe */
    if (ch->keyframe_count >= ch->keyframe_capacity) {
        int32_t new_cap = ch->keyframe_capacity == 0 ? 16 : ch->keyframe_capacity * 2;
        vgfx3d_keyframe_t *nk = (vgfx3d_keyframe_t *)realloc(
            ch->keyframes, (size_t)new_cap * sizeof(vgfx3d_keyframe_t));
        if (!nk)
            return;
        ch->keyframes = nk;
        ch->keyframe_capacity = new_cap;
    }

    vgfx3d_keyframe_t *kf = &ch->keyframes[ch->keyframe_count++];
    kf->time = (float)time;
    kf->position[0] = position ? (float)rt_vec3_x(position) : 0.0f;
    kf->position[1] = position ? (float)rt_vec3_y(position) : 0.0f;
    kf->position[2] = position ? (float)rt_vec3_z(position) : 0.0f;
    kf->rotation[0] = rotation ? (float)rt_quat_x(rotation) : 0.0f;
    kf->rotation[1] = rotation ? (float)rt_quat_y(rotation) : 0.0f;
    kf->rotation[2] = rotation ? (float)rt_quat_z(rotation) : 0.0f;
    kf->rotation[3] = rotation ? (float)rt_quat_w(rotation) : 1.0f;
    kf->scale_xyz[0] = scale ? (float)rt_vec3_x(scale) : 1.0f;
    kf->scale_xyz[1] = scale ? (float)rt_vec3_y(scale) : 1.0f;
    kf->scale_xyz[2] = scale ? (float)rt_vec3_z(scale) : 1.0f;
}

void rt_animation3d_set_looping(void *obj, int8_t loop) {
    if (obj)
        ((rt_animation3d *)obj)->looping = loop;
}

int8_t rt_animation3d_get_looping(void *obj) {
    return obj ? ((rt_animation3d *)obj)->looping : 0;
}

double rt_animation3d_get_duration(void *obj) {
    return obj ? ((rt_animation3d *)obj)->duration : 0.0;
}

rt_string rt_animation3d_get_name(void *obj) {
    return obj ? rt_const_cstr(((rt_animation3d *)obj)->name) : rt_const_cstr("");
}

/*==========================================================================
 * Keyframe sampling
 *=========================================================================*/

/// @brief Sample a channel at time t, producing a local TRS matrix.
static void sample_channel(const vgfx3d_anim_channel_t *ch, float t, float *out_local) {
    if (ch->keyframe_count == 0) {
        mat4f_identity(out_local);
        return;
    }
    if (ch->keyframe_count == 1) {
        build_trs_float(ch->keyframes[0].position,
                        ch->keyframes[0].rotation,
                        ch->keyframes[0].scale_xyz,
                        out_local);
        return;
    }

    /* Find bracketing keyframes */
    int k0 = 0, k1 = 1;
    for (int i = 0; i < ch->keyframe_count - 1; i++) {
        if (ch->keyframes[i + 1].time >= t) {
            k0 = i;
            k1 = i + 1;
            break;
        }
        k0 = i;
        k1 = i + 1;
    }

    float t0 = ch->keyframes[k0].time;
    float t1 = ch->keyframes[k1].time;
    float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    const vgfx3d_keyframe_t *key0 = &ch->keyframes[k0];
    const vgfx3d_keyframe_t *key1 = &ch->keyframes[k1];

    /* Interpolate position (linear) */
    float pos[3];
    for (int i = 0; i < 3; i++)
        pos[i] = key0->position[i] + alpha * (key1->position[i] - key0->position[i]);

    /* Interpolate rotation (SLERP) */
    float rot[4];
    quat_slerp_float(key0->rotation, key1->rotation, alpha, rot);

    /* Interpolate scale (linear) */
    float scl[3];
    for (int i = 0; i < 3; i++)
        scl[i] = key0->scale_xyz[i] + alpha * (key1->scale_xyz[i] - key0->scale_xyz[i]);

    build_trs_float(pos, rot, scl, out_local);
}

/// @brief Sample a channel at time t, returning separate TRS components
/// instead of a composed matrix. Used for crossfade blending.
static void sample_channel_trs(const vgfx3d_anim_channel_t *ch, float t,
                               float *out_pos, float *out_rot, float *out_scl) {
    if (ch->keyframe_count == 0) {
        out_pos[0] = out_pos[1] = out_pos[2] = 0.0f;
        out_rot[0] = out_rot[1] = out_rot[2] = 0.0f;
        out_rot[3] = 1.0f; /* identity quaternion */
        out_scl[0] = out_scl[1] = out_scl[2] = 1.0f;
        return;
    }
    if (ch->keyframe_count == 1) {
        memcpy(out_pos, ch->keyframes[0].position, 3 * sizeof(float));
        memcpy(out_rot, ch->keyframes[0].rotation, 4 * sizeof(float));
        memcpy(out_scl, ch->keyframes[0].scale_xyz, 3 * sizeof(float));
        return;
    }

    /* Find bracketing keyframes (same logic as sample_channel) */
    int k0 = 0, k1 = 1;
    for (int i = 0; i < ch->keyframe_count - 1; i++) {
        if (ch->keyframes[i + 1].time >= t) {
            k0 = i;
            k1 = i + 1;
            break;
        }
        k0 = i;
        k1 = i + 1;
    }

    float t0 = ch->keyframes[k0].time;
    float t1 = ch->keyframes[k1].time;
    float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    const vgfx3d_keyframe_t *key0 = &ch->keyframes[k0];
    const vgfx3d_keyframe_t *key1 = &ch->keyframes[k1];

    for (int i = 0; i < 3; i++)
        out_pos[i] = key0->position[i] + alpha * (key1->position[i] - key0->position[i]);
    quat_slerp_float(key0->rotation, key1->rotation, alpha, out_rot);
    for (int i = 0; i < 3; i++)
        out_scl[i] = key0->scale_xyz[i] + alpha * (key1->scale_xyz[i] - key0->scale_xyz[i]);
}

/*==========================================================================
 * AnimPlayer3D implementation
 *=========================================================================*/

static void rt_anim_player3d_finalize(void *obj) {
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    free(p->bone_palette);
    p->bone_palette = NULL;
    free(p->local_transforms);
    p->local_transforms = NULL;
    free(p->globals_buf);
    p->globals_buf = NULL;
}

void *rt_anim_player3d_new(void *skeleton) {
    if (!skeleton) {
        rt_trap("AnimPlayer3D.New: null skeleton");
        return NULL;
    }
    rt_skeleton3d *skel = (rt_skeleton3d *)skeleton;

    rt_anim_player3d *p = (rt_anim_player3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_anim_player3d));
    if (!p) {
        rt_trap("AnimPlayer3D.New: memory allocation failed");
        return NULL;
    }
    p->vptr = NULL;
    p->skeleton = skel;
    p->current = NULL;
    p->crossfade_from = NULL;
    p->current_time = 0.0f;
    p->crossfade_time = 0.0f;
    p->crossfade_duration = 0.0f;
    p->crossfade_from_time = 0.0f;
    p->speed = 1.0f;
    p->playing = 0;

    size_t palette_size = (size_t)skel->bone_count * 16 * sizeof(float);
    p->bone_palette = (float *)calloc(1, palette_size);
    p->local_transforms = (float *)calloc(1, palette_size);

    /* Initialize palette to identity */
    for (int32_t i = 0; i < skel->bone_count; i++)
        mat4f_identity(&p->bone_palette[i * 16]);

    rt_obj_set_finalizer(p, rt_anim_player3d_finalize);
    return p;
}

void rt_anim_player3d_play(void *obj, void *animation) {
    if (!obj)
        return;
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    p->current = (rt_animation3d *)animation;
    p->current_time = 0.0f;
    p->playing = animation ? 1 : 0;
    p->crossfade_from = NULL;
}

void rt_anim_player3d_crossfade(void *obj, void *animation, double duration) {
    if (!obj || !animation)
        return;
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    p->crossfade_from = p->current;
    p->crossfade_from_time = p->current_time;
    p->current = (rt_animation3d *)animation;
    p->current_time = 0.0f;
    p->crossfade_time = 0.0f;
    p->crossfade_duration = (float)duration;
    p->playing = 1;
}

void rt_anim_player3d_stop(void *obj) {
    if (!obj)
        return;
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    p->playing = 0;
}

/// @brief Compute the bone palette from the current animation state.
static void compute_bone_palette(rt_anim_player3d *p) {
    rt_skeleton3d *skel = p->skeleton;
    if (!skel || skel->bone_count == 0)
        return;

    /* Start with bind pose for all bones */
    for (int32_t i = 0; i < skel->bone_count; i++)
        memcpy(&p->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

    /* Override with animated transforms from current animation */
    if (p->current) {
        for (int32_t c = 0; c < p->current->channel_count; c++) {
            int32_t bone = p->current->channels[c].bone_index;
            if (bone >= 0 && bone < skel->bone_count)
                sample_channel(
                    &p->current->channels[c], p->current_time, &p->local_transforms[bone * 16]);
        }
    }

    /* Crossfade: blend with previous animation using TRS decomposition.
     * SLERP quaternion rotation (avoids skew/shear from raw matrix lerp).
     * Linear lerp for position and scale. */
    if (p->crossfade_from && p->crossfade_duration > 0.0f) {
        float factor = p->crossfade_time / p->crossfade_duration;
        if (factor > 1.0f)
            factor = 1.0f;

        for (int32_t c = 0; c < p->crossfade_from->channel_count; c++) {
            int32_t bone = p->crossfade_from->channels[c].bone_index;
            if (bone < 0 || bone >= skel->bone_count)
                continue;

            /* Sample "from" animation into TRS */
            float from_pos[3], from_rot[4], from_scl[3];
            sample_channel_trs(
                &p->crossfade_from->channels[c], p->crossfade_from_time, from_pos, from_rot, from_scl);

            /* Sample "to" (current) animation into TRS for this bone */
            float to_pos[3], to_rot[4], to_scl[3];
            /* Find the current animation's channel for this bone */
            int found_to = 0;
            if (p->current) {
                for (int32_t tc = 0; tc < p->current->channel_count; tc++) {
                    if (p->current->channels[tc].bone_index == bone) {
                        sample_channel_trs(
                            &p->current->channels[tc], p->current_time, to_pos, to_rot, to_scl);
                        found_to = 1;
                        break;
                    }
                }
            }
            if (!found_to) {
                /* No current channel for this bone — use bind pose TRS.
                 * Extract from bind pose matrix (diagonal = scale, last col = pos). */
                const float *bp = skel->bones[bone].bind_pose_local;
                to_pos[0] = bp[12];
                to_pos[1] = bp[13];
                to_pos[2] = bp[14];
                to_rot[0] = 0;
                to_rot[1] = 0;
                to_rot[2] = 0;
                to_rot[3] = 1; /* identity rotation */
                to_scl[0] = to_scl[1] = to_scl[2] = 1.0f;
            }

            /* Blend TRS components: lerp position/scale, SLERP rotation */
            float blend_pos[3], blend_rot[4], blend_scl[3];
            for (int i = 0; i < 3; i++) {
                blend_pos[i] = from_pos[i] + factor * (to_pos[i] - from_pos[i]);
                blend_scl[i] = from_scl[i] + factor * (to_scl[i] - from_scl[i]);
            }
            quat_slerp_float(from_rot, to_rot, factor, blend_rot);

            /* Compose blended TRS back into local transform matrix */
            build_trs_float(blend_pos, blend_rot, blend_scl, &p->local_transforms[bone * 16]);
        }
    }

    /* Two-phase computation: first build all global transforms, then
     * multiply each by its inverse bind pose to produce the palette.
     * Using bone_palette as scratch for globals would be wrong because
     * palette entries include inverse_bind, which would corrupt children. */
    /* Reuse globals workspace across frames to avoid per-frame malloc */
    if (!p->globals_buf) {
        p->globals_buf = (float *)malloc((size_t)skel->bone_count * 16 * sizeof(float));
        if (!p->globals_buf)
            return;
    }
    float *globals = p->globals_buf;

    /* Phase 1: compute global transforms (parent_global * local) */
    for (int32_t i = 0; i < skel->bone_count; i++) {
        if (skel->bones[i].parent_index >= 0) {
            mat4f_mul_local(&globals[skel->bones[i].parent_index * 16],
                            &p->local_transforms[i * 16],
                            &globals[i * 16]);
        } else {
            memcpy(&globals[i * 16], &p->local_transforms[i * 16], 16 * sizeof(float));
        }
    }

    /* Phase 2: palette[i] = global[i] * inverse_bind[i] */
    for (int32_t i = 0; i < skel->bone_count; i++)
        mat4f_mul_local(&globals[i * 16], skel->bones[i].inverse_bind, &p->bone_palette[i * 16]);
}

void rt_anim_player3d_update(void *obj, double delta_time) {
    if (!obj)
        return;
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    if (!p->playing || !p->current)
        return;

    p->current_time += (float)(delta_time * p->speed);

    /* Handle looping / end */
    if (p->current->looping) {
        if (p->current->duration > 0.0f)
            p->current_time = fmodf(p->current_time, p->current->duration);
    } else {
        if (p->current_time >= p->current->duration) {
            p->current_time = p->current->duration;
            p->playing = 0;
        }
    }

    /* Update crossfade */
    if (p->crossfade_from) {
        p->crossfade_time += (float)delta_time;
        p->crossfade_from_time += (float)(delta_time * p->speed);
        if (p->crossfade_time >= p->crossfade_duration)
            p->crossfade_from = NULL;
    }

    compute_bone_palette(p);
}

void rt_anim_player3d_set_speed(void *obj, double speed) {
    if (obj)
        ((rt_anim_player3d *)obj)->speed = (float)speed;
}

double rt_anim_player3d_get_speed(void *obj) {
    return obj ? ((rt_anim_player3d *)obj)->speed : 1.0;
}

int8_t rt_anim_player3d_is_playing(void *obj) {
    return obj ? ((rt_anim_player3d *)obj)->playing : 0;
}

double rt_anim_player3d_get_time(void *obj) {
    return obj ? ((rt_anim_player3d *)obj)->current_time : 0.0;
}

void rt_anim_player3d_set_time(void *obj, double time) {
    if (obj)
        ((rt_anim_player3d *)obj)->current_time = (float)time;
}

void *rt_anim_player3d_get_bone_matrix(void *obj, int64_t bone_index) {
    if (!obj)
        return NULL;
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    if (bone_index < 0 || bone_index >= p->skeleton->bone_count)
        return NULL;
    const float *m = &p->bone_palette[bone_index * 16];
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/*==========================================================================
 * Mesh3D extensions
 *=========================================================================*/

void rt_mesh3d_set_skeleton(void *mesh, void *skeleton) {
    (void)mesh;
    (void)skeleton;
    /* Skeleton reference is stored conceptually but not needed for CPU skinning —
     * the AnimPlayer3D holds the skeleton. This is for future GPU skinning. */
}

void rt_mesh3d_set_bone_weights(void *obj,
                                int64_t vertex_index,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    if (vertex_index < 0 || vertex_index >= m->vertex_count)
        return;

    vgfx3d_vertex_t *v = &m->vertices[vertex_index];
    v->bone_indices[0] = (uint8_t)b0;
    v->bone_indices[1] = (uint8_t)b1;
    v->bone_indices[2] = (uint8_t)b2;
    v->bone_indices[3] = (uint8_t)b3;
    v->bone_weights[0] = (float)w0;
    v->bone_weights[1] = (float)w1;
    v->bone_weights[2] = (float)w2;
    v->bone_weights[3] = (float)w3;
}

/*==========================================================================
 * Canvas3D extension — DrawMeshSkinned (CPU path)
 *=========================================================================*/

void rt_canvas3d_draw_mesh_skinned(
    void *canvas, void *mesh, void *transform, void *material, void *anim_player) {
    if (!canvas || !mesh || !transform || !material || !anim_player)
        return;

    rt_mesh3d *m = (rt_mesh3d *)mesh;
    rt_anim_player3d *p = (rt_anim_player3d *)anim_player;

    if (m->vertex_count == 0 || !p->bone_palette)
        return;

    rt_canvas3d *c = (rt_canvas3d *)canvas;
    if (c && c->backend &&
        vgfx3d_backend_prefers_gpu_skinning(c->backend->name, p->skeleton->bone_count)) {
        rt_mesh3d tmp = *m;
        tmp.bone_palette = p->bone_palette;
        tmp.bone_count = p->skeleton->bone_count;
        rt_canvas3d_draw_mesh(canvas, &tmp, transform, material);
        return;
    }

    /* Allocate temporary skinned vertex buffer */
    vgfx3d_vertex_t *skinned =
        (vgfx3d_vertex_t *)malloc((size_t)m->vertex_count * sizeof(vgfx3d_vertex_t));
    if (!skinned)
        return;

    /* Apply CPU skinning (always needed — GPU skinning not yet bypassing this).
     * The bone_palette is also passed through the draw command so GPU backends
     * can apply skinning on the vertex shader if they choose. */
    vgfx3d_skin_vertices(
        m->vertices, skinned, m->vertex_count, p->bone_palette, p->skeleton->bone_count);

    /* Create a temporary mesh wrapper with skinned vertices */
    rt_mesh3d tmp = *m;
    tmp.vertices = skinned;

    /* Register skinned buffer for cleanup at end of frame.
     * rt_canvas3d_draw_mesh stores a pointer to vertices in the deferred
     * draw queue, so we can't free until End() has processed the draw. */
    extern void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);
    rt_canvas3d_add_temp_buffer(canvas, skinned);

    /* Stash bone palette info on the mesh for draw command population.
     * Canvas3D's draw path reads these when building vgfx3d_draw_cmd_t. */
    tmp.bone_palette = p->bone_palette;
    tmp.bone_count = p->skeleton->bone_count;

    /* Draw using the normal pipeline */
    rt_canvas3d_draw_mesh(canvas, &tmp, transform, material);
}

/*==========================================================================
 * AnimBlend3D — multi-state animation blending
 *=========================================================================*/

#define MAX_BLEND_STATES 8

typedef struct {
    char name[64];
    rt_animation3d *animation;
    float weight;
    float anim_time;
    float speed;
    int8_t looping;
} anim_blend_state_t;

typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;
    anim_blend_state_t states[MAX_BLEND_STATES];
    int32_t state_count;
    float *bone_palette;
    float *local_transforms;
    float *temp_state_local;
} rt_anim_blend3d;

static void anim_blend3d_finalizer(void *obj) {
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    free(b->bone_palette);
    free(b->local_transforms);
    free(b->temp_state_local);
    b->bone_palette = b->local_transforms = b->temp_state_local = NULL;
}

void *rt_anim_blend3d_new(void *skel_obj) {
    if (!skel_obj)
        return NULL;
    rt_skeleton3d *skel = (rt_skeleton3d *)skel_obj;
    rt_anim_blend3d *b = (rt_anim_blend3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_anim_blend3d));
    if (!b) {
        rt_trap("AnimBlend3D.New: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->skeleton = skel;
    b->state_count = 0;
    memset(b->states, 0, sizeof(b->states));

    size_t buf_sz = (size_t)skel->bone_count * 16 * sizeof(float);
    b->bone_palette = (float *)calloc(1, buf_sz);
    b->local_transforms = (float *)calloc(1, buf_sz);
    b->temp_state_local = (float *)calloc(1, buf_sz);

    /* Initialize bone palette to identity */
    for (int32_t i = 0; i < skel->bone_count; i++)
        mat4f_identity(&b->bone_palette[i * 16]);

    rt_obj_set_finalizer(b, anim_blend3d_finalizer);
    return b;
}

int64_t rt_anim_blend3d_add_state(void *obj, rt_string name, void *anim_obj) {
    if (!obj || !anim_obj)
        return -1;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    if (b->state_count >= MAX_BLEND_STATES)
        return -1;

    anim_blend_state_t *st = &b->states[b->state_count];
    memset(st, 0, sizeof(anim_blend_state_t));
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(st->name, cstr, len);
            st->name[len] = '\0';
        }
    }
    st->animation = (rt_animation3d *)anim_obj;
    st->weight = 0.0f;
    st->anim_time = 0.0f;
    st->speed = 1.0f;
    st->looping = 1;
    return b->state_count++;
}

void rt_anim_blend3d_set_weight(void *obj, int64_t state, double weight) {
    if (!obj)
        return;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    if (state < 0 || state >= b->state_count)
        return;
    b->states[state].weight = (float)weight;
}

void rt_anim_blend3d_set_weight_by_name(void *obj, rt_string name, double weight) {
    if (!obj || !name)
        return;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    const char *target = rt_string_cstr(name);
    if (!target)
        return;
    for (int32_t i = 0; i < b->state_count; i++) {
        if (strcmp(b->states[i].name, target) == 0) {
            b->states[i].weight = (float)weight;
            return;
        }
    }
}

double rt_anim_blend3d_get_weight(void *obj, int64_t state) {
    if (!obj)
        return 0.0;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    if (state < 0 || state >= b->state_count)
        return 0.0;
    return (double)b->states[state].weight;
}

void rt_anim_blend3d_set_speed(void *obj, int64_t state, double speed) {
    if (!obj)
        return;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    if (state < 0 || state >= b->state_count)
        return;
    b->states[state].speed = (float)speed;
}

void rt_anim_blend3d_update(void *obj, double dt) {
    if (!obj || dt <= 0)
        return;
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    rt_skeleton3d *skel = b->skeleton;
    if (!skel || skel->bone_count == 0)
        return;
    int32_t bc = skel->bone_count;

    /* Advance all state timers */
    for (int32_t s = 0; s < b->state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        if (!st->animation || st->weight < 1e-6f)
            continue;
        st->anim_time += (float)dt * st->speed;
        if (st->looping && st->animation->duration > 0)
            st->anim_time = fmodf(st->anim_time, st->animation->duration);
        else if (st->anim_time > st->animation->duration)
            st->anim_time = st->animation->duration;
    }

    /* Start with bind pose */
    for (int32_t i = 0; i < bc; i++)
        memcpy(&b->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

    /* Blend active states */
    float total_weight = 0.0f;
    for (int32_t s = 0; s < b->state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        if (!st->animation || st->weight < 1e-6f)
            continue;

        /* Sample this state's channels into temp */
        for (int32_t i = 0; i < bc; i++)
            memcpy(
                &b->temp_state_local[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

        for (int32_t c = 0; c < st->animation->channel_count; c++) {
            int32_t bone = st->animation->channels[c].bone_index;
            if (bone < 0 || bone >= bc)
                continue;
            sample_channel(
                &st->animation->channels[c], st->anim_time, &b->temp_state_local[bone * 16]);
        }

        /* Weighted blend into local_transforms */
        float w = st->weight;
        total_weight += w;
        float blend_t = (total_weight > 1e-6f) ? w / total_weight : 1.0f;

        for (int32_t i = 0; i < bc * 16; i++)
            b->local_transforms[i] += (b->temp_state_local[i] - b->local_transforms[i]) * blend_t;
    }

    /* Two-phase: globals + inverse_bind → bone palette */
    float *globals = (float *)malloc((size_t)bc * 16 * sizeof(float));
    if (!globals)
        return;
    for (int32_t i = 0; i < bc; i++) {
        if (skel->bones[i].parent_index >= 0)
            mat4f_mul_local(&globals[skel->bones[i].parent_index * 16],
                            &b->local_transforms[i * 16],
                            &globals[i * 16]);
        else
            memcpy(&globals[i * 16], &b->local_transforms[i * 16], 16 * sizeof(float));
    }
    for (int32_t i = 0; i < bc; i++)
        mat4f_mul_local(&globals[i * 16], skel->bones[i].inverse_bind, &b->bone_palette[i * 16]);
    free(globals);
}

int64_t rt_anim_blend3d_state_count(void *obj) {
    return obj ? ((rt_anim_blend3d *)obj)->state_count : 0;
}

void rt_canvas3d_draw_mesh_blended(
    void *canvas, void *mesh_obj, void *transform, void *material, void *blend_obj) {
    if (!canvas || !mesh_obj || !transform || !material || !blend_obj)
        return;
    rt_anim_blend3d *b = (rt_anim_blend3d *)blend_obj;
    rt_skeleton3d *skel = b->skeleton;
    if (!skel || skel->bone_count == 0)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    if (mesh->vertex_count == 0)
        return;

    /* CPU skinning with blend palette */
    vgfx3d_vertex_t *skinned =
        (vgfx3d_vertex_t *)malloc(mesh->vertex_count * sizeof(vgfx3d_vertex_t));
    if (!skinned)
        return;

    extern void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src,
                                     vgfx3d_vertex_t *dst,
                                     uint32_t count,
                                     const float *palette,
                                     int32_t bone_count);
    vgfx3d_skin_vertices(
        mesh->vertices, skinned, mesh->vertex_count, b->bone_palette, skel->bone_count);

    rt_mesh3d tmp;
    tmp.vptr = NULL;
    tmp.vertices = skinned;
    tmp.vertex_count = mesh->vertex_count;
    tmp.vertex_capacity = mesh->vertex_count;
    tmp.indices = mesh->indices;
    tmp.index_count = mesh->index_count;
    tmp.index_capacity = mesh->index_count;

    extern void rt_canvas3d_add_temp_buffer(void *canvas, void *buffer);
    rt_canvas3d_add_temp_buffer(canvas, skinned);

    rt_canvas3d_draw_mesh(canvas, &tmp, transform, material);
}

#endif /* VIPER_ENABLE_GRAPHICS */
