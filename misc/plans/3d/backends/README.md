# 3D Backend Feature Matrix — Verified Deep Audit (2026-03-28)

Two-pass audit: initial scan + deep verification reading actual shader source and draw paths.

## Feature Comparison: Software vs Metal vs OpenGL vs D3D11

### Vertex Processing

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Position input | ✅ | ✅ | ✅ | ✅ |
| Normal input | ✅ | ✅ | ✅ | ✅ |
| UV input | ✅ | ✅ | ✅ | ✅ |
| Color input | ✅ | ✅ | ✅ | ✅ |
| Tangent input | ✅ | ✅ | ✅ | ✅ |
| Bone indices (defined) | ✅ | ✅ | ✅ | ✅ |
| Bone weights (defined) | ✅ | ✅ | ✅ | ✅ |
| MVP transform | ✅ | ✅ | ✅ | ✅ |
| Normal matrix | ✅ | ✅ | 🐛 BUG: uses model matrix | 🐛 BUG: uses model matrix |
| Skeletal skinning | ✅ (CPU, pre-backend) | ❌ (inputs unused in shader) | ❌ (inputs unused in shader) | ❌ (inputs unused in shader) |
| Morph targets | ✅ (CPU, pre-backend) | ❌ | ❌ | ❌ |

### Texture Sampling

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Diffuse texture | ✅ perspective-correct | ⚠️ unlit path only | ❌ NONE | ❌ NONE |
| Normal map | ❌ (in draw cmd, unused) | ❌ (in draw cmd, unused) | ❌ | ❌ |
| Specular map | ❌ (in draw cmd, unused) | ❌ (in draw cmd, unused) | ❌ | ❌ |
| Emissive map | ✅ additive blend | ❌ (color only, no map) | ❌ | ❌ |
| Texture wrapping | ✅ clamp | ✅ repeat (hardcoded) | ❌ | ❌ |
| Texture filtering | ✅ nearest | ✅ bilinear | ❌ | ❌ |
| Texture cache | N/A | ❌ recreated every draw! | N/A | N/A |
| Sampler state | N/A | ✅ recreated every draw | ❌ | ❌ |

### Lighting

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Ambient light | ✅ | ✅ | ✅ | ✅ |
| Directional diffuse | ✅ | ✅ | ✅ | ✅ |
| Directional specular (Blinn-Phong) | ✅ | ✅ | ✅ | ✅ |
| Point light + attenuation | ✅ | ✅ | ✅ | ✅ |
| Spot light + cone | ✅ smoothstep | ❌ falls to ambient | ❌ falls to ambient | ❌ falls to ambient |
| Lighting model | Gouraud (per-vertex) | Per-pixel (fragment) | Per-pixel (fragment) | Per-pixel (fragment) |
| Max light slots | 8 | 8 | 8 | 8 |

### Material Properties

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Diffuse color | ✅ | ✅ | ✅ | ✅ |
| Specular color + shininess | ✅ | ✅ | ✅ | ✅ |
| Alpha/transparency | ✅ | ✅ | 🐛 BUG: uAlpha undeclared | ✅ |
| Emissive color | ✅ | ✅ | ✅ | ✅ |
| Unlit mode | ✅ | ✅ | ✅ | ✅ |
| Two-sided rendering | ✅ (backface_cull=0) | ✅ (cull mode toggle) | ✅ (cull mode toggle) | ❌ (always back-cull) |

### Rendering Features

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Depth testing | ✅ float Z-buffer | ✅ Depth32Float | ✅ GL_LESS | ✅ D3D11_COMPARISON_LESS |
| Alpha blending | ✅ src*a + dst*(1-a) | ✅ MTLBlend | ✅ GL_BLEND | ✅ D3D11 blend state |
| Transparency sorting | ✅ (Canvas3D two-pass) | ✅ (Canvas3D two-pass) | ✅ (Canvas3D two-pass) | ✅ (Canvas3D two-pass) |
| Depth write disabled for transparent | ✅ | ✅ | ✅ | ✅ |
| Backface culling | ✅ screen-space area | ✅ per-draw toggle | ✅ per-draw toggle | ❌ always on, no toggle |
| Wireframe mode | ✅ Bresenham lines | ❌ param ignored | ❌ param ignored | ❌ param ignored |
| Fog (linear distance) | ✅ per-pixel | ❌ | ❌ | ❌ |
| Shadow mapping | ❌ | ❌ | ❌ | ❌ |
| Render-to-texture | ✅ CPU color_buf | ✅ GPU→CPU readback | ❌ stub | ❌ stub |

