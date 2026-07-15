# Plan 04 — Crowd Skinning Palettes + Animation LOD

Instanced skinning currently shares ONE ≤256-bone palette across the whole
draw (`vgfx3d_backend.h` instanced-skinned command: 32 characters × 8 bones).
That caps crowds at marching-in-lockstep or tiny skeletons. Move palettes to a
per-instance indexed buffer and add distance-based animation LOD so hundreds
of characters stay cheap.

## Design

1. **Palette buffer**: one large per-frame palette array (all instances'
   palettes concatenated, built by the existing CPU palette gather which
   already runs per character in `rt_skeleton3d_skinning.inc` — the frame
   arena from the tempmgr work holds the staging copy). Per-instance data
   gains a `palette_offset` (instance stream already carries per-instance
   transform + color; append one uint).
2. **Shader**: skinned-instanced vertex path indexes
   `palettes[palette_offset + boneIndex]` instead of the fixed 256-slot
   uniform block:
   - Metal: device-address buffer argument (palettes already bind as a buffer
     — lift the size cap and add the offset).
   - D3D11: StructuredBuffer<float4x4> SRV.
   - GL: texture buffer (TBO) — core since 3.1, no compute needed.
   - Software: CPU skinning already handles per-character palettes; only the
     auto-instancing merge criteria change (below).
3. **Auto-instancing merge**: `rt_canvas3d.c` auto-instance detection
   currently refuses skinned meshes with differing palettes; with offsets it
   can merge same-mesh+material skinned draws regardless of pose — extend the
   geometry/material key comparison, drop the palette-equality check.
4. **Animation LOD** (CPU cost is pose evaluation, not just GPU):
   - `AnimController3D.SetLodDistances(full: Float, half: Float, hold: Float)`
     (def row, controllers.def; stubs; manifest bump).
   - Beyond `full`: sample every 2nd frame (interpolate held pose); beyond
     `half`: every 4th; beyond `hold`: freeze pose, keep root motion.
   - Pose sharing: characters flagged `SetPoseGroup(id)` reuse one evaluated
     pose per group per frame (crowd background walkers) — evaluation keyed
     by (group, clip, quantized phase) in a per-world cache.
5. **Culled characters**: skip pose evaluation entirely when the entity's
   bounds failed frustum culling last frame (one-frame pop guarded by the
   existing fat-bounds hysteresis pattern from the query broadphase).

## Tests / acceptance

- Unit: palette offset packing + merge-key equality
  (`test_rt_canvas3d_gpu_paths.cpp` fixture backends get `gpu_skinning = 1`
  already; add palette-offset assertions on the recorded commands).
- Unit: LOD stride sampling — pose at distance bands matches full evaluation
  at the sampled frames (`test_rt_animcontroller3d.cpp`).
- Conformance: `"crowd"` scene mode — 64 instanced skinned characters at
  staggered clip phases; SW golden vs Metal.
- Perf gate: 200 animated characters (mixed LOD bands) — pose evaluation
  ≤ 2 ms CPU on the dev Mac (`Diagnostics3D` timer counters), instanced draw
  count collapses to ≤ 4 (`Canvas3D.BackendDrawCalls`).
