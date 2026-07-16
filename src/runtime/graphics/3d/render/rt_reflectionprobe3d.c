//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_reflectionprobe3d.c
// Purpose: Local reflection probes — captured 6-face cubemaps with a box
//   influence volume, completing the reflection chain SSR -> local probe ->
//   skybox IBL. Capture renders the scene through an off-screen RenderTarget3D
//   from the probe position and assembles a CubeMap3D consumable by the
//   existing environment-map machinery.
// Key invariants:
//   - Capture is explicit/scripted, never per-frame; CaptureDirty flags
//     re-capture requests (time-of-day hooks set it).
// Ownership/Lifetime:
//   - GC-managed; the probe retains its captured cubemap until finalized.
// Links: misc/plans/thirdpersonupgrade/15-reflection-probes.md, ADR 0089.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_reflectionprobe3d.h"
#include "rt_canvas3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_scene3d.h"
#include "rt_trap.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int32_t rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern int64_t rt_obj_class_id(void *obj);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_rendertarget3d_new(int64_t width, int64_t height);
extern void *rt_rendertarget3d_as_pixels(void *obj);
extern void *rt_camera3d_new(double fov_deg, double aspect, double near_val, double far_val);
extern void rt_camera3d_look_at(void *obj, void *eye, void *target, void *up);

typedef struct rt_reflectionprobe3d {
    void *vptr;
    double position[3];
    double box_min[3];
    double box_max[3];
    double influence_scale;
    int64_t resolution;
    int8_t capture_dirty;
    void *cubemap; /* retained CubeMap3D from the last capture */
} rt_reflectionprobe3d;

static int probe_read_vec3(void *v, double out[3]) {
    if (!v || rt_obj_class_id(v) != RT_VEC3_CLASS_ID)
        return 0;
    out[0] = rt_vec3_x(v);
    out[1] = rt_vec3_y(v);
    out[2] = rt_vec3_z(v);
    return 1;
}

static void reflectionprobe3d_finalize(void *obj) {
    rt_reflectionprobe3d *probe = (rt_reflectionprobe3d *)obj;
    if (probe && probe->cubemap && rt_obj_release_check0(probe->cubemap))
        rt_obj_free(probe->cubemap);
    if (probe)
        probe->cubemap = NULL;
}

void *rt_reflectionprobe3d_new(void *position, void *box_min, void *box_max) {
    double pos[3], bmin[3], bmax[3];
    if (!probe_read_vec3(position, pos) || !probe_read_vec3(box_min, bmin) ||
        !probe_read_vec3(box_max, bmax)) {
        rt_trap("ReflectionProbe3D.New: position and box bounds must be Vec3");
        return NULL;
    }
    rt_reflectionprobe3d *probe = (rt_reflectionprobe3d *)rt_obj_new_i64(
        RT_G3D_REFLECTIONPROBE3D_CLASS_ID, (int64_t)sizeof(rt_reflectionprobe3d));
    if (!probe) {
        rt_trap("ReflectionProbe3D.New: allocation failed");
        return NULL;
    }
    memset(probe, 0, sizeof(*probe));
    rt_obj_set_finalizer(probe, reflectionprobe3d_finalize);
    memcpy(probe->position, pos, sizeof(pos));
    for (int a = 0; a < 3; ++a) {
        probe->box_min[a] = bmin[a] < bmax[a] ? bmin[a] : bmax[a];
        probe->box_max[a] = bmin[a] < bmax[a] ? bmax[a] : bmin[a];
    }
    probe->influence_scale = 1.0;
    probe->resolution = 64;
    probe->capture_dirty = 1;
    return probe;
}

static rt_reflectionprobe3d *reflectionprobe3d_checked(void *obj, const char *method) {
    rt_reflectionprobe3d *probe =
        (rt_reflectionprobe3d *)rt_g3d_checked_or_null(obj, RT_G3D_REFLECTIONPROBE3D_CLASS_ID);
    if (!probe)
        rt_trap(method);
    return probe;
}

void *rt_reflectionprobe3d_get_position(void *obj) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.get_Position: invalid probe");
    if (!probe)
        return NULL;
    return rt_vec3_new(probe->position[0], probe->position[1], probe->position[2]);
}

void rt_reflectionprobe3d_set_influence_scale(void *obj, double scale) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.set_InfluenceScale: invalid probe");
    if (probe && isfinite(scale) && scale >= 1.0)
        probe->influence_scale = scale > 8.0 ? 8.0 : scale;
}

double rt_reflectionprobe3d_get_influence_scale(void *obj) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.get_InfluenceScale: invalid probe");
    return probe ? probe->influence_scale : 0.0;
}

