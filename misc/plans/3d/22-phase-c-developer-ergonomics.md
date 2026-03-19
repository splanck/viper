# Phase C: Developer Ergonomics

## Goal

Add Transform3D helper, camera shake/smooth follow, and 3D path following — making game code cleaner and camera behavior professional.

## Dependencies

- Vec3, Quat, Mat4 math (complete in `src/runtime/graphics/`)
- Camera3D with FPS mode (`rt_camera3d.c`, struct in `rt_canvas3d_internal.h:62-74`)
- Spline system (`rt_spline.c` — 2D only, has CatmullRom/Bezier/Linear)
- Easing functions (`rt_easing.h` — 20 functions available)
- `build_trs_matrix` from `rt_scene3d.c` (TRS composition, reusable)

---

## C1. Transform3D Helper (~150 LOC)

### New Files

**`src/runtime/graphics/rt_transform3d.h`** (~25 LOC)
**`src/runtime/graphics/rt_transform3d.c`** (~125 LOC)

### Data Structure

```c
typedef struct {
    void *vptr;              /* GC dispatch — MUST be first */
    double position[3];      /* default (0,0,0) */
    double rotation[4];      /* quaternion (x,y,z,w), default (0,0,0,1) = identity */
    double scale[3];         /* default (1,1,1) */
    double matrix[16];       /* cached TRS matrix (row-major) */
    int8_t dirty;            /* 1 = matrix needs recomputation */
} rt_transform3d;
```

### TRS Composition

Reuse `build_trs_matrix` from `rt_scene3d.c:109-131`:
```c
// Already implemented — copies the same inline TRS composition:
// T(pos) * R(quat) * S(scale) — rotation columns scaled, translation in last column
```

For Transform3D, copy the function as `static void build_trs(...)` since it's `static` in rt_scene3d.c and can't be shared directly. Same algorithm:
```c
static void build_trs(const double *pos, const double *quat,
                       const double *scl, double *out) {
    double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    double x2 = x+x, y2 = y+y, z2 = z+z;
    /* ... identical to rt_scene3d.c:113-130 ... */
    out[3] = pos[0]; out[7] = pos[1]; out[11] = pos[2];
    out[12] = out[13] = out[14] = 0.0; out[15] = 1.0;
}
```

### LookAt for Transform3D

Orient the transform to face a target point:
```c
void rt_transform3d_look_at(void *obj, void *target, void *up_vec) {
    rt_transform3d *xf = (rt_transform3d *)obj;
    /* Compute forward = normalize(target - position) */
    /* Compute right = normalize(cross(up, forward)) */
    /* Compute true_up = cross(forward, right) */
    /* Build rotation matrix from right/true_up/forward */
    /* Extract quaternion from rotation matrix */
    xf->dirty = 1;
}
```

Quaternion extraction from rotation matrix uses the well-known Shepperd method.

### Public API

```c
void   *rt_transform3d_new(void);
void    rt_transform3d_set_position(void *xf, double x, double y, double z);
void   *rt_transform3d_get_position(void *xf);           /* returns Vec3 */
void    rt_transform3d_set_rotation(void *xf, void *quat);
void   *rt_transform3d_get_rotation(void *xf);           /* returns Quat */
void    rt_transform3d_set_euler(void *xf, double pitch, double yaw, double roll);
void    rt_transform3d_set_scale(void *xf, double x, double y, double z);
void   *rt_transform3d_get_scale(void *xf);              /* returns Vec3 */
void   *rt_transform3d_get_matrix(void *xf);             /* returns Mat4 (lazy recompute) */
void    rt_transform3d_translate(void *xf, void *delta); /* Vec3 offset added to position */
void    rt_transform3d_rotate(void *xf, void *axis, double angle); /* axis-angle rotation applied */
void    rt_transform3d_look_at(void *xf, void *target, void *up); /* orient toward point */
```

### Namespace: `Viper.Graphics3D.Transform3D`

### RuntimeClasses.hpp: add `RTCLS_Transform3D`
### runtime.def: 12 RT_FUNC + 1 RT_CLASS
### Stubs: 12 functions
### CMakeLists: add `graphics/rt_transform3d.c`
### RuntimeSignatures.cpp: add `#include "rt_transform3d.h"`

---

## C2. Camera Shake + Smooth Follow (~150 LOC)

### Modified File: `src/runtime/graphics/rt_camera3d.c` (+150 LOC)

### Camera3D Struct Additions (`rt_canvas3d_internal.h`)