### Advanced Features

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Instanced rendering | ❌ | ❌ | ❌ | ❌ |
| Terrain splat (per-pixel) | ❌ | ❌ | ❌ | ❌ |
| Post-processing | ✅ CPU (bloom, FXAA, etc.) | ❌ | ❌ | ❌ |
| VBO/buffer strategy | N/A (CPU arrays) | Per-draw MTL buffers | Per-draw VBO/IBO (wasteful) | Per-draw VB/IB (wasteful) |

---

### Per-Vertex Color

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Vertex color used in shading | ❌ ignores, uses diffuse_color only | ✅ passed to fragment | ✅ passed to fragment | ✅ passed to fragment |

---

## Bugs Found

| ID | Backend | Severity | Description | Line |
|----|---------|----------|-------------|------|
| OGL-1 | OpenGL | 🔴 CRITICAL | `uAlpha` used in GLSL fragment shader but never declared as uniform. Location is -1. Alpha undefined. | 388, 414 |
| OGL-2 | OpenGL | 🟡 HIGH | Normal matrix = model matrix (no inverse-transpose). Lighting wrong on scaled objects. | 653 |
| D3D-1 | D3D11 | 🟡 HIGH | Normal matrix = model matrix. Same bug as OGL-2. | 537 |
| D3D-2 | D3D11 | 🟡 HIGH | Backface culling + wireframe params void-cast. No per-draw render state toggle. | 497 |
| MTL-1 | Metal | 🟡 HIGH | Spot lights (type 3) fall through to ambient. No cone attenuation in shader. | 175-178 |
| MTL-2 | Metal | 🟡 HIGH | Diffuse texture only sampled in `unlit` path. Lit textured meshes render as solid color. | 161 |
| MTL-3 | Metal | 🟠 MEDIUM | Texture + sampler recreated every draw. No cache. | 614-659 |
| OGL-3 | OpenGL | 🟠 MEDIUM | VBO/IBO created+destroyed per draw. | 694-732 |
| D3D-3 | D3D11 | 🟠 MEDIUM | VBO/IBO created+destroyed per draw. | 508-598 |
| D3D-4 | D3D11 | 🟠 MEDIUM | ~10 unchecked HRESULTs on D3D11 creation calls. | various |
| MTL-2b | Metal | 🟡 HIGH | Diffuse texture only sampled in **unlit** code path. Lit textured meshes render as solid color. | 161 |
| SW-1 | Software | 🟠 MEDIUM | Ignores per-vertex colors — uses cmd->diffuse_color only. GPU backends use vertex colors. | compute_lighting |
| ALL-1 | All | 🟡 HIGH | Normal map and specular map pointers accepted in draw_cmd but **never sampled** by any backend. | — |
| ALL-2 | All GPU | 🟡 HIGH | Fog parameters passed to begin_frame but **ignored** by Metal, OpenGL, and D3D11. Fog only works in software. | begin_frame |

## Corrections from Previous Audit

| Claim | Correction |
|-------|-----------|
| "Software does per-pixel lighting" | **WRONG.** Gouraud (per-vertex). Colors interpolated per-pixel. |
| "Metal samples diffuse in lit + unlit" | **WRONG.** Only unlit path checks `hasTexture`. Lit textured meshes are solid color. |
| "OpenGL has working alpha" | **WRONG.** `uAlpha` uniform undeclared. Alpha is undefined. |
| "Normal/specular maps not used in SW+Metal" | **EXPANDED.** Not used in ANY backend. All 4 ignore them. |
| "Fog works on software" | **CONFIRMED.** GPU backends silently drop fog params in begin_frame. |
| "Vertex colors work everywhere" | **WRONG.** Software backend ignores vertex colors, uses diffuse_color only. |

## Verified Cross-Backend Facts

