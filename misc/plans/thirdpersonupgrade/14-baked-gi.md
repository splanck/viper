# Plan 14 — Baked GI: Lightmap Baker + SH Light-Probe Grid

## 1. Objective & scope

The single biggest visual multiplier for adventure-game interiors and dressed exteriors: offline-baked global illumination. Two halves: (a) a from-scratch CPU **lightmap baker** for static geometry, and (b) an **SH-9 light-probe grid** so dynamic objects (player, NPCs, props) receive the same bounced light. Today ambient is flat or skybox-IBL only — interiors either glow uniformly or need a forest of point lights.

**In scope:** (a) `LightBaker3D` (deterministic CPU path tracer over static scene geometry, direct+indirect, lightmap atlas output); (b) `Material3D.SetLightmap` + TEXCOORD_1 sampling in all four backends; (c) `LightProbeGrid3D` bake/sample/serialize; (d) shading integration (lightmapped surfaces skip dynamic ambient; dynamic draws sample probes).
**Out of scope:** runtime GI, directional lightmaps/normal-mapped GI (v2), GPU baking, emissive-mesh area lights beyond a simple emissive-sample term.

**Zero external dependencies — absolute.** Path tracing, SH projection, UV chart packing all from published math (precedent: the from-scratch BRDF LUT `vgfx3d_brdf_lut.c` and IBL SH-9 projector).

## 2. Current state (verified anchors)

- **No lightmap/probe code exists** (grep `lightmap|light_probe|global illum` — only a TEXCOORD_1 doc comment).
- **TEXCOORD_1 convention documented:** the SW sampler notes the "`TEXCOORD_1` convention for lightmap or detail textures" (`vgfx3d_backend_sw_texture.inc:303`) — mesh vertex format supports a second UV set; verify importer preservation (glTF `TEXCOORD_1` accessor handling) at write time.
- **SH-9 machinery exists:** IBL computes an SH-9 irradiance projection + GGX prefiltered chain + split-sum LUT, identical on all backends (`rendering3d.md` §Lighting Helpers, IBL paragraph) — the probe grid reuses the projector and the diffuse-irradiance evaluation.
- **Static flagging:** bodies have static flags but *render* staticness doesn't exist; `SceneNode` needs a `Static` hint bit (bake input + future optimizations).
- **Sampling scene geometry:** navmesh bake already flattens every `Mesh3D` under a `SceneGraph` with world transforms (`NavMesh3D.Bake` path, `rendering3d.md` §NavMesh3D) — the baker reuses this traversal pattern for its BVH build.
- **Material inputs for the tracer:** albedo color/map, emissive color/intensity via `Material3D` getters (runtime.def Material3D block).
- **Ray infrastructure:** `rt_raycast3d.c` provides mesh raycast primitives (scene-level ray queries); the baker builds its own triangle BVH for tracing throughput regardless.
- **Serialization precedent:** `VNAVMSH2` explicit little-endian export/import (`rendering3d.md` §NavMesh3D) — `.vlm` (lightmap atlas metadata) and `.vlpg` (probe grid) follow.

## 3. Design

### 3.1 `LightBaker3D` (offline, editor/CLI-invoked)

New `src/runtime/graphics/3d/render/rt_lightbaker3d.c` (+ internal BVH in a `.inc`). Invocation is explicit (`Bake(scene, options)`), never per-frame; long-running with a progress getter (main-thread slices or a blocking call with progress polling from a second Zia-visible method — v1: blocking with `BakeProgress` readable from the same thread between tile slices via a callback-free chunked API: `BakeStep() -> bool done`).

1. **Input set:** nodes flagged `SceneNode.SetStatic(true)` with meshes; lights flagged `CastsShadows`/enabled (directional/point/spot/ambient/emissive materials).
2. **UV charts:** if a mesh lacks TEXCOORD_1, generate per-triangle charts by normal clustering + rectangle packing into the atlas (simple, robust; quality charts are v2). Texel density from `options.texelsPerUnit` (default 8).
3. **Tracing:** deterministic path tracer — stratified hemisphere sampling (fixed seeds per texel — determinism), `options.bounces` (default 2), `options.samples` (default 64), BVH over world-space triangles; direct light sampled explicitly per light type; sky term from the canvas skybox/IBL SH.
4. **Output:** one or more RGBA16-equivalent `Pixels` atlas pages (stored 8-bit with a scale factor v1; HDR16F pages when render targets allow, `rt_rendertarget3d.c` HDR mirror precedent) + per-mesh chart transforms. Dilation pass over chart borders (bleed guard).
5. **Apply:** `Material3D.SetLightmap(pixels)` per baked mesh instance (bakes write material *instances*, `MakeInstance`, so shared materials stay clean) + node UV1 chart data. `Save(path)`/`Load(path)` for `.vlm`.

### 3.2 Shading integration (all four sources)

Lightmapped draws: `indirect = lightmap.rgb × albedo`; the flat ambient and probe terms are skipped for these surfaces; analytic lights still add direct contribution unless `options.bakeDirect` was set (then baked-direct lights are tagged and skipped at draw for those surfaces — light payload gains a per-light `bakedMask` bit; v1 keeps it simple: bake indirect only, `bakeDirect=false` default). SW raster implements first (baseline), GPU shaders port.