Add after `fps_pitch`:
```c
/* Camera shake state */
double shake_intensity;     /* current intensity (world units, decays over time) */
double shake_duration;      /* remaining duration in seconds */
double shake_decay;         /* decay rate per second [0-10] (default 5.0) */
double shake_offset[3];     /* current random offset applied to eye position */
uint32_t shake_seed;        /* deterministic PRNG seed for reproducible shake */
```

### Init in `rt_camera3d_new`:
```c
cam->shake_intensity = 0.0;
cam->shake_duration = 0.0;
cam->shake_decay = 5.0;
cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
cam->shake_seed = 0x12345678;
```

### Camera Shake Implementation

```c
void rt_camera3d_shake(void *obj, double intensity, double duration, double decay) {
    if (!obj) return;
    rt_camera3d *cam = (rt_camera3d *)obj;
    cam->shake_intensity = intensity;
    cam->shake_duration = duration;
    cam->shake_decay = decay;
}

/* Called internally by FPSUpdate or externally via update_shake */
static void apply_shake(rt_camera3d *cam, double dt) {
    if (cam->shake_duration <= 0.0) {
        cam->shake_offset[0] = cam->shake_offset[1] = cam->shake_offset[2] = 0.0;
        return;
    }
    cam->shake_duration -= dt;
    cam->shake_intensity *= exp(-cam->shake_decay * dt); /* exponential decay */

    /* Deterministic random offset (xorshift on seed) */
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
    cam->shake_offset[2] = (r1 * r2) * cam->shake_intensity * 0.3; /* less Z shake */
}
```

Integrate into `rt_camera3d_fps_update` by adding shake_offset to eye AFTER computing position:
```c
/* At end of FPSUpdate, after eye position is set: */
apply_shake(cam, dt);
cam->eye[0] += cam->shake_offset[0];
cam->eye[1] += cam->shake_offset[1];
cam->eye[2] += cam->shake_offset[2];
/* Rebuild view matrix with shaken eye position */
```

### Smooth Follow (Third-Person)

```c
void rt_camera3d_smooth_follow(void *obj, void *target_pos,
                                 double distance, double height,
                                 double speed, double dt) {
    if (!obj || !target_pos) return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    double tx = rt_vec3_x(target_pos);
    double ty = rt_vec3_y(target_pos) + height;
    double tz = rt_vec3_z(target_pos);

    /* Desired camera position: behind target using current yaw */
    double yaw_rad = cam->fps_yaw * (M_PI / 180.0);
    double desired[3] = {
        tx - sin(yaw_rad) * distance,
        ty,
        tz + cos(yaw_rad) * distance
    };

    /* Exponential damping (framerate-independent smoothing) */
    double t = 1.0 - exp(-speed * dt);
    cam->eye[0] += (desired[0] - cam->eye[0]) * t;
    cam->eye[1] += (desired[1] - cam->eye[1]) * t;
    cam->eye[2] += (desired[2] - cam->eye[2]) * t;

    /* Look at target (slightly below height offset for better framing) */
    double look_target[3] = {tx, ty - height * 0.3, tz};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look_target, up);

    apply_shake(cam, dt);
}
```

### Smooth Look-At

```c
void rt_camera3d_smooth_look_at(void *obj, void *target, double speed, double dt) {
    if (!obj || !target) return;
    rt_camera3d *cam = (rt_camera3d *)obj;

    /* Current forward direction */
    double cur_fwd[3] = {-cam->view[8], -cam->view[9], -cam->view[10]};

    /* Desired forward direction */
    double tx = rt_vec3_x(target) - cam->eye[0];
    double ty = rt_vec3_y(target) - cam->eye[1];
    double tz = rt_vec3_z(target) - cam->eye[2];
    double len = sqrt(tx*tx + ty*ty + tz*tz);
    if (len > 1e-8) { tx /= len; ty /= len; tz /= len; }

    /* Lerp forward direction */
    double t = 1.0 - exp(-speed * dt);
    double new_fwd[3] = {
        cur_fwd[0] + (tx - cur_fwd[0]) * t,
        cur_fwd[1] + (ty - cur_fwd[1]) * t,
        cur_fwd[2] + (tz - cur_fwd[2]) * t
    };
    /* Normalize */
    len = sqrt(new_fwd[0]*new_fwd[0] + new_fwd[1]*new_fwd[1] + new_fwd[2]*new_fwd[2]);
    if (len > 1e-8) { new_fwd[0]/=len; new_fwd[1]/=len; new_fwd[2]/=len; }

    /* Rebuild view matrix */
    double look_at[3] = {cam->eye[0]+new_fwd[0], cam->eye[1]+new_fwd[1], cam->eye[2]+new_fwd[2]};
    double up[3] = {0, 1, 0};
    build_look_at(cam->view, cam->eye, look_at, up);
}
```

