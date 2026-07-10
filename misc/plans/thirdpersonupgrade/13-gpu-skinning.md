# Plan 13 — GPU Vertex-Shader Skinning

## 1. Objective & scope

Move skeletal skinning off the CPU on GPU backends. Today every skinned draw transforms vertices on the CPU (`vgfx3d_skin_vertices`) and uploads the result — the per-frame cost scales with vertex count × character count and caps town-scene NPC counts. Add a `"gpu-skinning"` backend capability: upload the bone palette, skin in the vertex shader, keep the CPU path as the software-backend correctness baseline and the fallback for unsupported payloads.

**In scope:** (a) palette upload path (Metal/D3D11/GL); (b) vertex-shader skinning incl. morph-then-skin ordering; (c) capability gating + automatic routing; (d) skinned-instancing GPU path; (e) parity probes.
**Out of scope:** compute-shader skinning, GPU morph targets beyond the existing packed-delta application point, changing `VGFX3D_MAX_BONES`.

**Zero external dependencies — absolute.** Standard linear-blend skinning in-shader.

## 2. Current state (verified anchors)

- **CPU skinning:** `vgfx3d_skinning.h` — "CPU-side vertex skinning — transforms vertices by a bone palette. Each vertex has up to 4 bone indices + weights (from `vgfx3d_vertex_t`)"; `vgfx3d_skin_vertices(_extra)` with per-thread scratch (`vgfx3d_skinning_scratch.h`). Draws route through `rt_canvas3d_draw_mesh_skinned` (`rt_skeleton3d.h`) and `rt_canvas3d_draw_instanced_skinned` (`rt_instbatch3d.h:43`).
- **Palette:** 256-slot draw palette (`VGFX3D_MAX_BONES 256`, `rt_skeleton3d_internal.h:21`); rigs exceeding it are remapped per-mesh via `rt_mesh3d::bone_map` (`:22-24` comment). Final palettes from `rt_anim_controller3d_get_final_palette_data`; dual palettes (previous frame) exist for motion vectors (`get_previous_palette_data`).
- **Worker mitigation exists but doesn't remove the cost:** animator updates parallelize over the world pool (`game3d.md` §World3D worker notes); skinning still runs per draw.
- **Backend shader files:** GLSL `vgfx3d_backend_opengl_shaders.inc`, HLSL `vgfx3d_backend_d3d11_shaders.inc`, MSL `vgfx3d_backend_metal_shaders.inc`, SW raster `vgfx3d_backend_sw_raster.inc`; capability strings via `BackendSupports` (`rendering3d.md` §Lighting Helpers pattern).
- **Vertex layout:** `vgfx3d_vertex_t` already carries 4 bone indices + weights per vertex (skinning header), so GPU buffers can include them without format invention; second influence set (`JOINTS_1/WEIGHTS_1`) is folded at import per `game3d.md` asset notes.
- **Motion vectors/TAA:** previous-frame palette feeds motion — GPU path must upload both palettes when motion blur/TAA is active.

## 3. Design

### 3.1 Routing and capability

`BackendSupports("gpu-skinning")`: true on Metal/D3D11/GL production backends, false on SW (SW stays the reference: bit-exact CPU baseline). A skinned draw routes GPU when: capability true, bone count ≤ 256, ≤ 4 influences (always true post-import), and no CPU-only debug flag (`Canvas3D.SetForceCpuSkinning(bool)` for bisection). Otherwise CPU path unchanged.

### 3.2 Palette upload

Per skinned draw: upload `bone_count × 3×4` float rows (row-major, affine — same packing the CPU path multiplies) for current palette, plus previous palette when motion output is enabled. Metal: `setVertexBytes`/ring buffer; D3D11: dynamic constant buffer (two 256×3 float4 arrays = 12 KB, within cbuffer limits) or structured buffer SRV; GL: UBO (std140, 256×3 vec4) — GL3.3 minimum UBO size covers it. Upload counted in the existing `TextureUploadBytes`-style telemetry? No — new `SkinningUploadBytes` counter (palette uploads are not textures; keep telemetry honest).

### 3.3 Shader change (all four sources)

