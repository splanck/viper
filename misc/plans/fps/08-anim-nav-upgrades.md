# 08 — Engine: Barycentric 2D Blend Trees, Retarget Bone Maps, Always-Tiled NavMesh, Agent Link State

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped: E26 freeform-directional 2D blending — from-scratch Bowyer–Watson Delaunay
> triangulation over sample space + barycentric weights (≤3 active clips, weights sum to 1;
> outside-hull queries project onto the nearest hull edge; <3 non-collinear samples falls
> back to legacy). `BlendTree3D.BlendMode` (0 freeform default / 1 legacy IDW) and a new
> read-only `BlendTree3D.Blend` property exposing the internal `AnimBlend3D` (per-sample
> weights were previously unobservable from Zia — surface gap closed). E27 explicit retarget
> maps: `Skeleton3D.SetBoneAlias(external, local)` + `get_AliasCount` (empty local removes);
> `Animation3D.Retarget` resolves dst-alias → exact name → src-alias reversal → humanoid
> role → index fallback. E28 always-tiled bakes: `NavMesh3D.Bake` auto-derives a tile size
> (extent/16, clamped ≥4 wu) and retains per-tile voxel sources, so `RebuildTile` is O(tile)
> on every baked mesh; `get_TileSize` reports it (0.0 for `Build`/`Import`). E29 agent link
> state: `NavAgent3D.OnOffMeshLink`/`LinkKind` match the current waypoint segment against
> off-mesh links (0.25 endpoint tolerance) for jump/ladder gameplay hooks. Coverage:
> `tests/runtime/test_anim_nav_upgrades.zia` (freeform ≤3-weight + sum-to-1 vs legacy ≥5
> spread on a 9-sample ring, alias add/remove/re-add + alias-only retarget non-null,
> Bake TileSize>0 + RebuildTile, idle-agent link defaults); anim/nav regression sweep and
> full `-L graphics3d` (93/93) green; runtime completeness green. Docs: rendering3d.md
> (BlendTree3D modes/Blend, Skeleton3D aliases, Retarget order, NavMesh tiling/TileSize,
> NavAgent link state + `New(navMesh, radius, height)` ctor fix), game3d.md (world bake
> hooks always tiled).
> Eliminates constraints #7 (`RebuildTile` refilters the whole mesh on non-tiled bakes),
> #8 (2D blend trees are inverse-distance², not barycentric), #9 (retargeting is name-based
> humanoid mapping only). Consumers: 18-animation (locomotion), 16-enemies (downloaded rigs),
> 22-world-systems (destructible barricades rewriting nav).

## 0. TL;DR

Three correctness/quality upgrades in animation + navigation: proper **freeform-directional 2D
blending** (Delaunay + barycentric interior weights) so strafing locomotion doesn't smear;
an **explicit bone-map retarget API** so any downloaded rig retargets deterministically; and
**always-tiled navmesh baking** so `RebuildTile` is O(tile) everywhere (destructibles depend
on it). Plus a tiny agent addition: query off-mesh-link traversal state.

## 1. Current state (verified anchors)

- BlendTree3D 2D: normalized inverse-distance-squared weighting over ALL samples, ≤16 samples
  (`rt_blendtree3d.c:14-16,41`) — off-manifold contributions (backpedal sample bleeding into
  forward-strafe), soft/rounded transitions. 1D is bracket-lerp (fine).
- Retarget: `Animation3D.Retarget(srcSkel,dstSkel)` = name-based humanoid-role mapping +
  proportional translation scaling (`rt_skeleton3d_animation.inc:377-633`); custom bone names
  fail to map silently.
