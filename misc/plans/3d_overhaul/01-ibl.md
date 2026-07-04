# Plan 01 — True Image-Based Lighting (IBL)

## 1. Objective & scope

Replace the current "fake IBL" — flat ambient color + roughness-to-mip cubemap lookup — with real image-based lighting: a GGX-prefiltered specular environment mip chain, SH-9 diffuse irradiance, and a split-sum specular BRDF term. This is the single largest visual-quality delta available: it's what makes PBR materials read as "Unity-grade" instead of flat.

**In scope:** CPU prefiltering of environment cubemaps at load/set time; SH-9 irradiance projection; analytic split-sum (Karis approximation — no BRDF LUT texture); shader changes in all 4 backends; public intensity/toggle controls; golden probes.
**Out of scope:** light probes / probe volumes, HDR environment import (BC6H — see plan 03 optional item), realtime reflection probes, lightmapping.

**Zero external dependencies — absolute.** No image-processing, math, or IBL-baking libraries (no cmft/IBLBaker-style tools, no vendored reference shaders). The SH projection, GGX prefilter, and split-sum approximation are implemented from scratch in-tree from the published math, exactly like the existing from-scratch Cook-Torrance shading.

## 2. Current state (verified anchors)

- Ambient is a flat color: `canvas->ambient[3]` (`render/rt_canvas3d_internal.h:826-831`) → passed as the `ambient` parameter of `submit_draw(ctx, win, cmd, lights, light_count, ambient, wireframe, backface_cull)` (`backend/vgfx3d_backend.h:288-393`) → shader uniform `uAmbientColor` (GLSL `backend/vgfx3d_backend_opengl_shaders.inc:165`; HLSL `ambientColor` in cbuffer `PerScene`, `_d3d11_shaders.inc:39-53`; MSL `PerScene` struct, `vgfx3d_backend_metal.m:283+`).
- Env reflection is a mip-LOD hack: shader `envSample` does `textureLod(uEnvMap, R, roughness * uEnvMaxLod)` (`opengl_shaders.inc:262-265`), applied at `:545-548`, blended by `uReflectivity`. `uEnvMaxLod` comes from `gl_cubemap_max_lod` (`_opengl_texture.inc:946`).
- Cubemaps: `rt_cubemap3d` = 6 `Pixels` faces + `face_size` + `cache_identity` (`rt_canvas3d_internal.h:543-548`), created/validated in `render/rt_cubemap3d.c` (`rt_cubemap3d_new`, `cubemap_faces_valid:77`). **There is no cubemap upload vtable hook** — backends receive the object pointer via `cmd->env_map` and `draw_skybox(ctx, cubemap)` (`backend.h:324`) and lazily upload into private LRU caches keyed by `cache_identity` (GL: `_opengl_texture.inc:1006-1073`, mips via `GenerateMipmap:1033`, LRU prune `:176`).
- A CPU roughness blur already exists for the software path: `rt_cubemap_sample_roughness` (`rt_cubemap3d.c:560+`, declared `rt_canvas3d_internal.h:1087`) — a fake, not GGX.
- PBR shading is real Cook-Torrance (GGX `distributionGGX:236`, Smith `:242-248`, Fresnel `:250`, `F0 = mix(0.04, baseColor, metallic)` `:479`, `kD = (1-kS)(1-metallic)` `:485`) — the direct-lighting half is correct; only the ambient/environment half is fake.
- Software rasterizer mirrors the shading in `backend/vgfx3d_backend_sw_raster.inc`.

## 3. Design

### 3.1 Data: prefiltered environment on `rt_cubemap3d`

Extend `rt_cubemap3d` (keep POD layout; fields appended):