### Public API (added to Camera3D)

```c
void rt_camera3d_shake(void *cam, double intensity, double duration, double decay);
void rt_camera3d_smooth_follow(void *cam, void *target, double distance,
                                 double height, double speed, double dt);
void rt_camera3d_smooth_look_at(void *cam, void *target, double speed, double dt);
```

### Header: add declarations to `rt_canvas3d.h`
### runtime.def: 3 RT_FUNC added to Camera3D class
### Stubs: 3 functions (no-op)

---

## C3. Path3D / Spline Following (~200 LOC)

### New Files

**`src/runtime/graphics/rt_path3d.h`** (~20 LOC)
**`src/runtime/graphics/rt_path3d.c`** (~180 LOC)

### Data Structure

```c
typedef struct {
    void *vptr;               /* GC dispatch */
    double *xs, *ys, *zs;    /* separate coordinate arrays (same pattern as rt_spline.c) */
    int32_t point_count;
    int32_t point_capacity;
    int8_t looping;
    double cached_length;
    int8_t length_dirty;
} rt_path3d;
```

### 3D Catmull-Rom Evaluation

Extending the 2D `catmull_rom_segment` from `rt_spline.c:211-229`:

```c
static void catmull_rom_3d(double p0x, double p0y, double p0z,
                            double p1x, double p1y, double p1z,
                            double p2x, double p2y, double p2z,
                            double p3x, double p3y, double p3z,
                            double t, double *ox, double *oy, double *oz) {
    double t2 = t * t, t3 = t2 * t;
    *ox = 0.5 * ((2.0*p1x) + (-p0x+p2x)*t + (2.0*p0x-5.0*p1x+4.0*p2x-p3x)*t2 +
                 (-p0x+3.0*p1x-3.0*p2x+p3x)*t3);
    *oy = 0.5 * ((2.0*p1y) + (-p0y+p2y)*t + (2.0*p0y-5.0*p1y+4.0*p2y-p3y)*t2 +
                 (-p0y+3.0*p1y-3.0*p2y+p3y)*t3);
    *oz = 0.5 * ((2.0*p1z) + (-p0z+p2z)*t + (2.0*p0z-5.0*p1z+4.0*p2z-p3z)*t2 +
                 (-p0z+3.0*p1z-3.0*p2z+p3z)*t3);
}
```

### Position Evaluation

```c
void *rt_path3d_get_position_at(void *obj, double t) {
    rt_path3d *p = (rt_path3d *)obj;
    if (!p || p->point_count < 2) return rt_vec3_new(0, 0, 0);

    /* Clamp or wrap t */
    if (p->looping) {
        t = fmod(t, 1.0);
        if (t < 0) t += 1.0;
    } else {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
    }

    /* Find segment */
    int n = p->point_count;
    double seg_f = t * (n - 1);
    int seg = (int)seg_f;
    double local_t = seg_f - seg;
    if (seg >= n - 1) { seg = n - 2; local_t = 1.0; }

    /* Get 4 control points (clamp for endpoints, wrap for looping) */
    int i0 = seg - 1, i1 = seg, i2 = seg + 1, i3 = seg + 2;
    if (p->looping) {
        i0 = (i0 + n) % n; i1 = i1 % n; i2 = i2 % n; i3 = i3 % n;
    } else {
        if (i0 < 0) i0 = 0;
        if (i3 >= n) i3 = n - 1;
    }

    double ox, oy, oz;
    catmull_rom_3d(p->xs[i0], p->ys[i0], p->zs[i0],
                   p->xs[i1], p->ys[i1], p->zs[i1],
                   p->xs[i2], p->ys[i2], p->zs[i2],
                   p->xs[i3], p->ys[i3], p->zs[i3],
                   local_t, &ox, &oy, &oz);
    return rt_vec3_new(ox, oy, oz);
}
```

### Direction (Tangent) Evaluation

```c
void *rt_path3d_get_direction_at(void *obj, double t) {
    /* Finite difference: (pos(t+eps) - pos(t-eps)) / (2*eps), normalized */
    double eps = 0.001;
    void *p0 = rt_path3d_get_position_at(obj, t - eps);
    void *p1 = rt_path3d_get_position_at(obj, t + eps);
    double dx = rt_vec3_x(p1) - rt_vec3_x(p0);
    double dy = rt_vec3_y(p1) - rt_vec3_y(p0);
    double dz = rt_vec3_z(p1) - rt_vec3_z(p0);
    double len = sqrt(dx*dx + dy*dy + dz*dz);
    if (len > 1e-8) { dx/=len; dy/=len; dz/=len; }
    return rt_vec3_new(dx, dy, dz);
}
```

