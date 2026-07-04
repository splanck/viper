# Plan 06 — Shadow Quality Upgrade (Bias Model, Poisson PCF, Cascade/Slot Decoupling)

> **Status (2026-07-03): §3.1/§3.2 IMPLEMENTED; §3.3 DEFERRED (design blocker found).**
>
> Shipped across all four shader sources + the software rasterizer:
> - **Bias model:** receivers are normal-offset ~1.5 shadow texels before projecting
>   into light space, with the per-slot world texel size and depth scale derived
>   *in-shader* from the shadow VP row lengths (no new plumbing; far cascades offset
>   proportionally, perspective/spot slots skip the offset). The compare bias adds a
>   slope-proportional per-texel term, finally wiring the existing-but-inert
>   `SetShadowSlopeBias` knob into the sampling compare. The software rasterizer
>   scales its map-gradient slope term by the same knob.
> - **`Canvas3D.SetShadowStrength(s)`:** replaces the hard-coded `mix(0.15, 1.0, vis)`
>   lit floor (default 0.85 reproduces the legacy look; 1.0 gives fully-black shadows).
> - **Poisson PCF:** shared 16-point Poisson disk (deterministic dart-throwing, LCG
>   seed 80, min separation 0.4369, embedded in GLSL/MSL/HLSL and vgfx3d_backend_sw.c)
>   rotated per-pixel by interleaved-gradient noise; `Canvas3D.SetShadowQuality(0|1|2)`
>   selects 4/8/16 taps (software caps at 8 for CPU cost). Metal/D3D11 taps run through
>   the hardware comparison sampler. Knobs travel via three appended
>   `vgfx3d_camera_params_t` fields; Metal reuses the spare `ibl_params` lanes and
>   D3D11 appends `float4 shadowFilter` to PerScene.
>
> **§3.3 cascade/slot decoupling is deferred with a corrected finding:** the design
> called for four additional GL samplers (`uShadowCascadeTex0..3`), but the GL main
> fragment program already assigns texture units 0–15 (diffuse/normal/specular/
> emissive, shadow0-3, splat×5, env, metallic-roughness, AO) — GL 3.3's guaranteed
> fragment-sampler minimum is exactly 16, so there is no headroom for more individual
> samplers on min-spec hardware. Doing this right requires consolidating shadow
> storage into array textures (one unit for all slots), which in turn requires
> uniform slice sizes across slots — a standalone storage-consolidation design, not
> a sampler-count bump. Deferred until that design lands; until then cascades and
> shadow lights share the 4-slot budget exactly as before.
>
> Verified: MSL revalidated via the runtime Metal compiler harness; 245/245 canvas3d,
> 88/88 graphics3d, 21/21 surface audits; Zia probe of the new setters green.
> GL (Linux) / D3D11 (Windows) code-complete under the standing waivers.

## 1. Objective & scope

Shadows currently use one global depth bias and a fixed 3×3 PCF, so users trade acne against peter-panning with a single knob and can't win both; and the 4 shadow slots are shared between *distinct shadow-casting lights* and *cascade splits of the sun*, so enabling 4-cascade CSM consumes the entire shadow budget.

**In scope:** normal-offset + per-cascade slope-scaled bias; rotated-Poisson PCF with quality-scaled kernel; decoupling the directional-cascade budget from other shadow lights; software-backend parity. **Out of scope:** PCSS/contact-hardening (follow-up), point-light omni shadows (cube maps), shadow-map atlasing.

**Zero external dependencies:** all sampling kernels and bias math implemented from scratch in the four in-tree shader sources + the CPU shadow rasterizer; no shadow libraries or borrowed shader code.

## 2. Current state (verified anchors)