```c
/* IBL data, computed lazily by rt_cubemap3d_ensure_ibl() */
float sh_irradiance[9][3];      /* SH-9 RGB irradiance coefficients            */
void **prefiltered_mips;        /* [prefiltered_mip_count] Pixels* per face-strip
                                   OR per-face arrays — mirror the existing
                                   6-face storage: void *prefiltered[MIPS][6]   */
int32_t prefiltered_mip_count;  /* e.g. 5 levels: rough 0.0,0.25,0.5,0.75,1.0  */
int8_t ibl_ready;
uint64_t ibl_revision;          /* bump cache_identity so backend caches re-upload */
```

New functions in `render/rt_cubemap3d.c`:
- `void rt_cubemap3d_ensure_ibl(rt_cubemap3d *cm)` — idempotent; computes SH + prefiltered chain on first use. Called from the canvas when a cubemap is set as skybox/env (see 3.3), NOT in the render loop.
- `static void cubemap_project_sh9(const rt_cubemap3d *cm, float out_sh[9][3])` — cosine-convolved SH-9 projection over all 6 faces (standard per-texel solid-angle weighting).
- `static void cubemap_prefilter_ggx_level(const rt_cubemap3d *cm, float roughness, int out_size, void *out_faces[6])` — GGX importance sampling (fixed sample count per level, e.g. 64/128/256 scaling with roughness; deterministic Hammersley sequence — **no `rand()`**, determinism rule).
- Prefilter runs on the existing runtime thread pool (`threads/rt_threadpool.c`) split per face×level, with a synchronous fallback; results are committed before first draw use (ensure-before-use, same model as the glTF preload commit queue).

Sizing: level 0 = min(face_size, 128); halving per level; 5 levels. 128² is plenty for glossy reflections at this engine's target and keeps prefilter cost ~tens of ms.

### 3.2 Shader changes (×4 sources)

New scene-level uniforms (added next to `uAmbientColor` in all four backends — GLSL uniforms, HLSL `PerScene` cbuffer members, MSL `PerScene` struct fields, SW raster context fields):
- `uShIrradiance[9]` (vec3[9]) — SH-9 coefficients for the active environment; all zeros = feature off.
- `uIblEnabled` (int), `uIblIntensity` (float, default 1.0), `uEnvSpecularMips` (float — prefiltered chain max LOD).

Fragment ambient term (replaces the flat-ambient + envSample blend at `opengl_shaders.inc:435` and `:545-548`) when `uWorkflow != 0 && uIblEnabled != 0`:

```glsl
vec3 irradiance = evalSH9(uShIrradiance, N);                 // new fn
vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);      // new fn (roughness-aware)
vec3 kD = (1.0 - F) * (1.0 - metallic);
vec3 diffuseIBL  = kD * irradiance * albedo;
vec3 prefiltered = textureLod(uEnvMap, R, roughness * uEnvSpecularMips).rgb;
vec2 ab = envBRDFApprox(roughness, NdotV);                    // Karis analytic split-sum
vec3 specularIBL = prefiltered * (F0 * ab.x + ab.y);
ambientTerm = (diffuseIBL + specularIBL) * ao * uIblIntensity;
```

- `envBRDFApprox` is the well-known Karis mobile approximation (4 MADs) — avoids shipping/generating a BRDF LUT texture and keeps the SW-raster port trivial.
- When `uIblEnabled == 0` (no env cubemap set), the existing `uAmbientColor` flat path remains — full backward compatibility; existing golden baselines for scenes without env maps must not change.
- Blinn-Phong workflow (`uWorkflow == 0`) keeps its current path untouched.
- The prefiltered chain replaces `GenerateMipmap` for env cubemaps at upload: backends detect `ibl_ready` and upload `prefiltered_mips` levels explicitly (GL: `TexImage2D` per level per face, MIN_FILTER stays `GL_LINEAR_MIPMAP_LINEAR`; Metal: `MTLTexture` with explicit mip uploads; D3D11: `D3D11_SUBRESOURCE_DATA` per mip). Skybox rendering keeps sampling level 0.

### 3.3 Plumbing