void rt_reflectionprobe3d_set_resolution(void *obj, int64_t resolution) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.set_Resolution: invalid probe");
    if (!probe)
        return;
    if (resolution < 16)
        resolution = 16;
    if (resolution > 512)
        resolution = 512;
    probe->resolution = resolution;
}

int64_t rt_reflectionprobe3d_get_resolution(void *obj) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.get_Resolution: invalid probe");
    return probe ? probe->resolution : 0;
}

void rt_reflectionprobe3d_set_capture_dirty(void *obj, int8_t dirty) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.set_CaptureDirty: invalid probe");
    if (probe)
        probe->capture_dirty = dirty ? 1 : 0;
}

int8_t rt_reflectionprobe3d_get_capture_dirty(void *obj) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.get_CaptureDirty: invalid probe");
    return probe ? probe->capture_dirty : 0;
}

/// @brief True when @p position lies inside the influence-scaled proxy box.
int8_t rt_reflectionprobe3d_contains(void *obj, void *position) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.Contains: invalid probe");
    double pos[3];
    if (!probe || !probe_read_vec3(position, pos))
        return 0;
    for (int a = 0; a < 3; ++a) {
        double center = (probe->box_min[a] + probe->box_max[a]) * 0.5;
        double half = (probe->box_max[a] - probe->box_min[a]) * 0.5 * probe->influence_scale;
        if (pos[a] < center - half || pos[a] > center + half)
            return 0;
    }
    return 1;
}

/// @brief Retained captured cubemap (NULL before the first capture).
void *rt_reflectionprobe3d_get_cubemap(void *obj) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.get_Cubemap: invalid probe");
    if (!probe || !probe->cubemap)
        return NULL;
    rt_obj_retain_maybe(probe->cubemap);
    return probe->cubemap;
}

/// @brief Capture 6 faces of @p scene from the probe position through @p canvas.
/// @details Face order and orientations follow the CubeMap3D.New contract
///   (+X, -X, +Y, -Y, +Z, -Z). Explicit/scripted only — a capture re-renders the
///   scene six times. Clears CaptureDirty on success.
/// @return 1 on success.
int8_t rt_reflectionprobe3d_capture(void *obj, void *canvas, void *scene) {
    rt_reflectionprobe3d *probe =
        reflectionprobe3d_checked(obj, "ReflectionProbe3D.Capture: invalid probe");
    if (!probe || !canvas || !scene)
        return 0;
    static const double face_fwd[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const double face_up[6][3] = {
        {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    void *faces[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    /* HDR capture: the reflection chain is SSR -> local probe -> skybox IBL,
     * and the sky/IBL terms carry HDR range. An LDR (UNORM8) face target
     * clamped bright reflections to [0,1], making probe reflections dimmer
     * than the sky fallback they blend with. */
    void *target = rt_rendertarget3d_new_hdr(probe->resolution, probe->resolution);
    void *camera = rt_camera3d_new(90.0, 1.0, 0.05, 10000.0);
    int ok = target && camera;
    for (int f = 0; f < 6 && ok; ++f) {
        void *eye = rt_vec3_new(probe->position[0], probe->position[1], probe->position[2]);
        void *look = rt_vec3_new(probe->position[0] + face_fwd[f][0],
                                 probe->position[1] + face_fwd[f][1],
                                 probe->position[2] + face_fwd[f][2]);
        void *up = rt_vec3_new(face_up[f][0], face_up[f][1], face_up[f][2]);
        if (eye && look && up) {
            rt_camera3d_look_at(camera, eye, look, up);
            rt_canvas3d_set_render_target(canvas, target);
            rt_scene3d_draw(scene, canvas, camera);
            rt_canvas3d_reset_render_target(canvas);
            faces[f] = rt_rendertarget3d_as_pixels(target);
            if (!faces[f])
                ok = 0;
        } else {
            ok = 0;
        }
        if (eye && rt_obj_release_check0(eye))
            rt_obj_free(eye);
        if (look && rt_obj_release_check0(look))
            rt_obj_free(look);
        if (up && rt_obj_release_check0(up))
            rt_obj_free(up);
    }
    if (ok) {
        void *cubemap =
            rt_cubemap3d_new(faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]);
        if (cubemap) {
            if (probe->cubemap && rt_obj_release_check0(probe->cubemap))
                rt_obj_free(probe->cubemap);
            probe->cubemap = cubemap;
            probe->capture_dirty = 0;
        } else {
            ok = 0;
        }
    }
    for (int f = 0; f < 6; ++f) {
        if (faces[f] && rt_obj_release_check0(faces[f]))
            rt_obj_free(faces[f]);
    }
    if (target && rt_obj_release_check0(target))
        rt_obj_free(target);
    if (camera && rt_obj_release_check0(camera))
        rt_obj_free(camera);
    return ok ? 1 : 0;
}

#else
typedef int rt_reflectionprobe3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