- Slot model: no slot struct — canvas state (`rt_canvas3d_internal.h:890-899`): `shadow_rts[VGFX3D_MAX_SHADOW_LIGHTS]`, `shadow_light_vps[4][16]`, `shadow_bias`, `shadow_slope_bias`, `shadow_cascade_count`, `shadow_resolution`; slot identity rides on `vgfx3d_light_params_t` (`shadow_index/shadow_cascade_count/shadow_projection_type/shadow_cascade_splits[VGFX3D_MAX_SHADOW_LIGHTS]`, `backend.h:244-259`). `VGFX3D_MAX_SHADOW_LIGHTS = 4` (`internal.h:488`).
- Orchestration: `render/rt_canvas3d_render_pass.inc:85-204` — cascades only for the primary directional light (`selected_lights[0].type==0 && shadow_cascade_count>1`); each cascade consumes one of the 4 slots.
- Maps: square CPU float depth buffers passed via `shadow_begin(ctx, slot, float *depth_buf, w, h, light_vp)` / `shadow_draw` / `shadow_end(ctx, slot, bias)` (`backend.h:316-319`); lazily allocated `canvas3d_ensure_shadow_target_slot` (`_shadow.inc:491-532`). GL binds individual `uShadowTex0..3` sampler2Ds (`shaders.inc:218-221`); D3D11 `shadowTex0..3` t4-t7.
- VP construction: splits `canvas3d_compute_shadow_cascade_splits` (`_occlusion.inc:539`); ortho fit `canvas3d_build_shadow_light_vp` (`_shadow.inc:393`) + `canvas3d_shadow_build_ortho_projection` (`:215`); spot `canvas3d_build_spot_shadow_light_vp` (`:306`); texel snap `canvas3d_shadow_snap_light_bounds` (`:351`).
- Sampling: `sampleShadowMap` (`shaders.inc:307-339`) — fixed 3×3 PCF, compare `depth - uShadowBias <= smp` (`:335`), applied `mix(0.15, 1.0, shadow)` (`:468-471,:522-525`). Cascade pick `resolveShadowCascade` (`:276-291`). Single `uShadowBias` uniform (`_opengl_material.inc:330`); `shadow_slope_bias` exists on the canvas and the public API but flows into the *raster* pass, not the sampling compare.
- Software: `_sw_shadow.inc` — `sw_shadow_begin` (`:872`), tiled parallel raster `sw_shadow_draw_parallel` (`:796`), self-documented "no anti-aliasing" (`:14`).
- Public API today (`Canvas3D`): `EnableShadows`, shadow bias/slope-bias/cascade-count/resolution props (def block ~13171); `Material3D.ShadowMode`; `Light3D` shadow-casting flag; capability `"shadow-csm"` gate (`rt_canvas3d.c:2375`).

## 3. Design

### 3.1 Bias model — normal-offset + per-cascade slope-scale

- **Normal-offset:** offset the *sampling position* along the surface normal before projecting into light space: `worldPos += N * normalOffset * texelWorldSize(cascade)`. Per-cascade texel world size is derivable from the ortho bounds already computed in `canvas3d_build_shadow_light_vp` — pass per-slot `shadow_texel_world_size[4]` through the light params (new field beside `shadow_cascade_splits`).
- **Per-cascade slope-scaled compare bias:** `bias = base + slope * tan(acos(NdotL))` clamped, with `base`/`slope` scaled by cascade texel size so far cascades (coarser texels) get proportionally larger bias. Wire the existing-but-unused `shadow_slope_bias` into the shader compare (new uniform `uShadowSlopeBias`), keeping `uShadowBias` as the base term — no new user knobs required; existing props gain their intended meaning.
- Kill the hard `mix(0.15, 1.0, ...)` floor as a hidden constant: promote to `uShadowStrength` uniform (default 0.85 ⇒ same look), exposed as `Canvas3D.ShadowStrength` — users can finally get fully-dark shadows.

### 3.2 Filtering — rotated-Poisson PCF, quality-scaled

- Replace the 3×3 box with a Poisson-disk kernel (fixed 16-point table, from-scratch generated offline and embedded as constants) rotated per-pixel by interleaved-gradient-noise angle; sample count by quality tier: PERFORMANCE 4, BALANCED 8, CINEMATIC 16 (new uniform `uShadowSampleCount` driven from the quality profile / a `Canvas3D.ShadowQuality` prop).
- Software backend: implement the same Poisson loop in the SW shading path at the BALANCED count max (cost-bounded); keep the current 3×3 as its PERFORMANCE tier.

### 3.3 Cascade/slot decoupling

