# ADR 0086: Terrain Holes, 8 Splat Layers, and Slope/Height Auto-Blend

Date: 2026-07-10

## Status

Accepted

## Context

Terrain3D had three authoring gaps for open worlds: no holes (cave and
dungeon entrances forced mesh-swap workarounds), a 4-layer splat cap, and
hand-painted-only splat weights. The plan assumed a GPU splat shader would
need a texture-array capability for 8 layers, but the actual pipeline has two
splat paths: a 4-layer realtime per-pixel path and a baked-diffuse composite
that renders identically on every backend.

## Decision

- **Holes:** authored XZ rectangles (terrain-local units) rasterize once into
  a per-cell bitmask that every consumer reads: chunk builds skip carved
  cells at all LOD strides (conservative — a coarse quad drops when any
  covered fine cell is holed, so no distant shimmer over pits),
  `BuildNavMesh` skips the same cells, and heightfield colliders receive a
  copy through `rt_terrain3d_get_hole_mask_raw` →
  `rt_collider3d_heightfield_set_holes_raw`, making
  `sample_heightfield_raw` report no surface inside the footprint (physics
  fall-through). Streamed tiles parse `"holes": [[x,z,w,d],...]` and apply
  them at instantiation, before the collider/nav entities build, so the
  single-rasterization invariant holds under streaming too.
- **8 splat layers:** the cap doubles to 8 with a second splat map
  (`SetSplatMapAt(1, pixels)` weights layers 4-7). Rather than growing the
  4-slot realtime shader surface across four backends, extended content
  routes through the existing baked composite — uniform output everywhere,
  zero shader drift, and the realtime path stays byte-identical for existing
  4-layer scenes. The planned `texture-array` capability is unnecessary in
  this architecture and was dropped; if a realtime 8-layer path is ever
  wanted it belongs in the LIT-phase shader batch.
- **Auto-blend rules:** `SetSlopeLayer`/`SetHeightLayer` configure per-layer
  smoothstep bands (slope in degrees from the surface normal, height in
  world Y); `RebuildSplatWeights` regenerates both splat maps at heightmap
  resolution in one deterministic CPU pass, normalizing weights across
  configured layers (unmatched texels fall back to layer 0). Painting a map
  afterwards overrides — last write wins.

## Consequences

- Render/physics/nav can never disagree about a hole: one bitmask, one
  rasterizer, pushed outward.
- LOD-conservative holing can widen pits slightly at coarse strides;
  invisible under fog and documented. Interior-chunk skirts are unaffected;
  a border-touching hole can show a skirt wall inside the pit.
- Splat memory doubles only when map 1 is actually assigned.
- Tests: `test_rt_terrain3d_upgrades` (nav-triangle carve at fine and coarse
  strides, collider fall-through via the shared mask, rule-generated weight
  normalization across both maps, streamed manifest holes, 8-layer setter
  surface).
