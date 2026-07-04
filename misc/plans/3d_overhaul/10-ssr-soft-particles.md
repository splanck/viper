# Plan 10 — Screen-Space Reflections + Soft Particles (+ Shared In-Pass Depth Access)

> **Status (2026-07-04): IMPLEMENTED (SSR runs full-res in the postfx chain; §3.1's
> optional opaque_color_tex was not needed).**
>
> - **Shared depth snapshot (§3.1)**: new optional vtable hook
>   `resolve_opaque_targets(ctx)` called at the opaque→transparent seam in
>   `canvas3d_render_main_pass` (skipped when no blend draws follow). SW = memcpy
>   of the active zbuf; Metal = end scene encoder → blit depth → re-begin with
>   load-existing color+depth; GL = `glBlitFramebuffer` depth into a snapshot
>   FBO (loader gained `BlitFramebuffer` + read/draw-framebuffer enums); D3D11 =
>   `CopyResource` into an R32_TYPELESS texture (t16 unbound first). Camera
>   params carry `znear/zfar` for shader-side linearization (Metal/D3D11 ride
>   `clusterParams.zw`; GL sets `uClusterParams` per draw now).
> - **Soft particles (§3.2)**: draw cmd + material gained `soft_particle_fade`;
>   the fade lane rides `pbrScalars1.z` (forced 0 while no snapshot exists, so a
>   dummy/unbound snapshot can never zero particle alpha) and applies only to
>   blend-mode fragments. GL aliases the snapshot onto the splat-control texture
>   unit (16-unit budget; fade force-disabled for splat draws so the aliasing
>   can't misread). `Particles3D.SetSoftness(distance)`; the four `Effects3D`
>   presets default softness ≈ start size. Capability `"soft-particles"` =
>   backends with the snapshot hook (incl. software).
> - **SSR (§3.3)**: mask = motion target's **alpha** channel (plan's B-channel
>   assumption was stale — B already carries the Plan-05 object-history flag;
>   alpha was the free lane, and motion clears now use alpha 0). Mask value =
>   `reflectivity` (or 0.5 when the material has none) for `ssr_enabled`
>   materials via `vgfx3d_draw_cmd_ssr_mask`. The SSR pass slots into the postfx
>   chain exactly like TAA's pre-resolve (own target, then the shared pass
>   copies forward): from-scratch linear march (≤48 steps, IGN-jittered start,
>   geometric growth) + 4-step binary refinement + thickness rejection +
>   screen-edge fade + cheap grazing-angle fresnel; misses keep the source pixel
>   (its env-map term is the fallback). Runs **full-res** (deviation: half-res
>   would add a dedicated downsample/upsample pair; measured cost on the live
>   probe is ~0.4 ms/frame at 160×120-window scale — revisit if a real scene
>   needs it) and **in chain order** (grouped with SSAO after tonemap, matching
>   the engine's existing screen-space effect placement, not §3.3's pre-tonemap
>   note). `PostFX3D.AddSSR(intensity, maxRoughness)`; `Material3D.SsrEnabled`
>   prop; `Water3D` opts in automatically; CINEMATIC quality adds SSR when
>   `BackendSupports("ssr")` (GPU postfx backends).
> - **Found+fixed on the way**: the SW unlit vertex path dropped `cmd->alpha`
>   (GPU shaders multiply it in) — `Material3D.SetAlpha` was a no-op for unlit
>   software draws; particles masked it by baking alpha into vertex colors.
> - **Tests**: SW end-to-end soft-particle pixel test (hard vs faded vs
>   empty-background renders through a RenderTarget3D); SSR chain-export/
>   rejection/mask unit test (255/255 canvas3d suite); live Metal proof in
>   `g3d_openworld_slice_gpu_smoke` (`SSR_SOFT_PARTICLES: backend=metal draws=3
>   three_frames_us=1320` — chain binds, LastError empty, particles + SsrEnabled
>   floor render, final-frame readback OK).

## 1. Objective & scope

Two of the most visible "engine vs toy" tells: water and glossy floors reflect only a static environment cubemap (no scene reflections), and particles hard-clip where their quads intersect geometry (no depth fade). Both need the same missing capability — **scene depth readable during the transparent/composite stages** — so this plan builds that once and ships both features on it.