Vertex stage: when the draw's skinning flag is set, `pos/normal/tangent = Σ w_i × (palette[j_i] × v)`; morph deltas apply **before** skinning (matching the CPU order: morph → skin, per the morphed-draw entry point `rt_canvas3d_draw_mesh_morphed`). SW raster keeps calling `vgfx3d_skin_vertices` — its "shader" is the existing CPU path (no change, baseline preserved). Motion-vector output uses the previous palette for the previous-position stream.

### 3.4 Skinned instancing

`draw_instanced_skinned` currently CPU-skins per instance set; GPU path uploads one palette per batch entry (shared-animation crowds: one palette, N transforms — the common crowd case) — per-instance distinct palettes stay CPU in v1 (documented; crowd batches share `AnimPlayer3D` players per the instancing API).

### 3.5 Buffers

Skinned meshes on GPU backends retain static vertex buffers with bone indices/weights (today's per-frame skinned vertex re-upload disappears — that's the win). Mesh dirty/regeneration paths (morph target payload generation counters) invalidate correctly via the existing mesh revision plumbing.

## 4. Implementation steps

1. Capability key + routing skeleton + `SetForceCpuSkinning`; no behavior change (capability false everywhere).
2. Metal: palette upload + MSL skinning + static skinned buffers; local visual parity vs SW (tolerance golden, walk_min + skinned agent fixture).
3. GLSL + HLSL ports (implement now, verify on Linux/Windows by hand later — record the standard waiver).
4. Morph-then-skin ordering + motion-vector previous palette.
5. Skinned-instancing shared-palette GPU path.
6. Telemetry (`SkinningUploadBytes`, `GpuSkinnedDrawCount`) + docs (`rendering3d.md` §Skeletal Animation note, capability table) + ADR.
7. Perf evidence: N-character crowd scene frame time CPU vs GPU path on Metal (harness numbers in the plan's landing notes).

## 5. Public API changes (runtime.def)

Minimal — this is an internal pipeline feature:

```
Canvas3D: RT_METHOD("SetForceCpuSkinning","void(obj,i1)"), RT_PROP("GpuSkinnedDrawCount","i64",get),
          RT_PROP("SkinningUploadBytes","i64",get)
```

`BackendSupports("gpu-skinning")` string documented. No new classes. ADR `00xx-gpu-skinning.md` (backend contract addition).

## 6. Tests

- **Parity (headline):** Given the skinned glTF agent fixture mid-crossfade — When rendered SW (CPU) vs Metal (GPU) — Then final frames match within the established GPU-tolerance comparator (existing GPU smoke pattern); pose sampled at 3 animation times (fail-before: GPU path absent ⇒ test skips, capability false).
- **Morph+skin:** morphed skinned fixture parity (ordering bug would show immediately).
- **Fallback:** `SetForceCpuSkinning(true)` on Metal reproduces the SW-parity result byte-for-byte against its own CPU run; >256-bone remapped rig routes CPU (assert `GpuSkinnedDrawCount == 0`).
- **Instanced:** 64-instance shared-palette crowd GPU vs CPU tolerance parity; per-instance-palette batch stays CPU (counter assert).
- **Perf:** crowd probe records ≥ 2× frame-time improvement at 32 characters on Metal (recorded, not asserted — perf numbers are evidence, not gates).
- **Determinism:** simulation untouched (skinning is render-only); standard suites green; SW captures bit-identical before/after.

## 7. Verification gates

Full build + ctest; `test_rt_canvas3d*` lanes; GPU opt-in smoke on Metal; SW goldens unchanged (bit-exact — CPU path untouched); platform lint (backend files are approved adapter layers); `-L slow`; waiver note for GL/D3D11 hand verification.

## 8. Risks & constraints

- **Four-source drift:** the skinning function must be scalar-identical across GLSL/HLSL/MSL — write it once in a shared comment block and port mechanically; parity tests catch drift on capable machines.
- **Palette row packing** must match the CPU multiply exactly (row-major 3×4 affine) — a transpose bug passes casual visual checks; the 3-time-sample parity test is designed to catch it.
- **NL3-033 policy:** software visual correctness stays the baseline — never let the GPU path redefine expected output; tolerance comparisons only.
- **UBO size on old GL:** 12 KB × 2 palettes fits the GL3.3 minimum (16 KB) only barely when motion is on — split into two UBO bindings.