- SH coefficients travel like ambient does: add `float sh_irradiance[27]` + `ibl` flags to `vgfx3d_camera_params_t` (`backend.h:218-232`) so they upload once in `begin_frame` (aligned with plan 04's direction), sourced from the canvas skybox/env cubemap. Per-material `cmd->env_map` overrides still use the same global SH (documented limitation; per-material SH is out of scope).
- Canvas triggers `rt_cubemap3d_ensure_ibl` in `rt_canvas3d_set_skybox` and in `rt_material3d_set_env_map` handlers.
- Software backend: `evalSH9` + `envBRDFApprox` ported into `_sw_raster.inc`; prefiltered sampling reuses the existing per-mip `Pixels` fallbacks in place of `rt_cubemap_sample_roughness` (which is then deleted or kept only as the no-IBL fallback).

## 4. Implementation steps

1. `rt_cubemap3d` struct + `cubemap_project_sh9` + unit test (analytic check: constant-color cubemap → DC term only; directional gradient → correct linear band signs).
2. `cubemap_prefilter_ggx_level` + `rt_cubemap3d_ensure_ibl` (threadpool + sync fallback) + unit test (constant cubemap → all levels constant; roughness 0 level ≈ source).
3. Plumb SH/flags through `vgfx3d_camera_params_t` and canvas set-skybox/env paths.
4. GLSL + MSL + SW raster shader changes (`evalSH9`, `fresnelSchlickRoughness`, `envBRDFApprox`, new ambient term); HLSL written in the same commit (compiled/verified later on Windows — waiver).
5. Backend env-cubemap upload switch: explicit prefiltered mip upload when `ibl_ready` (GL/Metal/D3D11 texture caches; keyed by `ibl_revision`).
6. Public API knobs (§5) + docs.
7. Golden probe: metallic×roughness sphere grid (5×5) under a sky cubemap, rendered Metal + software; add to `examples/3d/baselines/`.

## 5. Public API changes (runtime.def)

- `Viper.Graphics3D.Canvas3D`: `RT_PROP("IblEnabled","i1", get/set)`, `RT_PROP("IblIntensity","f64", get/set)` (handlers `rt_canvas3d_get/set_ibl_enabled`, `_ibl_intensity`).
- `Viper.Game3D.World3D`: forwarders `RT_PROP("IblIntensity","f64", ...)`; `Environment3D` presets (`rt_game3d_presets.c`) updated to enable IBL when they install a sky.
- No new classes; no new class IDs. Update `docs/viperlib/graphics/rendering3d.md` (lighting section) + `game3d.md` (environment section).

## 6. Tests

- Unit: SH projection (constant + gradient cubemaps, Given/When/Then), GGX prefilter invariants, `ensure_ibl` idempotency + revision bump, no-env fallback keeps flat ambient byte-identical (existing probe unchanged).
- Golden: `ibl_sphere_grid_software.png` + Metal capture — fails before (flat grid), passes after (Fresnel rim + roughness gradient).
- Perf sanity: `ensure_ibl` on a 256² cubemap completes < 250 ms on the dev machine (logged, not asserted).

## 7. Verification gates

- `ctest -L graphics3d -L canvas3d` + full suite + `-L slow`.
- Golden probes on Metal + software; GL/D3D11 code-complete with Windows/Linux waiver recorded.
- Existing baselines without env maps unchanged (regression guard for the fallback path).

## 8. Risks & constraints

- **Determinism:** Hammersley sequence + fixed sample counts; threadpool split must not change summation order per texel (accumulate per-texel locally, no shared accumulators).
- **Four-source drift:** implement GLSL first, port mechanically; SW raster is the reference for goldens.
- **LDR env sources:** until BC6H/HDR import exists (plan 03 optional), environments are LDR — specular highlights clamp. Acceptable v1; note in docs.
- **Cubemap cache invalidation:** backends key caches on `cache_identity`; bump on `ensure_ibl` completion or stale mips will render.