**In scope:** post-opaque depth availability for in-pass sampling; half-res roughness-aware SSR composited into reflective materials + `Water3D`; soft particles (depth-fade in the particle path). **Out of scope:** planar reflections, refraction, SSR on transparent surfaces, particle lighting.

**Zero external dependencies:** ray-march, depth-fade, and composite shaders are from-scratch in the four in-tree backends + CPU fallback decisions below; no rendering libraries or borrowed shader implementations.

## 2. Current state (verified anchors)

- **Depth is not readable in-pass:** during the main pass, depth is the live attachment (`scene_depth_tex`, DEPTH_COMPONENT32F on GL — `_opengl_targets.inc:55-117`); it is bound for reading only in the postfx shader (`uSceneDepthTex`, `_opengl_shaders.inc:770`, consumed by SSAO/DOF `sampleDepth/reconstructWorld:802-808`). Sampling a texture that is simultaneously the depth attachment is undefined — a copy or pass split is required.
- Frame order (`render/rt_canvas3d_render_pass.inc:305-320`): skybox → shadow pass → main pass (frustum cull `:230` → opaque state-sorted `:276` → CPU occlusion `:284` → **transparent back-to-front `:292`**) → overlay → postfx/present. The opaque→transparent boundary inside `canvas3d_render_main_pass` (`:218`) is the natural seam for a depth snapshot.
- Particles: billboard quads through the standard mesh path with unlit alpha/additive `Material3D` (`world/rt_particles3d.c:980-1056` — `cached_material:100`, per-frame `draw_materials[]:105`, additive = one batched draw, alpha = depth-sorted by `view_depth:997`). They are transparent-partition draws, rendered after the depth seam. No custom shader.
- Water: mesh + alpha, double-sided, env-cubemap reflection via `set_env_map`/`set_reflectivity` (`world/rt_water3d.c:61-120`) — also a transparent-partition draw.
- Draw cmd has material identity to flag participation: `reflectivity`, `roughness`, `workflow`, `shading_model`, `custom_params[8]` (`backend/vgfx3d_backend.h:43-125`).
- Motion/inverse matrices for reprojection/reconstruction: `scene_inv_vp` already maintained in the D3D11 history (`_d3d11_shared.h:145-153`); GL/Metal equivalents added by plan 05 (SSAO reconstruction) — **shared prerequisite**.
- HDR scene target: plan 05 §3.1 (SSR ray-march output on LDR loses spec highlights; plan 05 lands first).
- Capability plumbing: `rt_canvas3d_backend_supports` (`_overlay.c:614-639`) for new `"ssr"` / `"soft-particles"` strings; quality profiles `postfx3d_configure_quality_profile` (`rt_postfx3d.c:1222-1265`).

## 3. Design

### 3.1 Shared: post-opaque depth snapshot

- New per-backend target `opaque_depth_tex` (+ optional `opaque_color_tex` for SSR): at the opaque→transparent seam, copy/resolve the current depth (GL `glBlitFramebuffer`/`glCopyTexSubImage2D`; D3D11 `CopyResource`; Metal blit encoder) into a sampleable texture. One new vtable hook: `resolve_opaque_targets(void *ctx)` (optional; NULL ⇒ features report unsupported). Canvas calls it between the opaque flush and the transparent loop in `canvas3d_render_main_pass`.
- Transparent-pass draws may then bind `uOpaqueDepthTex` (+ `uOpaqueColorTex`); new scene uniforms carry near/far + inv-projection for linearization (plan 05 already adds them).
- Software backend: the SW rasterizer's depth buffer is CPU memory — a snapshot is a `memcpy` at the same seam; soft particles are cheap there; SSR on CPU is **skipped** (capability off) — cost unjustified.

### 3.2 Soft particles

- Extend the particle draw path: flag `cmd->custom_params` (or a new `soft_particle_fade` field on the draw cmd — field preferred, explicit) with the fade distance; the transparent-pass fragment shader, when fade > 0 and `uOpaqueDepthTex` bound, computes `fade = saturate((linearize(sceneDepth) - linearize(fragDepth)) / fadeDistance)` and multiplies alpha.
- `rt_particles3d.c`: set the fade field from a new emitter param (`Particles3D.SetSoftness(distance)`; default on with distance ≈ particle size for the `Effects3D` presets — smoke/dust/explosion gain it automatically).
- SW backend implements the same fade against its snapshot (readback is native there).

