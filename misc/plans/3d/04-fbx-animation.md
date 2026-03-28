# Plan: FBX Animation Fixes + Crossfade Improvement — COMPLETE

## Status: All items resolved or verified already correct.

### ~~1. FBX Keyframe Extraction~~ — ALREADY IMPLEMENTED (verified)
Lines 1190-1400 of `rt_fbx_loader.c` contain full animation extraction.

### ~~2. FBX Bind Pose~~ — ALREADY CORRECT (verified)
Lines 1054-1088 read full TRS (translation, Euler rotation, scale).

### ~~3. Duration Loop~~ — ALREADY CORRECT (verified)
Uses `fmodf` for looping. Handles non-looping case properly.

### 4. Animation Crossfade — FIXED (2026-03-28)
**Was:** Raw 4x4 matrix element lerp in `compute_bone_palette` (line 683-684):
```c
to[i] = from_local[i] + factor * (to[i] - from_local[i]);
```

**Now:** TRS decomposition + SLERP:
1. Added `sample_channel_trs()` — variant of `sample_channel` that returns position/rotation/scale separately
2. During crossfade, both "from" and "to" animations are sampled into TRS components
3. Position and scale: linear lerp
4. Rotation: `quat_slerp_float()` (shortest-path quaternion interpolation)
5. Blended TRS is recomposed via `build_trs_float()`

**Why this matters:** Matrix lerp of two rotations produces sheared/scaled intermediate transforms. For a 180-degree crossfade, the matrix midpoint has near-zero determinant (the mesh collapses to a line). SLERP takes the shortest rotational arc, producing valid orthogonal matrices at every blend factor.

### Files Changed
- `src/runtime/graphics/rt_skeleton3d.c` — `sample_channel_trs()` + crossfade rewrite

### Tests Added
2 new tests in `test_rt_skeleton3d.cpp`:
- `test_crossfade_basic`: Creates two animations (identity → 90-deg Y rotation), crossfades at midpoint, verifies bone matrix is non-null
- `test_crossfade_preserves_structure`: Compile/run validation of TRS blend path

### Total: 29/29 skeleton, 1358/1358 full suite.