- Split budgets: `VGFX3D_MAX_SHADOW_CASCADES 4` (sun) + `VGFX3D_MAX_SHADOW_LIGHTS` reinterpreted as *additional* non-directional shadow lights (spot; keep 4). Canvas state grows: `shadow_cascade_rts[4]` + `shadow_cascade_vps[4][16]` alongside the existing per-light slots; `vgfx3d_light_params_t.shadow_index` for the directional light indexes the cascade set (flagged via `shadow_projection_type`).
- Backend surface: raise sampler count — GL `uShadowTex0..3` + `uShadowCascadeTex0..3` (or move to a `sampler2DArray` for cascades where supported — decide per backend; array texture preferred for GL/Metal/D3D11, individual textures remain the fallback); `shadow_begin` gains a `is_cascade` flag or slot-space convention (slots 0-3 = lights, 4-7 = cascades) — **convention chosen: extend slot index range 0..7**, no signature change.
- Orchestration change in `render_pass.inc:85-204`: render sun cascades into the cascade set AND up to 4 spot shadow maps in the same frame. Shader: cascade resolve reads the cascade set; spot shadows read the light set — `resolveShadowCascade` and the shadow-index plumbing split accordingly.
- Memory guard: 8 potential maps × resolution² × 4 bytes — default `shadow_resolution` stays as-is; lazy per-slot allocation already exists (`canvas3d_ensure_shadow_target_slot`) so cost is only paid for active slots.

## 4. Implementation steps

1. `uShadowStrength` + wire `shadow_slope_bias` into the compare (×4 sources + SW) — smallest visible win, no structural change; acne/peter-pan probe pair added first (fails-before on grazing-angle acne).
2. Normal-offset sampling + per-slot texel-size plumbing (×4 + SW) + probe update.
3. Poisson PCF + quality scaling (×4 + SW) + penumbra-smoothness probe.
4. Cascade/slot decoupling: canvas state + render-pass orchestration + slot-space convention + backend sampler expansion (Metal + SW verified; GL/D3D11 waivered) + a "4-cascade sun + 2 spot shadows coexist" probe.
5. Quality-profile + docs updates.

## 5. Public API changes

- `Viper.Graphics3D.Canvas3D`: `RT_PROP("ShadowStrength","f64", get/set)`, `RT_PROP("ShadowQuality","i64", get/set)` (0/1/2 tier override; -1 = follow quality profile). Existing bias/slope-bias/cascade props unchanged in signature, upgraded in effect (document the behavior change).
- `World3D` forwarders for both. Docs: `rendering3d.md` shadows section rewrite (bias tuning guide).
- No new classes/IDs. Surface audits + completeness script after def changes.

## 6. Tests

- Golden probes (Metal + SW): grazing-angle ground acne (Given sun at 5° elevation, Then no striping), thin-fin peter-panning (offset stays < 1 texel), cascade-boundary continuity (no visible seam line at split distances), penumbra smoothness (Poisson vs box banding), sun+spot coexistence.
- Unit: per-cascade bias monotonic in texel size; slot-space convention (cascade render targets distinct from light slots); `ShadowStrength=1.0` produces fully-black occluded texels in SW readback.
- Regression: existing shadow goldens regenerate deliberately (bias-model change shifts every shadowed pixel — one reviewed regen commit, reasons documented per baseline).

## 7. Verification gates

Full build + ctest + `-L slow`; probe review on Metal + SW; GL/D3D11 code-complete with Win/Linux waivers; `ridgebound` and `game3d_showcase` visually spot-checked (outdoor sun + interior spot scenes).

## 8. Risks & constraints

- **Golden churn** (same mitigation as plan 05: single reviewed regen commit).
- **SW-backend cost:** Poisson at 16 taps on CPU is expensive — tier-capped at 8; keep the tiled-parallel raster untouched.
- **Slot-space convention** is load-bearing across all four backends + the canvas — land it in one commit with the mock-backend call-order test from plan 04 extended to shadow hooks.
- **Zero external dependencies:** Poisson table generated by a small throwaway script checked in under `scripts/` or embedded with a derivation comment — not copied from an external source.