### 3.3 SSR

- Half-res ray-march pass executed at the postfx stage (it needs completed opaque color+depth; postfx already owns those bindings): hierarchical-free linear march (16-24 steps + 4 binary-refine steps), jittered start (IGN), roughness-aware: ray from `reflect(V,N)` where N comes from depth-derivative reconstruction (plan 05's SSAO function, reused), march only for pixels whose material wrote reflectivity — requires knowing reflectivity per pixel: **v1 scope decision — SSR as a postfx effect applied to `Water3D` and floor-like materials via a new material flag `ssr_enabled` that writes a 1-byte mask into the motion target's spare channel** (`scene_motion_tex` is RGBA8; RG = velocity, B free — write reflectivity×ssr_enabled there; zero new targets for the mask).
- Composite: `sceneColor + ssrColor * F(roughness, NdotV) * mask`, falling back to the existing env-cubemap term where the ray misses (screen-edge fade + backface rejection). Output blends **before** tonemap (HDR).
- New postfx enum entry `VGFX3D_POSTFX_EFFECT_SSR` + snapshot params (`ssr_intensity`, `ssr_max_roughness`, `ssr_steps`); `PostFX3D.AddSSR(...)`; CINEMATIC quality adds it when `BackendSupports("ssr")`.
- `Water3D` sets the material flag automatically when SSR is available (`rt_water3d.c` material setup `:83-86`).

## 4. Implementation steps

1. Depth-snapshot hook + canvas seam call + SW memcpy snapshot + a readback unit test (snapshot equals pre-transparent depth; transparent draws don't affect it).
2. Soft particles: draw-cmd field + particle param + shader fade (×4 incl. SW) + `Particles3D.SetSoftness` + Effects3D preset defaults + golden probe (smoke column through floor: hard line before, fade after).
3. Reflectivity mask into motion-target B channel (×3 GPU shaders) + material flag plumbing (`Material3D.set_SsrEnabled` internal; public via Water3D + `Material3D` prop).
4. SSR march + composite pass (Metal first, then GLSL/HLSL code-complete w/ waivers) + water-reflection golden probe (box above water reflects; screen-edge fade present).
5. PostFX3D.AddSSR + quality-profile wiring + capability strings + docs.

## 5. Public API changes (runtime.def)

- `Viper.Graphics3D.Particles3D`: `RT_METHOD("SetSoftness","obj(f64)")` (0 disables).
- `Viper.Graphics3D.Material3D`: `RT_PROP("SsrEnabled","i1", get/set)`.
- `Viper.Graphics3D.PostFX3D`: `RT_METHOD("AddSSR","obj(f64,f64)")` (intensity, maxRoughness).
- `BackendSupports` strings `"ssr"`, `"soft-particles"`. Docs: `rendering3d.md` (SSR/soft-particle sections + limitations: opaque-only reflections, screen-space misses fall back to env map).
- No new classes; draw-cmd field addition is internal vtable-adjacent (same ADR posture as plan 04 — additive internal struct member, note in commit).

## 6. Tests

- Depth snapshot: SW unit test (byte-compare snapshot vs pre-transparent buffer).
- Soft particles: golden (Metal + SW) smoke-through-floor; unit: fade=0 byte-identical to today (regression pin).
- SSR: golden water probe (Metal); miss-fallback probe (reflected object off-screen → env-map term, no streaks); mask-channel probe (motion vectors unaffected in RG — TAA from plan 05 keeps working: run the TAA stability probe with SSR on).
- Perf recorded: SSR half-res cost on openworld_slice water scene.

## 7. Verification gates

Full build + ctest + `-L slow`; goldens on Metal (+ SW where the feature exists); GL/D3D11 waivers; TAA+SSR interaction probe green; `ridgebound` water visually spot-checked.

## 8. Risks & constraints

- **Order dependency:** plan 05 must land first (HDR target, inv-VP uniforms, depth-derivative normal reconstruction — all reused here).
- **Motion-target B-channel reuse** couples SSR's mask to the motion buffer's format — if a future feature needs that channel, SSR needs its own R8 mask target (documented escape hatch).
- **SSR ghosting/noise:** jittered march under TAA is the intended pairing; without TAA, keep intensity conservative (default 0.5).
- **Zero external dependencies:** march/refine/fade implemented from the published techniques' math, fresh code, no ported shaders.
