# 09 — Engine: Mesh Simplification (Auto-LOD), ASCII FBX, meshopt Decode

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E
>
> **Shipped.** E30: from-scratch QEM decimation (`rt_mesh_simplify.c/.h`) behind
> `Mesh3D.Simplify(mesh, targetTris)` — full-record vertex weld so attribute seams become
> protected borders (UV/material seams survive; subset placement keeps surviving-vertex
> attrs, skinning-safe), boundary edges penalized 10x via perpendicular planes,
> lazy-invalidation heap with deterministic tie-break, triangle-flip rejection, emits a
> compacted NEW mesh. `SceneNode.GenerateLODs(levels, ratio)` builds a ratio^k chain
> (<=4 levels), registers radius-derived distances via AddLOD, enables SetAutoLOD.
> Test: `g3d_test_mesh_simplify` (dense-sphere budget hit, determinism, no-grow guard,
> render smoke, LodCount). E31: ASCII FBX with the standard `; FBX` signature now routes
> through the ASCII geometry parser (loader.inc gate fix) instead of being rejected —
> positions/triangulation/normals/UVs/vertex colors import; signature files with no
> parsable geometry return null + `ASCII FBX file did not contain parsable mesh geometry`.
> Skins/anim curves in ASCII remain geometry-only (binary loader covers rigged content).
> Test: `g3d_test_fbx_ascii` (cube + attributed quad fixtures, malformed-null-plus-
> diagnostic, binary-path regression). Docs: rendering3d.md format matrix + LOD workflow,
> graphics3d-guide.md Mesh3D table.
>
> **Cut per plan section 2 stretch line:** E32 EXT_meshopt decoder — the glTF
> required-extension gate already fails cleanly (`requires EXT_meshopt_compression
> (unsupported)`); this is the documented next chunk. Deviation: quadric attribute-space
> extension (10-coeff position-only quadrics + protected seams instead of extended
> attribute quadrics) — seams are preserved rather than smoothly collapsed, which is the
> conservative choice for game LODs. Eliminates constraints #20 (`SetAutoLOD` only selects among *existing*
> meshes — rendering3d.md:566 — nothing synthesizes LODs) and #21 partially (ASCII FBX
> unsupported — rendering3d.md:70-71; EXT_meshopt_compression deferred —
> 3d_overhaul/09-gltf-coverage.md:24,40; Draco stays out of scope). Consumers: 26-assets
> (downloaded single-LOD models get real LOD chains), P18 content-scale pass.

## 0. TL;DR

Downloaded CC0 models ship one LOD. A 9-level game with 12 active enemies + set dressing needs
distance LODs everywhere or draw/skinning cost explodes. Implement **quadric-error-metric mesh
simplification** from scratch (`Mesh3D.Simplify`), a **one-call LOD-chain generator**
(`SceneNode.GenerateLODs`) feeding the existing `AddLOD`/`SetAutoLOD` machinery, **ASCII FBX**
parsing (many free packs export it), and (stretch) the open-spec **EXT_meshopt** vertex/index
decoder so compressed glTF from asset stores loads.

## 1. Current state (verified anchors)

- LOD selection machinery exists and is good: `SceneNode.AddLOD/SetAutoLOD/SetImpostor`
  (runtime.def region 13751-13753), screen-error auto selection proven by ridgebound's maples
  (`SetAutoLOD(true, 3px)`, forest.zia:233). Nothing generates the meshes.
- Mesh3D full geometry access: `AddVertex/AddTriangle/Reserve/RecalcNormals/CalcTangents/
  Clone/Transform` + skinning attrs `SetSkeleton/SetBoneWeights` (runtime.def 13364-13419
  region) — simplification can read/write through internal buffers directly.
- FBX: binary-only, recoverable loader (`assets/rt_fbx_loader_*.inc`); ASCII detected and
  rejected with "re-export as binary FBX".
- glTF: full importer (`assets/rt_gltf*.inc`); required-extension gate fails cleanly on
  `KHR_draco_mesh_compression` and `EXT_meshopt_compression` (gltf-coverage plan).

## 2. Design