### Arc Length

```c
double rt_path3d_get_length(void *obj) {
    rt_path3d *p = (rt_path3d *)obj;
    if (!p || p->point_count < 2) return 0.0;
    if (!p->length_dirty) return p->cached_length;

    /* Numerical integration: sample N points, sum segment distances */
    int steps = p->point_count * 20; /* 20 samples per control point */
    double total = 0.0;
    double prev_x, prev_y, prev_z;
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        void *pt = rt_path3d_get_position_at(obj, t);
        double x = rt_vec3_x(pt), y = rt_vec3_y(pt), z = rt_vec3_z(pt);
        if (i > 0) {
            double dx = x-prev_x, dy = y-prev_y, dz = z-prev_z;
            total += sqrt(dx*dx + dy*dy + dz*dz);
        }
        prev_x = x; prev_y = y; prev_z = z;
    }
    p->cached_length = total;
    p->length_dirty = 0;
    return total;
}
```

### Public API

```c
void   *rt_path3d_new(void);
void    rt_path3d_add_point(void *path, void *pos);          /* Vec3 */
void   *rt_path3d_get_position_at(void *path, double t);     /* Vec3 at t∈[0,1] */
void   *rt_path3d_get_direction_at(void *path, double t);    /* tangent Vec3 */
double  rt_path3d_get_length(void *path);                     /* total arc length */
int64_t rt_path3d_get_point_count(void *path);
void    rt_path3d_set_looping(void *path, int8_t loop);
void    rt_path3d_clear(void *path);
```

### Namespace: `Viper.Graphics3D.Path3D`

### RuntimeClasses.hpp: add `RTCLS_Path3D`
### runtime.def: 8 RT_FUNC + 1 RT_CLASS
### Stubs: 8 functions
### CMakeLists: add `graphics/rt_path3d.c`
### RuntimeSignatures.cpp: add `#include "rt_path3d.h"`

---

## Files Modified/Created Summary

| Action | File | Est. LOC |
|--------|------|----------|
| NEW | `src/runtime/graphics/rt_transform3d.h` | ~25 |
| NEW | `src/runtime/graphics/rt_transform3d.c` | ~125 |
| NEW | `src/runtime/graphics/rt_path3d.h` | ~20 |
| NEW | `src/runtime/graphics/rt_path3d.c` | ~180 |
| MOD | `src/runtime/graphics/rt_camera3d.c` | +150 |
| MOD | `src/runtime/graphics/rt_canvas3d_internal.h` | +8 (shake fields) |
| MOD | `src/runtime/graphics/rt_canvas3d.h` | +8 (declarations) |
| MOD | `src/runtime/graphics/rt_graphics_stubs.c` | +25 |
| MOD | `src/il/runtime/runtime.def` | +23 entries |
| MOD | `src/il/runtime/classes/RuntimeClasses.hpp` | +2 class IDs |
| MOD | `src/il/runtime/RuntimeSignatures.cpp` | +2 includes |
| MOD | `src/runtime/CMakeLists.txt` | +2 sources |
| MOD | `src/tests/unit/CMakeLists.txt` | +1 test |
| NEW | `src/tests/unit/test_rt_transform_path.cpp` | ~150 |

---

## Tests

### Transform3D Tests (6)
- Default state: position=(0,0,0), scale=(1,1,1), identity rotation
- SetPosition → GetMatrix has correct translation column
- SetEuler → rotation applied correctly
- Translate accumulates relative to current position
- LookAt orients toward target
- Dirty flag: matrix only recomputed when dirty

### Camera Tests (5)
- Shake: offset applied during duration
- Shake: offset decays to zero after duration
- SmoothFollow: camera converges toward target over multiple frames
- SmoothFollow: respects distance and height
- SmoothLookAt: forward direction converges toward target

### Path3D Tests (6)
- 2 points: linear interpolation (t=0 → first, t=1 → last)
- 4 points: smooth Catmull-Rom curve passes through control points
- Direction at midpoint is tangent to curve
- Arc length > straight-line distance for curved paths
- Looping: t wraps around smoothly
- Clear: point count drops to 0

## Verification

1. Build clean (zero warnings)
2. 1334+ ctest pass (new add ~17)
3. Demo: camera dolly shot following Path3D around a scene
4. FPS demo uses Camera3D.Shake on shooting impact
5. Third-person demo with SmoothFollow
6. Native compilation verified
7. `./scripts/check_runtime_completeness.sh` passes
