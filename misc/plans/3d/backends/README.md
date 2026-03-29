# 3D Backend Feature Matrix — Verified Deep Audit (2026-03-28)

Two-pass audit: initial scan + deep verification reading actual shader source and draw paths.

## Feature Comparison: Software vs Metal vs OpenGL vs D3D11

### Vertex Processing

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Position input | ✅ | ✅ | ✅ | ✅ |
| Normal input | ✅ | ✅ | ✅ | ✅ |
| UV input | ✅ | ✅ | ✅ | ✅ |
| Color input | ✅ | ✅ attribute passed, unused | ✅ multiplied into diffuse/alpha | ✅ attribute passed, unused |
| Tangent input | ✅ | ✅ | ✅ | ✅ |
| Bone indices (defined) | ✅ | ✅ | ✅ | ✅ |
| Bone weights (defined) | ✅ | ✅ | ✅ | ✅ |
| MVP transform | ✅ | ✅ | ✅ | ✅ |
| Normal matrix | ✅ | ✅ | ✅ inverse-transpose helper | 🐛 BUG: uses model matrix |
| Skeletal skinning | ✅ (CPU, pre-backend) | ✅ GPU vertex shader + shared producer bypass | ✅ GPU vertex shader + shared producer bypass | ❌ shader path missing |
| Morph targets | ✅ (CPU, pre-backend) | ✅ GPU vertex shader + shared producer payload | ✅ GPU vertex shader + shared producer payload | ❌ |

### Texture Sampling

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Diffuse texture | ✅ perspective-correct | ✅ lit + unlit paths | ✅ lit + unlit paths | ❌ NONE |
| Normal map | ✅ per-pixel TBN perturbation | ✅ TBN perturbation (slot 1) | ✅ TBN perturbation (slot 1) | ❌ |
| Specular map | ✅ per-pixel modulation | ✅ per-texel modulation (slot 2) | ✅ per-texel modulation (slot 2) | ❌ |
| Emissive map | ✅ additive map/sample | ✅ additive map sample (slot 3) | ✅ emissive color * map, added after lighting | ❌ |
| Texture wrapping | ✅ repeat (modulo) | ✅ repeat (shared sampler) | ✅ repeat | ❌ |
| Texture filtering | ✅ bilinear | ✅ bilinear (shared sampler) | ✅ linear | ❌ |
| Texture cache | N/A | ✅ per-frame cache by Pixels ptr | ✅ per-frame cache by Pixels ptr | ❌ |
| Sampler state | N/A | ✅ shared sampler | ✅ fixed repeat/clamp + linear state per texture type | ❌ |

### Lighting

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Ambient light | ✅ | ✅ | ✅ | ✅ |
| Directional diffuse | ✅ | ✅ | ✅ | ✅ |
| Directional specular (Blinn-Phong) | ✅ | ✅ | ✅ | ✅ |
| Point light + attenuation | ✅ | ✅ | ✅ | ✅ |
| Spot light + cone | ✅ smoothstep | ✅ smoothstep cone | ✅ smoothstep cone | ❌ falls to ambient |
| Lighting model | Gouraud + per-pixel normal-map path | Per-pixel | Per-pixel | Per-pixel |
| Max light slots | 8 | 8 | 8 | 8 |

### Material Properties

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Diffuse color | ✅ | ✅ | ✅ | ✅ |
| Specular color + shininess | ✅ | ✅ | ✅ | ✅ |
| Alpha/transparency | ✅ | ✅ | ✅ | ✅ |
| Emissive color | ✅ | ✅ | ✅ | ✅ |
| Unlit mode | ✅ | ✅ | ✅ | ✅ |
| Two-sided rendering | ✅ | ✅ | ✅ | ❌ toggle missing |

### Rendering Features

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Depth testing | ✅ float Z-buffer | ✅ Depth32Float | ✅ GL_LESS | ✅ D3D11_COMPARISON_LESS |
| Alpha blending | ✅ src*a + dst*(1-a) | ✅ | ✅ | ✅ |
| Transparency sorting | ✅ | ✅ | ✅ | ✅ |
| Depth write disabled for transparent | ✅ | ✅ | ✅ | ✅ |
| Backface culling | ✅ | ✅ | ✅ | ❌ toggle missing |
| Wireframe mode | ✅ | ✅ | ✅ | ❌ param ignored |
| Fog (linear distance) | ✅ | ✅ | ✅ | ❌ |
| Shadow mapping | ✅ | ✅ | ✅ | ❌ |
| Render-to-texture | ✅ | ✅ GPU offscreen + readback | ✅ GPU offscreen + readback | ❌ stub |