- NavMesh: voxel-tiled bake path does true O(tile) `RebuildTile`; the plain `Build`/untiled
  `Bake` path **refilters the entire mesh and ignores tile coords**
  (`rt_navmesh3d_query.inc:854-880`, "(void)tile_x; (void)tile_z", comment "Real tiled data
  ownership is future work"). Off-mesh links, dynamic obstacles, area costs all real
  (`rt_navmesh3d_query.inc:548-584,784`; `rt_navmesh3d.c:114-132`).
- NavAgent: RVO avoidance real (`rt_navagent3d.c:436-634`); no public "am I on an off-mesh
  link" state (verify during implementation; add if absent).

## 2. Design

### E26 — Freeform-directional 2D blending
- Build phase (on first Evaluate after sample edits): Delaunay triangulation of the ≤16 sample
  points (Bowyer–Watson, from scratch, ~120 lines); store triangle list + adjacency.
- Evaluate: locate containing triangle (walk from last hit — coherent), barycentric weights
  over its 3 clips; outside the hull → project onto nearest hull edge, lerp its 2 clips
  (matching Unity freeform-directional behavior). Degenerate/collinear samples → fall back to
  the existing IDW path (keeps old content working; logged once via AssetDiagnostics3D warning).
- API unchanged (`BlendTree3D.New2D/AddSample/SetPosition`) + new opt-out
  `BlendTree3D.SetBlendMode(i64)` (0=freeform default, 1=legacy IDW) for regression safety.

### E27 — Explicit retarget maps
```text
Viper.Graphics3D.Skeleton3D.SetBoneAlias(str,str)            void(obj,str,str)
    — alias external name → this skeleton's bone (repeatable; cleared by empty target).
Viper.Graphics3D.Animation3D.RetargetWithMap(obj,obj,obj)    obj(obj,obj,obj)
    — (anim, srcSkel, dstSkel) honoring aliases on BOTH skeletons before the existing
      role-based fallback; returns the retargeted clip. Unmapped bones: listed via
      AssetDiagnostics3D.GetLoadWarnings (count + first 8 names) instead of silent skip.
```
Implementation: alias table (parallel name lists) consulted in the existing role-mapping
resolve (`rt_skeleton3d_animation.inc:377+`); zero behavior change without aliases.

### E28 — Always-tiled bakes
- `World3D.BakeNavMesh` / `NavMesh3D.Bake` route through the tiled voxel pipeline with an
  auto tile size (world extent / target 16×16 grid, clamped 4–64 m) — the untiled code path
  is deleted, `RebuildTile` is O(tile) unconditionally, and `Export/Import` writes the tiled
  format only (version-bumped header; old imports still readable, re-tiled on load).
- `NavMesh3D.get_TileCountX/Z`, `get_TileSize` getters (game debug draw + targeted rebuilds).
- Perf assert: bake time for the L3-scale terrain within 1.5× of the old untiled bake
  (recorded in banner).

### E29 — Agent link state
`NavAgent3D.get_OnOffMeshLink i1(obj)` + `get_LinkKind i64(obj)` (0 none, 1 drop, 2 jump —
kinds set at link authoring via existing off-mesh-link add params; verify the param carries
a user tag, else add one). Lets 16-enemies play leap/drop animations at the right moment.

## 3. Files

`src/runtime/graphics/3d/anim/rt_blendtree3d.c` (+ new `rt_delaunay2d.inc` local helper),
`src/runtime/graphics/3d/anim/rt_skeleton3d_animation.inc`, `rt_skeleton3d.c` (alias table),
`src/runtime/graphics/3d/nav/rt_navmesh3d.c`, `rt_navmesh3d_bake.inc`,
`rt_navmesh3d_query.inc` (delete untiled rebuild path), `rt_navagent3d.c`,
`src/il/runtime/runtime.def`, `docs/viperlib/graphics/rendering3d.md` (anim section) +
`game3d.md` (nav section), tests: extend blendtree/skeleton/navmesh unit suites.

## 4. Tests

1. Blend 2D: 8-sample locomotion ring + idle center — query at (0.7,0.7) yields exactly 3
   non-zero weights summing to 1 (was: all 9 non-zero under IDW); hull-exterior query clamps
   to edge pair; collinear samples fall back to IDW with warning; legacy mode bit-matches old
   goldens.
2. Delaunay unit: random point sets — empty-circumcircle property holds; duplicate points
   deduped; 3-point and collinear cases.
3. Retarget map: rig with bones `mixamorig:Hips…` aliased to `hips…` → retargeted clip matches
   the same-skeleton golden within ε; unmapped bone list surfaces exactly the un-aliased names;
   no-alias path byte-matches existing Retarget goldens.
4. Tiled bake: `Bake` on the ridgebound-style heightfield → tile grid getters sane;
   `RebuildTile` after a dynamic obstacle touches exactly 1 tile (probe the test hook
   `rt_navmesh3d_test_set_tile_source` counters); pathfinding across rebuilt tile boundary
   still valid; Export/Import round-trip preserves paths.
5. Bake-time perf recorded; agent link-state flips true during a scripted link traversal.

## 5. Verification gate

`ctest -R 'blendtree|skeleton|navmesh|navagent'` green → determinism probe (paths + blends
VM==native) → runtime completeness/surface audit → full no-skip build incl. `-L slow`.
Consumers switch immediately: 18-animation authors the 9-sample locomotion ring assuming
freeform mode; 22-world-systems destructibles call `RebuildTile` per barricade with no
whole-mesh cost; 26-assets-content documents the alias tables per downloaded pack.
