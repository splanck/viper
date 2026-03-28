# Plan: FBX Animation Fixes + Crossfade Improvement

## Overview
After code verification: FBX keyframe extraction IS implemented, and bind pose reads full TRS. The duration loop is also correct. One confirmed issue remains: animation crossfade uses matrix lerp instead of TRS decomposition + SLERP.

## ~~1. FBX Keyframe Extraction~~ — ALREADY IMPLEMENTED
**Verified:** Lines 1190-1400 of `rt_fbx_loader.c` contain full animation extraction with `rt_animation3d_add_keyframe()` calls.

## ~~2. FBX Bind Pose~~ — ALREADY CORRECT
**Verified:** Lines 1054-1088 read translation, rotation (Euler ZYX decomposition), AND scale. Full TRS implemented.

## ~~3. Duration Loop~~ — ALREADY CORRECT
**Verified:** Uses `fmodf(p->current_time, p->current->duration)` for looping. Proper handling.

## 4. Animation Crossfade Uses Matrix Lerp (CONFIRMED)
**File:** `src/runtime/graphics/rt_skeleton3d.c:747-751`
**Verified:** Line 750 shows direct matrix element lerp:
```c
to[i] = from_local[i] + factor * (to[i] - from_local[i]);
```
This lerps 4x4 matrix elements directly, which produces shear/skew artifacts when bones rotate significantly between the two animation poses.

**Fix:** Decompose each bone's local transform into TRS components, then:
1. Translation: linear lerp
2. Rotation: quaternion SLERP (or nlerp for performance)
3. Scale: linear lerp
4. Recompose matrix after blending

**Implementation detail:**
```c
// Decompose mat4 to TRS
void decompose_trs(const double m[16], double pos[3], double quat[4], double scale[3]);

// In crossfade blend:
double pos_a[3], quat_a[4], scale_a[3];
double pos_b[3], quat_b[4], scale_b[3];
decompose_trs(from_local, pos_a, quat_a, scale_a);
decompose_trs(to_local, pos_b, quat_b, scale_b);

// Lerp position and scale
for (int i = 0; i < 3; i++) {
    pos_a[i] += factor * (pos_b[i] - pos_a[i]);
    scale_a[i] += factor * (scale_b[i] - scale_a[i]);
}
// SLERP rotation
quat_slerp(quat_a, quat_b, factor, result_quat);

// Recompose
compose_trs(result, pos_a, result_quat, scale_a);
```

**Note:** `rt_quat.c` already has `rt_quat_slerp()` — reuse it. Need to add `decompose_trs` and `compose_trs` helpers to `rt_mat4.c`.

## Files Modified
- `src/runtime/graphics/rt_skeleton3d.c` — Replace matrix lerp with TRS decompose + SLERP
- `src/runtime/graphics/rt_mat4.c` — Add `decompose_trs()` and `compose_trs()` helpers

## Verification
- Crossfade between idle (arms down) and wave (arm up): arm should rotate smoothly, not skew/shear
- Fast crossfade (0.1s) between 180-degree rotations: should take shortest path via SLERP