### Advanced Features

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Instanced rendering | ❌ | ⚠️ backend-side multi-draw hook, not true instancing | ✅ hardware instancing | ❌ |
| Terrain splat (per-pixel) | ✅ | ✅ | ✅ | ❌ |
| Post-processing | ✅ CPU | ⚠️ shader/pipeline pieces exist, shared flip handoff still needed | ✅ GPU fullscreen pass + shared flip handoff | ❌ |
| VBO/buffer strategy | N/A | Per-draw MTL buffers | ✅ persistent dynamic VBO/IBO/instance/morph buffers | ❌ per-draw VB/IB |

### Per-Vertex Color

| Feature | Software | Metal | OpenGL | D3D11 |
|---------|----------|-------|--------|-------|
| Vertex color used in shading | ✅ multiplied into diffuse | ❌ passed through, unused | ✅ multiplied into diffuse/alpha | ❌ passed through, unused |

---

## Bugs Found

| ID | Backend | Severity | Description |
|----|---------|----------|-------------|
| D3D-1 | D3D11 | 🟡 HIGH | Normal matrix = model matrix. |
| D3D-2 | D3D11 | 🟡 HIGH | Backface culling and wireframe params ignored. |
| D3D-3 | D3D11 | 🟠 MEDIUM | Per-draw VB/IB creation. |
| D3D-4 | D3D11 | 🟠 MEDIUM | Unchecked HRESULTs on several creation paths. |
| GPU-VC | Metal/D3D11 | 🟠 MEDIUM | Vertex color is forwarded from the vertex stage but not used in shading. |

---

## Shared Cross-Cutting Notes

Several backend plans depend on shared runtime infrastructure rather than backend-local work alone.

Already present:

- `vgfx3d_draw_cmd_t` carries texture, splat, bone palette, and morph payload fields.
- `InstanceBatch3D` already uses the optional `submit_draw_instanced()` backend hook when available.
- Canvas3D already schedules the shadow prepass through `shadow_begin` / `shadow_draw` / `shadow_end`.
- `vgfx3d_postfx_get_snapshot()` already exports a compact backend-facing PostFX snapshot.
- Render-target binding already flows through [`rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c).

Implemented shared prerequisites now available to every backend:

- GPU postfx presentation handoff in [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L1001) through `present_postfx`.
- Producer-side GPU skinning bypass in [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L934) with backend-name gating and CPU fallback.
- Producer-side morph payload packing in [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c#L251) plus draw-command propagation in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L664).
- GPU RTT ownership fix in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L761) so the CPU skybox path is not clobbered during GPU render-target readback.

Recently implemented shared prerequisites:

- `Material3D.env_map` / `reflectivity` now forward through `vgfx3d_draw_cmd_t`.
- The GPU morph producer now packs optional morph normal deltas.
- The backend vtable now exposes an optional `draw_skybox` hook.
- The GPU PostFX snapshot now includes SSAO, DOF, and motion-blur parameters.

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
| [MTL-01](mtl-01-lit-texture.md) | Fix diffuse texture in lit path | — | **DONE** |
| [MTL-02](mtl-02-spot-lights.md) | Spot light cone attenuation | — | **DONE** |
| [MTL-03](mtl-03-texture-cache.md) | Texture + sampler caching | — | **DONE** |
| [MTL-04](mtl-04-normal-map.md) | Normal map sampling (tangent space) | MTL-03 | **DONE** |
| [MTL-05](mtl-05-specular-map.md) | Specular map sampling | MTL-03 | **DONE** |
| [MTL-06](mtl-06-emissive-map.md) | Emissive map sampling | MTL-03 | **DONE** |
| [MTL-07](mtl-07-fog.md) | Linear distance fog | — | **DONE** |
| [MTL-08](mtl-08-wireframe.md) | Wireframe mode | — | **DONE** |
| [MTL-09](mtl-09-skinning.md) | GPU skeletal skinning (bone palette) | Shared producer bypass for true end-to-end GPU path | **BACKEND DONE** |
| [MTL-10](mtl-10-morph-targets.md) | GPU morph targets | Shared producer payload | **BACKEND DONE** |
| [MTL-11](mtl-11-post-processing.md) | GPU post-processing pipeline | Shared flip handoff | **PARTIAL** |
| [MTL-12](mtl-12-shadow-mapping.md) | Shadow mapping (depth pass + comparison) | — | **DONE** |
| [MTL-13](mtl-13-instanced-rendering.md) | Instanced rendering hook | Shared hook | **PARTIAL** |
| [MTL-14](mtl-14-terrain-splat.md) | Per-pixel terrain splatting (5 extra textures) | Shared terrain payload, MTL-03 | **DONE** |

### Metal recommended execution order

Metal is no longer the blocking backend for parity. Remaining work is primarily in shared GPU postfx presentation, true GPU skinning bypass, morph payload production, true hardware instancing if desired, and vertex-color support.

## D3D11 Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [D3D-01](d3d-01-diffuse-texture.md) | Diffuse texture pipeline + vertex color modulation | — | Medium |
| [D3D-02](d3d-02-normal-matrix.md) | True inverse-transpose normal matrix | — | Small |
| [D3D-03](d3d-03-texture-cache.md) | Per-frame texture cache | D3D-01 | Medium |
| [D3D-04](d3d-04-spot-lights.md) | Spot light cone attenuation | — | Small |
| [D3D-05](d3d-05-wireframe-cull.md) | Wireframe mode + cull toggle | — | Small |
| [D3D-06](d3d-06-fog.md) | Linear distance fog | — | Small |
| [D3D-07](d3d-07-normal-map.md) | Normal map sampling | D3D-01, D3D-03 | Medium |
| [D3D-08](d3d-08-specular-emissive-maps.md) | Specular + emissive maps | D3D-01, D3D-03 | Small |
| [D3D-09](d3d-09-render-to-texture.md) | Render-to-texture + readback | Shared render-target path, skybox interaction fix | Large |
| [D3D-10](d3d-10-skinning-morph.md) | GPU skinning + morph consumption | Shared producer work | Large |
| [D3D-11](d3d-11-post-processing.md) | GPU post-processing | D3D-09, shared flip handoff | Large |
| [D3D-12](d3d-12-vbo-optimization.md) | Persistent dynamic VB/IB | — | Medium |
| [D3D-13](d3d-13-hresult-checks.md) | HRESULT / failure-path rigor | — | Small |
| [D3D-14](d3d-14-shadow-mapping.md) | Shadow mapping | D3D-09 | Large |
| [D3D-15](d3d-15-instanced-rendering.md) | True hardware instancing | Shared hook, D3D-12 recommended | Medium |
| [D3D-16](d3d-16-terrain-splat.md) | Terrain splatting | D3D-01, D3D-03, shared terrain payload | Medium |

### D3D11 recommended execution order

1. **D3D-02** — fix the normal-matrix bug first
2. **D3D-13** — harden failure paths before more D3D resource creation lands
3. **D3D-05** — cheap render-state parity
4. **D3D-01** — establish texture upload/bind path and fix missing vertex-color modulation
5. **D3D-04** — complete light-type parity
6. **D3D-06** — add scene fog
7. **D3D-03** — make texture-heavy draws practical
8. **D3D-08** — specular/emissive build cheaply on the same SRV path
9. **D3D-07** — normal maps build on the same texture path
10. **D3D-12** — remove per-draw buffer churn before larger features pile on
11. **D3D-09** — offscreen render target foundation
12. **D3D-14** — shadow pass builds cleanly on RTT ownership
13. **D3D-16** — terrain splat is straightforward after texture/cache work
14. **D3D-15** — true instancing once buffer management is cleaned up
15. **D3D-10** — backend shader work plus producer-side skinning/morph prerequisites
16. **D3D-11** — last because it needs both RTT and shared flip ownership changes

## OpenGL Backend Implementation Plans

| Plan | Feature | Depends On | Effort |
|------|---------|-----------|--------|
| [OGL-01](ogl-01-alpha-fix.md) | Fix missing `uAlpha` declaration | — | Trivial |
| [OGL-02](ogl-02-normal-matrix.md) | True inverse-transpose normal matrix | — | Small |
| [OGL-03](ogl-03-diffuse-texture.md) | Diffuse texture pipeline + vertex color modulation | — | Medium |
| [OGL-04](ogl-04-texture-cache.md) | Per-frame texture cache | OGL-03 | Medium |
| [OGL-05](ogl-05-spot-lights.md) | Spot light cone attenuation | — | Small |
| [OGL-06](ogl-06-wireframe.md) | Wireframe mode via `glPolygonMode` | — | Trivial |
| [OGL-07](ogl-07-fog.md) | Linear distance fog | OGL-01 | Small |
| [OGL-08](ogl-08-normal-map.md) | Normal map sampling | OGL-03, OGL-04 | Medium |
| [OGL-09](ogl-09-specular-emissive-maps.md) | Specular + emissive maps | OGL-03, OGL-04 | Small |
| [OGL-10](ogl-10-render-to-texture.md) | Render-to-texture + readback | Shared render-target path, skybox interaction fix | Large |
| [OGL-11](ogl-11-skinning-morph.md) | GPU skinning + morph consumption | Shared producer work | Large |
| [OGL-12](ogl-12-shadow-mapping.md) | Shadow mapping | OGL-10 | Large |
| [OGL-13](ogl-13-post-processing.md) | GPU post-processing | OGL-10, shared flip handoff | Large |
| [OGL-14](ogl-14-vbo-optimization.md) | Persistent dynamic VBO/IBO | — | Medium |
| [OGL-15](ogl-15-instanced-rendering.md) | True hardware instancing | Shared hook, OGL-14 recommended | Medium |
| [OGL-16](ogl-16-terrain-splat.md) | Terrain splatting | OGL-03, OGL-04, shared terrain payload | Medium |
| [OGL-17](ogl-17-cubemap-skybox.md) | Backend-owned cubemap skybox rendering | Shared skybox submission hook, OGL-04 recommended | **DONE** |
| [OGL-18](ogl-18-env-reflections.md) | Material environment reflections | OGL-17, shared draw-command payload for env-map data | **DONE** |
| [OGL-19](ogl-19-morph-normal-deltas.md) | GPU morph normal-delta parity | OGL-11, shared morph-normal payload | **DONE** |
| [OGL-20](ogl-20-advanced-postfx.md) | SSAO / DOF / motion blur on GPU | OGL-10, OGL-13, shared advanced PostFX snapshot | **DONE** |

### OpenGL status

OGL-01 through OGL-20 are now implemented in [`vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c) plus the shared runtime work in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c), [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c), [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c), and [`rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c).

The advanced postfx path now uses a real velocity-buffer motion-blur implementation. Shared runtime history in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c), [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c), [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c), and [`rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) now supplies previous-frame object state so OpenGL can emit per-pixel motion vectors instead of falling back to camera-only reprojection.

### OpenGL recommended execution order

1. **OGL-01** — fix the critical alpha bug first
2. **OGL-02** — correct lighting on scaled geometry
3. **OGL-06** — cheap render-state parity
4. **OGL-03** — establish texture upload/bind path and fix missing vertex-color modulation
5. **OGL-05** — complete light-type parity
6. **OGL-07** — add scene fog
7. **OGL-04** — make texture-heavy draws practical
8. **OGL-09** — specular/emissive are cheap once texture units exist
9. **OGL-08** — normal maps build on the same texture path
10. **OGL-14** — remove per-draw buffer churn before larger features pile on
11. **OGL-10** — offscreen render target foundation
12. **OGL-12** — shadow pass builds cleanly on RTT/FBO ownership
13. **OGL-16** — terrain splat is straightforward after texture/cache work
14. **OGL-15** — real instancing once buffer management is cleaned up
15. **OGL-11** — backend shader work plus producer-side skinning/morph prerequisites
16. **OGL-13** — last because it needs both RTT and shared flip ownership changes

### SW recommended execution order

1. **SW-08**
2. **SW-03**
3. **SW-01**
4. **SW-02**
5. **SW-07**
6. **SW-05**
7. **SW-06**