### E30 — QEM simplification + LOD chains
```text
Viper.Graphics3D.Mesh3D.Simplify(i64)               obj(obj,i64)
    — returns a NEW mesh decimated to ≈targetTriangles (never in-place), preserving:
      UV seams and material boundaries (edge collapse restricted across attribute seams),
      bone weights (collapsed vertex inherits dominant-influence blend, re-normalized),
      normals (recomputed post-collapse), open borders (boundary quadric penalty ×10).
Viper.Graphics3D.Mesh3D.SimplifyToError(f64)        obj(obj,f64)   — error-bound variant
Viper.Graphics3D.SceneNode.GenerateLODs(i64,f64)    void(obj,i64,f64)
    — (levels 1..4, ratio e.g. 0.4): builds levels with tri counts ×ratio^k via Simplify,
      registers them through AddLOD with distance thresholds derived from mesh bounds,
      enables SetAutoLOD. Skinned meshes supported (weights carried).
Viper.Graphics3D.SceneAsset.GenerateAllLODs(i64,f64) void(obj,i64,f64) — whole-scene helper.
```
Algorithm (from scratch, Garland–Heckbert): per-vertex 4×4 quadrics from face planes; valid
pair = edge (no virtual pairs); min-heap of collapse costs; iterative collapse with
consistency checks (no triangle flips — normal dot test; no seam crossing); attribute-aware
via wedge splitting (position welded, UV/normal wedges tracked so seams survive). Deterministic:
stable tie-breaking by vertex index. Perf target: 30K-tri mesh → 4 LODs < 250 ms (recorded).

### E31 — ASCII FBX
Text tokenizer → same node-record IR the binary parser feeds (`rt_fbx_loader_*.inc` shares the
downstream scene builder). Handles: `Objects/Geometry/Model/Material/Deformer` subset matching
the binary feature set (meshes, skins, anims, materials); scientific-notation floats; `a:`
array attributes with `,`-separated values. Unsupported constructs degrade exactly like binary
(warning + skip). Recoverable: malformed ASCII returns null via the same
`rt_fbx_load_recoverable` contract (BUG-002 lineage).

### E32 — EXT_meshopt_compression decode (stretch; cut line documented)
Implement the three meshopt filters/modes needed by the spec (attributes/triangles/indices
streams with delta+zigzag+dezig transforms per the public EXT spec). Gate: if session B runs
long, ship E30+E31 and leave E32 as the documented next chunk — the required-extension gate
already fails cleanly, so nothing regresses.

## 3. Files

New `src/runtime/graphics/3d/assets/rt_mesh_simplify.c/.h` (pure, unit-testable; bump
`scripts/source_health_baseline.tsv`), `rt_model3d_api.inc` (GenerateLODs/GenerateAllLODs),
`assets/rt_fbx_ascii.inc` (new) + `rt_fbx_loader_*.inc` dispatch, `assets/rt_gltf_codec.inc`
(meshopt), `src/il/runtime/runtime.def`, `docs/viperlib/graphics/rendering3d.md`
(model-pipeline section: LOD workflow + format matrix update), tests:
`src/tests/unit/test_mesh_simplify.cpp` (new), `test_rt_fbx_ascii.cpp` (new), golden probes.

## 4. Tests

1. Simplify correctness: icosphere 20K tris → 2K keeps Hausdorff error < 1 % bounds diagonal
   (sampled); UV-seamed cube keeps seam UVs (golden render at LOD1 shows no texture swim);
   skinned cylinder keeps deformation (animated golden within tolerance); boundary edges of an
   open plane survive; degenerate input (already minimal) returns clone.
2. Determinism: same input → byte-identical output, VM==native.
3. GenerateLODs: 3 levels registered, thresholds monotonic, AutoLOD active; scene helper walks
   all nodes; render probe flips LODs by distance (existing auto-LOD counters).
4. ASCII FBX: golden ASCII export of the test rig loads with identical mesh/skeleton/anim
   counts as its binary twin; malformed ASCII → null + diagnostic; binary path untouched
   (existing FBX tests green).
5. meshopt (if shipped): reference-encoded fixture decodes to byte-identical accessors vs
   uncompressed twin; gate still clean-fails Draco.
6. Perf recorded: simplification timing table into the banner.

## 5. Verification gate

`ctest -R 'simplify|fbx|gltf'` + `-L graphics3d` green → goldens committed → runtime
completeness/surface audits + source-health baseline bump → full no-skip build.
Consumer: 26-assets-content pipeline step becomes "load GLB → GenerateAllLODs(3, 0.4) →
Instantiate", and the P18 content-scale pass asserts every spawned archetype has ≥3 LODs
via scene-node queries in the perf probe.