| Fact | Status |
|------|--------|
| All backends share same vtable signature | ✅ Confirmed |
| Transparency sorting done in Canvas3D (not backends) | ✅ Confirmed — two-pass opaque/transparent |
| Spot light cone data correctly passed to all backends | ✅ Confirmed — inner_cos/outer_cos in light params |
| Backend selection: Metal(macOS) → D3D11(Win) → OpenGL(Linux) → Software(fallback) | ✅ Confirmed |
| Canvas3D.New traps when graphics disabled; all other funcs silently return | ✅ Confirmed |

## Feature Parity Score (out of 34 features)

| Backend | Implemented | Partial/Buggy | Missing | Score |
|---------|------------|---------------|---------|-------|
| **Software** | 22 | 1 (no vertex colors) | 11 | 65% |
| **Metal** | 16 | 4 (spot, lit texture, texture cache, no fog) | 14 | 47% |
| **OpenGL** | 14 | 2 (uAlpha, normal matrix) | 18 | 41% |
| **D3D11** | 14 | 2 (normal matrix, no cull toggle) | 18 | 41% |

---

## Shared Integration Notes

The backend plans are intentionally split by renderer, but several implementation details are shared and should only be solved once:

- `vgfx3d_draw_cmd_t` in [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) does not yet carry skinning, morph-target, or terrain-splat payloads. Those feature families need one shared draw-command extension before any backend can consume them.
- Producer-side integration belongs in the feature owner, not in generic Canvas3D code:
  - Skinning: [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
  - Morph targets: [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)
  - Terrain splat: [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c)
  - Render-target binding: [`src/runtime/graphics/rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c)
  - PostFX ownership: [`src/runtime/graphics/rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c)
- `InstanceBatch3D` in [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) already bypasses Canvas3D's deferred queue and loops `submit_draw()` directly. A future instanced backend hook still helps, but it reduces per-instance backend/shader work, not deferred-queue overhead.
- Shadow mapping needs a shared pass-scheduling change in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c), because Canvas3D owns the deferred queue and currently replays it only once. Do not try to hide the whole shadow pass inside a backend `submit_draw()` call.
- GPU post-processing should not read `rt_postfx3d.c` private structs directly from a backend. Export a compact backend-facing snapshot/helper API from [`src/runtime/graphics/rt_postfx3d.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h) first.
- Keep existing CPU/software fallbacks until the equivalent GPU path is fully wired. Terrain splat, skinning, morph targets, and postfx already have correctness fallbacks that should remain available.

## Software Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [SW-01](sw-01-normal-map.md) | Normal map sampling (requires per-pixel lighting upgrade) | — | Large |
| [SW-02](sw-02-specular-map.md) | Specular map sampling | SW-01 | Small |
| [SW-03](sw-03-bilinear-filtering.md) | Bilinear texture filtering | — | Small |
| [SW-04](sw-04-two-sided.md) | Two-sided rendering | — | **ALREADY DONE** |
| [SW-05](sw-05-shadow-mapping.md) | Shadow mapping (shared pass scheduling + software depth pass) | — | Large |
| [SW-06](sw-06-instanced-rendering.md) | Software fallback path for instancing | — | Small |
| [SW-07](sw-07-terrain-splat.md) | Per-pixel terrain splatting | — | Medium |
| [SW-08](sw-08-vertex-colors.md) | Vertex color support | — | Small |

## Metal Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [MTL-01](mtl-01-lit-texture.md) | Fix diffuse texture in lit path | — | Small |
| [MTL-02](mtl-02-spot-lights.md) | Spot light cone attenuation | — | Small |
| [MTL-03](mtl-03-texture-cache.md) | Texture + sampler caching | — | Medium |
| [MTL-04](mtl-04-normal-map.md) | Normal map sampling (tangent space) | MTL-03 | Medium |
| [MTL-05](mtl-05-specular-map.md) | Specular map sampling | MTL-03 | Small |
| [MTL-06](mtl-06-emissive-map.md) | Emissive map sampling | MTL-03 | Small |
| [MTL-07](mtl-07-fog.md) | Linear distance fog | — | Small |
| [MTL-08](mtl-08-wireframe.md) | Wireframe mode | — | Trivial |
| [MTL-09](mtl-09-skinning.md) | GPU skeletal skinning (bone palette) | Shared draw-cmd extension | Large |
| [MTL-10](mtl-10-morph-targets.md) | GPU morph targets | Shared draw-cmd extension | Large |
| [MTL-11](mtl-11-post-processing.md) | GPU post-processing pipeline | — | Large |
| [MTL-12](mtl-12-shadow-mapping.md) | Shadow mapping (depth pass + comparison) | — | Large |
| [MTL-13](mtl-13-instanced-rendering.md) | Instanced rendering (instance_id) | Shared instanced backend hook | Medium |
| [MTL-14](mtl-14-terrain-splat.md) | Per-pixel terrain splatting (5 extra textures) | Shared draw-cmd extension, MTL-03 | Medium |

### Metal recommended execution order
1. **MTL-01** (lit texture) — critical bug fix, 5-line shader change
2. **MTL-08** (wireframe) — trivial, 2-line fix
3. **MTL-02** (spot lights) — small, adds shader branch
4. **MTL-07** (fog) — small, adds PerScene fields + shader fog
5. **MTL-03** (texture cache) — medium, eliminates per-draw allocation
6. **MTL-04** (normal map) — medium, adds texture slot + TBN math
7. **MTL-05** (specular map) — small, adds texture slot
8. **MTL-06** (emissive map) — small, adds texture slot
9. **MTL-09** (skinning) — large, shared draw payload + vertex shader bone transform
10. **MTL-10** (morph targets) — large, shared draw payload + vertex shader morph
11. **MTL-12** (shadow mapping) — large, shared pass scheduling + depth compare
12. **MTL-13** (instanced rendering) — medium, shared hook + `instance_id`
13. **MTL-14** (terrain splat) — medium, shared terrain payload + 5 texture slots
14. **MTL-11** (post-processing) — large, offscreen render + backend-facing PostFX snapshot

## D3D11 Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [D3D-01](d3d-01-diffuse-texture.md) | Diffuse texture sampling (SRV + sampler + HLSL) | — | Medium |
| [D3D-02](d3d-02-normal-matrix.md) | Fix normal matrix (inverse-transpose) | — | Small |
| [D3D-03](d3d-03-texture-cache.md) | Texture SRV caching | D3D-01 | Medium |
| [D3D-04](d3d-04-spot-lights.md) | Spot light cone attenuation | — | Small |
| [D3D-05](d3d-05-wireframe-cull.md) | Wireframe mode + backface culling toggle | — | Small |
| [D3D-06](d3d-06-fog.md) | Linear distance fog | — | Small |
| [D3D-07](d3d-07-normal-map.md) | Normal map sampling (tangent + TBN) | D3D-01, D3D-03 | Medium |
| [D3D-08](d3d-08-specular-emissive-maps.md) | Specular + emissive map sampling | D3D-01, D3D-03 | Small |
| [D3D-09](d3d-09-render-to-texture.md) | Render-to-texture (offscreen + readback) | — | Large |
| [D3D-10](d3d-10-skinning-morph.md) | GPU skeletal skinning + morph targets | D3D-01, Shared draw-cmd extension | Large |
| [D3D-11](d3d-11-post-processing.md) | GPU post-processing pipeline | D3D-09 | Large |
| [D3D-12](d3d-12-vbo-optimization.md) | Dynamic VBO/IBO (Map/Discard) | — | Medium |
| [D3D-13](d3d-13-hresult-checks.md) | HRESULT error checking | — | Small |
| [D3D-14](d3d-14-shadow-mapping.md) | Shadow mapping (depth pass + comparison) | D3D-01 | Large |
| [D3D-15](d3d-15-instanced-rendering.md) | Instanced rendering (DrawIndexedInstanced) | Shared instanced backend hook | Medium |
| [D3D-16](d3d-16-terrain-splat.md) | Per-pixel terrain splatting (5 extra SRVs) | D3D-01, D3D-03, Shared draw-cmd extension | Medium |

### D3D11 recommended execution order
1. **D3D-02** (normal matrix fix) — bug fix, small
2. **D3D-13** (HRESULT checks) — safety, small
3. **D3D-05** (wireframe + cull toggle) — small, 4 pre-created rasterizer states
4. **D3D-01** (diffuse texture) — critical missing feature, establishes SRV pattern
5. **D3D-04** (spot lights) — small shader branch
6. **D3D-06** (fog) — small, PerScene cbuffer extension
7. **D3D-03** (texture cache) — medium, prerequisite for maps
8. **D3D-12** (VBO optimization) — medium, eliminates per-draw allocation
9. **D3D-07** (normal map) — medium, tangent passthrough + TBN
10. **D3D-08** (specular + emissive maps) — small, texture slots t2/t3
11. **D3D-09** (render-to-texture) — large, offscreen + staging + readback
12. **D3D-10** (skinning + morph) — large, shared draw payload + VS modification
13. **D3D-14** (shadow mapping) — large, shared pass scheduling + comparison sampler
14. **D3D-15** (instanced rendering) — medium, shared hook + `DrawIndexedInstanced`
15. **D3D-16** (terrain splat) — medium, shared terrain payload + pixel shader blend
16. **D3D-11** (post-processing) — large, offscreen + backend-facing PostFX snapshot

## OpenGL Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [OGL-01](ogl-01-alpha-fix.md) | Fix uAlpha undeclared (CRITICAL) | — | Trivial (1 line) |
| [OGL-02](ogl-02-normal-matrix.md) | Fix normal matrix (inverse-transpose) | — | Small |
| [OGL-03](ogl-03-diffuse-texture.md) | Diffuse texture sampling (full GL texture pipeline) | — | Medium |
| [OGL-04](ogl-04-texture-cache.md) | Texture caching | OGL-03 | Medium |
| [OGL-05](ogl-05-spot-lights.md) | Spot light cone attenuation | — | Small |
| [OGL-06](ogl-06-wireframe.md) | Wireframe mode (glPolygonMode) | — | Trivial |
| [OGL-07](ogl-07-fog.md) | Linear distance fog | — | Small |
| [OGL-08](ogl-08-normal-map.md) | Normal map sampling (tangent + TBN) | OGL-03, OGL-04 | Medium |
| [OGL-09](ogl-09-specular-emissive-maps.md) | Specular + emissive map sampling | OGL-03, OGL-04 | Small |
| [OGL-10](ogl-10-render-to-texture.md) | Render-to-texture (FBO + readback) | — | Large |
| [OGL-11](ogl-11-skinning-morph.md) | GPU skeletal skinning + morph targets | OGL-03, Shared draw-cmd extension | Large |
| [OGL-12](ogl-12-shadow-mapping.md) | Shadow mapping (FBO depth + comparison) | OGL-10 | Large |

### OpenGL recommended execution order
1. **OGL-01** (alpha fix) — CRITICAL bug, 1-line shader fix
2. **OGL-02** (normal matrix) — bug fix, small
3. **OGL-06** (wireframe) — trivial, glPolygonMode
4. **OGL-03** (diffuse texture) — critical missing feature, establishes GL texture pipeline
5. **OGL-05** (spot lights) — small, shader branch + 2 uniform arrays
6. **OGL-07** (fog) — small, 4 uniforms + fragment fog
7. **OGL-04** (texture cache) — medium, prerequisite for maps
8. **OGL-08** (normal map) — medium, tangent varying + orthonormal TBN
9. **OGL-09** (specular + emissive maps) — small, texture units 2-3
10. **OGL-10** (render-to-texture) — large, FBO + readback
11. **OGL-11** (skinning + morph) — large, shared draw payload + shader variants
12. **OGL-12** (shadow mapping) — large, shared pass scheduling + `sampler2DShadow`

---

### SW recommended execution order
1. **SW-08** (vertex colors) — smallest, fixes parity with GPU backends
2. **SW-03** (bilinear filtering) — small, visible quality improvement
3. **SW-01** (normal maps) — large but foundational (upgrades to per-pixel lighting)
4. **SW-02** (specular maps) — small, builds on SW-01
5. **SW-07** (terrain splat) — medium, add backend path first and keep baked fallback until parity is preserved
6. **SW-05** (shadow mapping) — large, shared shadow-pass scheduling + software depth pass
7. **SW-06** (instanced rendering) — mostly documentation/correctness fallback unless a shared backend hook lands
