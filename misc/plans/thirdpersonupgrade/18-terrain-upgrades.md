# Plan 18 — Terrain Holes, 8 Splat Layers, Slope/Height Auto-Blend

## 1. Objective & scope

Three authoring-fidelity gaps in `Terrain3D`: no holes (cave/dungeon entrances force mesh-swap workarounds), only 4 splat layers, and hand-painted-only splat weights. Add hole carving that propagates to rendering, LOD, collision, and nav; extend splats to 8 layers via a capability-gated texture-array path; and add procedural slope/height blend rules that generate splat weights.

**In scope:** (a) `SetHole/ClearHoles`; (b) 8-layer splats (GPU texture array, SW loop); (c) `SetSlopeLayer/SetHeightLayer` weight generation; (d) streamed-tile compatibility (stitching, per-tile holes via manifest).
**Out of scope:** raising `TERRAIN3D_MAX_DIM`, voxel/overhang terrain, runtime sculpting.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **Caps:** `TERRAIN3D_MAX_DIM 4096`, `TERRAIN3D_MAX_CHUNKS 65536`, `TERRAIN_MAX_SPLAT_LAYERS 4` with per-layer textures + UV scales (`rt_terrain3d.c:80-84,114-115`).
- **Structure:** chunked heightmap terrain with LOD meshes (`rt_terrain3d_build.inc`, `_lod.inc`, `_draw.inc`), 16-bit heightmap ingest (`SetHeightmap`), splat texturing + normal generation (`rendering3d.md` §Terrain3D).
- **Collision:** heightfield collider from a Pixels heightmap (`rt_collider3d_new_heightfield`, `rt_collider3d.h:47`) with `sample_heightfield_raw` queries (`:103`); streamed tiles own `*_heightfield_collider` entities and `*_navmesh_source` nodes (`game3d.md` §WorldStream3D).
- **Stream stitching:** shared-edge tiles stitch border samples, invalidate LOD caches, rebuild collider/nav sources (`game3d.md` §WorldStream3D) — hole edits must ride the same invalidation path.
- **Nav bake:** consumes flattened scene meshes incl. hidden terrain nav sources (`World3D.bakeNavMesh`, `game3d.md`).
- **Backend textures:** texture arrays are not currently part of the material texture surface; capability strings gate backend features (`BackendSupports` pattern).

## 3. Design

### 3.1 Holes

- Model: a per-terrain list of world-space XZ rectangles (v1; radius holes composable later): `SetHole(x, z, width, depth) -> i64` (index), `RemoveHole(i)`, `ClearHoles()`. Internally rasterized to a per-cell hole bitmask at heightmap resolution.
- **Render/LOD:** chunk (re)build skips triangles whose cells are holed; LOD levels test at their own stride (a coarse triangle is skipped if any covered fine cell is holed — conservative, no distant shimmer over the pit). Affected chunks rebuild lazily (existing chunk invalidation).
- **Collision:** `rt_collider3d_sample_heightfield_raw` returns out-of-range for holed cells (physics falls through, as intended); the heightfield collider gains the same bitmask (new `rt_collider3d_heightfield_set_holes_raw` internal setter the terrain calls).
- **Nav:** nav-source mesh regenerates without holed triangles; `RebuildTile` covers tiled bakes.
- **Streamed tiles:** manifest tiles gain optional `"holes": [[x,z,w,d]...]` applied at payload instantiation; stitching ignores holes (border samples still stitch heights).

### 3.2 8 splat layers

- Raise `TERRAIN_MAX_SPLAT_LAYERS` to 8; weights move from one RGBA splat map to two (RGBA0 = layers 0-3, RGBA1 = 4-7), API `SetSplatMap(index, pixels)` (existing single-map call maps to index 0).
- **GPU path:** pack layer textures into a texture array (one bind) + both splat maps; gated by `BackendSupports("texture-array")` (new capability; Metal/D3D11/GL3.3 all support arrays). Fallback when unsupported: layers 4-7 ignored with a one-time recoverable diagnostic (SW implements all 8 — correctness baseline).
- **SW path:** loop 8 layers (straightforward extension of the 4-layer loop).

