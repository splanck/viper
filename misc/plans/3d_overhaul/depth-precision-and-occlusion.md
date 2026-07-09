# Depth Precision (Reversed-Z) and Rasterized CPU Occlusion

**STATUS: BOTH IMPLEMENTED (2026-07-09).** The sections below preserve the
original designs with landing notes.

The as-landed reversed-Z design improved on the plan below: instead of new
projection builders and shader remap rewrites, Canvas3D negates the
projection's z row when the backend advertises `reversed_z` (a reversed
GL-convention projection is exactly the standard one with the z row negated).
Every (VP, inverse-VP) consumer — frustum extraction, occlusion projection,
unprojection, TAA reprojection — is self-consistent automatically, so the
per-backend surface reduced to: depth clears (0), compares (Greater family),
skybox at far = 0, sky-guard/SSR-march direction flips, soft-particle
linearization input flips, canonical depth-probe publishing, and a dedicated
standard-Less shadow state (D3D11 previously shared the scene state; GL's
shadow pass previously inherited whatever DepthFunc ran last — both fixed).
Shadow maps keep the standard convention everywhere; the software backend
stays standard as the deterministic golden reference. Verified live on Metal
(depth ordering + full shadowed frame) and by the projection unit fixture.

The as-landed Hi-Z rasterizer follows the plan's shape without the authored
occluder-mesh API: eligible opaque draws (already excluding alpha/masked/
double-sided/deforming/instanced) rasterize their actual triangles into a
256x256 fine view-depth buffer (perspective-correct 1/w interpolation,
1024-triangle-per-draw and 8192-per-frame budgets); fully-written 4x4 blocks
fold into the coarse 64x64 grid as their conservative max depth, so the
coverage TEST — margins and covered-streak hysteresis included — is unchanged.
The AABB-rectangle write survives only as the over-budget fallback. Proven by
a unit fixture whose 45-degree-rotated occluder (deep AABB, rejected by the
span guard) culls the draws behind it. Authored occluder-proxy meshes remain
a possible future refinement for very high-poly walls.

---

Two structural renderer upgrades scoped during the 2026-07 engine-improvement
pass. Both were deliberately split out from that pass: each sweeps a
convention that every backend, the culling math, and the golden baselines
depend on, so they need their own staged landings with per-backend parity
verification rather than riding along with twenty other changes.

## 1. Reversed-Z depth

**Problem.** `rt_mat4_perspective` emits GL-convention `[-1, 1]` NDC depth
with standard (non-reversed) Z, and every backend consumes it into
`Depth32Float` targets cleared to `1.0` with `Less` compares (Metal remaps via
`clipZToBackend`: `z = z*0.5 + w*0.5`). Standard-Z over float depth is the
worst-precision pairing — float precision is dense near 0 exactly where
standard-Z least needs it. At Ridgebound/ASHFALL clip ranges (near 0.1, far
5000+) this produces distant z-fighting shimmer.

**Design.**
- Keep `rt_mat4_perspective` (public Math API, GL convention) untouched;
  introduce a renderer-internal reversed projection used only for the scene
  pass: swap near/far mapping so NDC z = 0 at far, 1 at near (D3D-style
  `[0, 1]` clip range).
- Per backend: clear depth to `0.0`, compare `Greater`/`GreaterEqual`
  (`LessEqual` skybox becomes `GreaterEqual` at z = 0), drop the Metal
  `clipZToBackend` remap (reversed projection already lands in `[0, 1]`),
  OpenGL requires `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` (core 4.5,
  else keep the legacy path), software raster flips its compare + clear.
- Depth consumers to sweep: SSR/TAA/soft-particle linearization
  (`vgfx3d_backend.h` clip planes), CPU post-FX `postfx_world_at`, the lens
  flare/scene depth probes (window-depth convention comment in
  `vgfx3d_backend.h`), shadow sampling is unaffected (light-space ortho keeps
  its own convention), the CPU occlusion grid depths are view-space distances
  (unaffected).
- Golden baselines: the software backend flip changes every golden frame's
  depth resolution behavior; regenerate via `./scripts/update_goldens.sh`
  after visual sign-off.
- Land order: software first (deterministic reference + full test coverage),
  then Metal (live verification), then GL/D3D11 with the platform smoke runs.

## 2. Rasterized CPU occlusion (software Hi-Z)

**Problem.** The CPU occlusion grid stores one conservative depth per cell,
written from projected AABB *rectangles*. Depth-spanning boxes cannot write
(span guard), so complex occluders contribute nothing; the rect
over-estimates silhouettes for diagonal geometry. The 2026-07 pass already
removed the 1/3-screen rect cap (near walls now occlude) and moved testing to
front-to-back order, but the grid is still AABB-proxy quality.

**Design.**
- Replace rect writes with a small software depth rasterizer
  (~256x144 float): rasterize actual occluder triangles — the lowest LOD
  level of meshes flagged as occluders (new `Mesh3D.OccluderMesh` /
  `SceneNode3D.SetOccluder`), capped per frame by triangle budget.
- Rasterize only opaque, non-deforming, non-alpha draws; keep the existing
  covered-streak hysteresis and camera-cut invalidation unchanged (they gate
  flicker, not geometry quality).
- Test path unchanged in shape: project the candidate AABB, compare its
  nearest depth against the max rasterized depth over the covered cells
  (build a 1-level max-mip for O(1) wide tests).
- The span guard and alpha/double-sided write exclusions become unnecessary
  for rasterized occluders (exact coverage), but stay for any remaining
  AABB-proxy writes.

Both items should each land as one coherent unit with: unit fixtures
(depth-precision compare probe; occlusion fixture where a rasterized occluder
culls what the AABB grid could not), the VM-vs-native screenshot parity lane,
and a perf note in the release notes.