### 3.3 `LightProbeGrid3D`

- `New(min, max, spacing)` regular grid (bounds from the scene or explicit); `Bake(scene)` runs the same tracer per probe center → SH-9 RGB (27 floats/probe); occluded-probe handling: probes inside geometry marked invalid and in-filled from neighbors.
- Runtime: `World3D.setLightProbes(grid)`; per dynamic draw, trilinear-interpolate the 8 surrounding probes' SH and replace the flat-ambient term in the existing IBL/ambient slot (per-draw CPU interpolation → 27 floats into the draw constants; matches the per-draw ambient upload path).
- `Save/Load` `.vlpg` (versioned LE: bounds, spacing, validity bitmap, SH floats).

## 4. Implementation steps

1. `SceneNode.SetStatic` flag + importer TEXCOORD_1 verification/preservation test.
2. Baker skeleton: BVH + deterministic sampler + direct-only bake; SW-render parity fixture (a lit box room bakes ≈ its rendered direct lighting).
3. Indirect bounces + emissive term + dilation + atlas packing.
4. `SetLightmap` + SW raster sampling + `.vlm` save/load round-trip.
5. GPU shader ports (Metal verified locally; GL/D3D11 waiver) — batch with plans 15/16/19 shader work.
6. Probe grid: bake + validity infill + trilinear runtime term + `.vlpg` round-trip.
7. `World3D.setLightProbes` + Game3D docs/examples; golden interior scene (`examples/3d/baked_room/` new fixture) with committed baseline.
8. runtime.def + audits + ADR + docs (`rendering3d.md` new §Baked Lighting).

## 5. Public API changes (runtime.def)

```
RT_CLASS_BEGIN("Viper.Graphics3D.LightBaker3D", G3dLightBaker3D, "obj", G3dLightBakerNew)   /* New(scene) */
    RT_PROP("TexelsPerUnit","f64",…) RT_PROP("Samples","i64",…) RT_PROP("Bounces","i64",…)
    RT_PROP("Progress","f64",get)
    RT_METHOD("BakeStep","i1(obj)",…)          /* chunked; returns done */
    RT_METHOD("Apply","void(obj)",…)           /* install lightmaps on baked materials */
    RT_METHOD("Save","i1(obj,str)",…) RT_METHOD("Load","i1(obj,str)",…)
RT_CLASS_END()
RT_CLASS_BEGIN("Viper.Graphics3D.LightProbeGrid3D", G3dLightProbeGrid3D, "obj", G3dLightProbeGridNew)
    RT_PROP("ProbeCount","i64",get)
    RT_METHOD("Bake","void(obj,obj<Viper.Graphics3D.SceneGraph>)",…)
    RT_METHOD("Save","i1(obj,str)",…) RT_METHOD("Load","i1(obj,str)",…)
RT_CLASS_END()
```

Plus `SceneNode.SetStatic/GetStatic`, `Material3D.SetLightmap(pixels)` + `get_HasLightmap`, `World3D.setLightProbes(grid)`. Leaves unique. New files → source-health bumps; internal BVH header → `RuntimeSurfacePolicy.inc`; ADR `00xx-baked-lighting.md`.

## 6. Tests

- **Analytic parity (C unit):** single directional light, white box floor, direct-only bake — texel values match the SW-rendered lit floor within 2/255 (fail-before: no API).
- **Bounce sanity:** red wall beside white floor with 2 bounces ⇒ floor texels near the wall gain red (channel-ratio assert), farther texels less (monotonic falloff).
- **Determinism:** same scene + options bakes byte-identical atlases twice (fixed seeds).
- **Round-trip:** `.vlm`/`.vlpg` save→load→Apply renders identical to in-memory bake (SW capture compare).
- **Probe interpolation:** probe grid with a bright half and dark half — a dynamic sphere crossing the boundary shows monotonic ambient transition across frames.
- **Golden:** `baked_room` interior fixture SW baseline committed; Metal tolerance compare.
- **No-regression:** scenes without lightmaps/probes render bit-identical to before (SW).

## 7. Verification gates

Full build + ctest; SW goldens (new + existing bit-exact); Metal visual verification, GL/D3D11 waiver; `-L graphics3d`; `-L slow`; surface audits; bake determinism run twice in CTest.

## 8. Risks & constraints

- **Scope discipline:** v1 quality bar is "flat-chart, 2-bounce, deterministic" — resist chart-quality/seam-perfection rabbit holes; dilation + texel density solve the visible 90% of seams.
- **Bake time:** budget ~1 min for the fixture room at defaults; chunked `BakeStep` keeps the editor responsive; document texel/sample scaling.
- **8-bit atlas banding:** store with a 2× scale headroom + dither on write; HDR pages when available.
- **Importer TEXCOORD_1:** if glTF import drops UV1 today, that fix is step 1's hidden work — verify before sizing.
- Streamed cells + lightmaps compose (atlas is per-cell content like any texture), but probe grids are world-scoped v1 — one grid per world, sized to the hero interior; per-cell grids are v2.