### 3.3 Slope/height auto-blend

`SetSlopeLayer(layer, minSlopeDeg, maxSlopeDeg, sharpness)` / `SetHeightLayer(layer, minY, maxY, sharpness)` — on demand (`RebuildSplatWeights()`), generate the splat maps procedurally from the height/normal fields: per-texel weight = product of smoothstep bands; normalized across configured layers; hand-painted maps and rules compose (rules write into the same maps; painting afterwards overrides). Runs at heightmap resolution on the CPU; deterministic.

## 4. Implementation steps

1. Hole bitmask + `SetHole/RemoveHole/ClearHoles` + chunk-skip rendering + LOD-conservative skip; SW golden (terrain with a pit).
2. Heightfield-collider hole support + fall-through physics test; nav-source regeneration + bake test.
3. Streamed-tile `holes` manifest field + stitch compatibility test.
4. Two-splat-map storage + 8-layer SW loop + `SetSplatMap(index, …)`.
5. GPU texture-array path + capability key (Metal verify; GL/D3D11 waiver); 4-layer scenes stay on the existing path unchanged.
6. Slope/height rules + `RebuildSplatWeights` + goldens (mountain fixture: rock above slope threshold, grass valleys, snow caps).
7. runtime.def + audits + ADR + docs (`rendering3d.md` §Terrain3D expansion).

## 5. Public API changes (runtime.def)

`Viper.Graphics3D.Terrain3D` additions:

```
RT_METHOD("SetHole","i64(obj,f64,f64,f64,f64)",…)  RT_METHOD("RemoveHole","i1(obj,i64)",…)
RT_METHOD("ClearHoles","void(obj)",…)               RT_PROP("HoleCount","i64",get)
RT_METHOD("SetSplatMap","void(obj,i64,obj)",…)      /* index 0..1 */
RT_METHOD("SetSlopeLayer","void(obj,i64,f64,f64,f64)",…)
RT_METHOD("SetHeightLayer","void(obj,i64,f64,f64,f64)",…)
RT_METHOD("RebuildSplatWeights","void(obj)",…)
```

No new classes. `BackendSupports("texture-array")` documented. ADR `00xx-terrain-holes-splats.md`.

## 6. Tests

- **Hole render (SW golden):** 2×2 m hole ⇒ pit pixels show background/underground geometry; all LOD distances hide the same footprint (camera sweep, no shimmer triangles) (fail-before: no API).
- **Fall-through:** dynamic sphere over the hole falls past terrain height; character controller walks off the edge (grounded false inside the hole footprint).
- **Nav:** baked navmesh has no polygons over the hole (`IsWalkable(center)` false); `RebuildTile` after `SetHole` updates a tiled bake.
- **8 layers (SW):** fixture with 8 distinct-color layers and generated weights renders all 8 (per-region dominant-color asserts).
- **Rules:** slope rule puts rock (not grass) on a 60° face; height rule puts snow above Y threshold; texel weights sum to 1 (±1/255).
- **Stitch:** two streamed tiles, hole at a tile interior ⇒ border stitching unchanged (existing seam telemetry green).
- **Compat:** 4-layer scenes byte-identical SW before/after.

## 7. Verification gates

Full build + ctest; terrain + streaming lanes; SW goldens; Metal verify + waiver; `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **LOD-conservative holing** can widen pits at extreme distance (whole coarse triangle skipped) — acceptable and invisible under fog; documented.
- **Collider/nav coherence:** all three consumers (render/physics/nav) must read the *same* bitmask rasterization — single source in the terrain, pushed outward; never rasterize twice.
- **Texture-array sampler budget:** arrays free up per-layer samplers on GL3.3 (currently a constraint per the shadow-slot notes) — net win, but verify GL binding plan on the waiver pass.
- Two splat maps double splat memory only when layers 4-7 are used (map 1 lazily allocated).
