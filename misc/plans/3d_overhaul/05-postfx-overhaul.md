# Plan 05 — Post-Processing Quality Overhaul (HDR Target, Mip-Chain Bloom, Normal-Aware SSAO, TAA, Gamma Fix)

> **Status (2026-07-03): IMPLEMENTED.** All five workstreams landed across the four
> shader sources + CPU fallback:
> - **HDR scene target (GL):** `ensure_scene_targets` now follows the shared
>   HDR16F policy with an RGBA8 retry for drivers without float color targets
>   (`scene_hdr_active`); the postfx readback/scratch ping-pong promotes alongside.
>   Metal/D3D11 were already RGBA16F. New caps: `"hdr-scene"`, `"taa"`.
> - **Mip-chain bloom:** 13-tap Karis-average threshold downsample + 3×3 tent
>   additive upsample over a half-res RGBA16F chain (≤6 octaves, min dim 8) on
>   GL/D3D11/Metal; the shared postfx shader composites mip 0 via a new
>   `bloomTex` binding (legacy 5-tap path retained as a resource-failure
>   fallback). The CPU fallback got the same progressive chain
>   (`apply_bloom` rewrite), with `blur_passes` now meaning chain depth.
> - **SSAO:** rewritten in all four sources — hemispheric golden-angle spiral
>   (4–16 taps) rotated by interleaved-gradient noise, normals from screen-space
>   derivatives of the reconstructed position, and a smoothstep range check that
>   kills silhouette halos. No G-buffer added.
> - **TAA:** new `VGFX3D_POSTFX_EFFECT_TAA` (appended enum), `PostFX3D.AddTAA(blend)`
>   (blend clamps to [0.5, 0.98]), Halton(2,3) sub-pixel projection jitter applied at
>   each backend's `begin_frame` (scene passes only; jittered VP feeds inv/prev
>   history so consumers stay consistent; the resolve subtracts the jitter delta from
>   velocities), RGBA16F ping-pong history with parity flip, neighborhood clamp, and
>   velocity-weighted blend. History invalidates on resize/chain-drop. CINEMATIC
>   quality now prefers TAA over FXAA when GPU window postfx is available; the
>   CPU/software path rejects TAA like SSAO/DOF/motion blur.
> - **Tonemap mode-0 gamma fix:** a new `tonemap_explicit` snapshot flag marks real
>   tonemap chain entries; on linear-HDR scene targets an explicit mode-0 tonemap now
>   applies exposure + gamma-out (matching modes 1/2), including the CPU HDR
>   render-target path (`apply_tonemap`/`postfx_chain_has_tonemap` take an
>   `hdr_active` flag). LDR targets keep the legacy passthrough.
>
> Deviation from §3.3: the planned separate bilateral-blur pass for SSAO was not
> added — IGN rotation + the range check keep noise acceptable without another
> full pass ×4 backends; revisit if a future G-buffer plan lands. Verified:
> 245/245 canvas3d, 92/92 postfx snapshot, 88/88 graphics3d, 21/21 surface
> audits; MSL validated via the runtime Metal compiler (all 5 entry points).
> GL (Linux) and D3D11 (Windows) are code-complete under the standing platform
> waivers.

## 1. Objective & scope

The post-FX stack has the right effect list but toy implementations: bloom is a single-pass 5-tap thresholded box, SSAO is depth-only with 8 fixed offsets and no range check (halos), anti-aliasing is a 4-tap luma blur, and TAA doesn't exist despite the motion-vector infrastructure being in place. Additionally the GL window scene target is LDR RGBA8, so bloom/tonemapping operate on clamped data, and tonemap mode 0 skips gamma-out.

**In scope:** HDR scene color target (window path) where missing; downsample/upsample mip-chain bloom; normal-aware, range-checked SSAO; TAA resolve pass; tonemap-mode-0 gamma fix; CPU-fallback parity in `rt_postfx3d.c`. **Out of scope:** SSR/soft particles (plan 10 — but the depth/normal availability work here is shared), DOF/motion-blur upgrades (keep current), SMAA/MSAA.

**Zero external dependencies:** all passes are hand-written shaders in the four in-tree backends + the from-scratch CPU fallback. No post-processing libraries, no shader-compiler tooling, no reference-implementation code vendored in.

## 2. Current state (verified anchors)

- Architecture: post-FX is a **single fullscreen shader with mode flags**, not discrete passes — GL `glsl_postfx_fragment_src` (`_opengl_shaders.inc:765-929`) with `uBloomEnabled/uSsaoEnabled/uFxaaEnabled/uDofEnabled/uMotionBlurEnabled/uTonemapMode/uColorGradeEnabled/uVignetteEnabled` branches, ping-ponging `scene_color_tex → postfx_scratch_tex/postfx_readback_tex`. Chain structs: `vgfx3d_postfx_chain_t`/`effect_desc`/`snapshot` (`render/rt_postfx3d.h:88-136`), kind enum (`:112-121`). CPU fallback per-pass fns in `render/rt_postfx3d.c` (bloom `:524-572` "simplified 5-tap kernel", FXAA `:702`). Hooks: `present_postfx`/`apply_postfx`/`set_gpu_postfx_enabled/snapshot` (`backend.h:351-366`); orchestration `render/rt_canvas3d_frame_postfx.inc`.
- Targets (GL window path, `_opengl_targets.inc:55-117` `ensure_scene_targets`): `scene_color_tex` = **GL_RGBA/GL_UNSIGNED_BYTE (LDR)**, `scene_motion_tex` = RGBA8 (COLOR_ATTACHMENT1, written by the main FS as `MotionColor` — `shaders.inc:162,416,575`), `scene_depth_tex` = DEPTH_COMPONENT32F. Destroy path `destroy_scene_targets` (`:14-25`). HDR16F exists only for `RenderTarget3D` (`VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F`, `rt_canvas3d_internal.h:567-570`). D3D11/Metal offscreen scene targets are already R16G16B16A16_FLOAT (`_d3d11.c:1021-1024`, `_metal_shared.h:57-60`).
- Bloom: threshold+add in the same shader (`shaders.inc:889-901`). SSAO: `computeSsao` depth-only 8 offsets, no normals/range check (`:809-823`). FXAA: `applyFxaa` 4-tap luma (`:863-879`). Tonemap: `uTonemapMode` 0=passthrough (**no gamma**), 1=Reinhard, 2=ACES, gamma applied only inside modes 1/2 (`:902-911`).
- Motion/TAA groundwork: per-object prev matrices in the draw cmd (`prev_model_matrix`, `backend.h:43-125`), per-object history `canvas_motion_history_t` (`internal.h:1157-1164`, origin-rebase clear via `canvas3d_clear_motion_history`); VP history D3D11 `vgfx3d_d3d11_frame_history_t` (`_d3d11_shared.h:145-153`, roll `_shared.c:178-205`), GL `ctx->draw_prev_vp`, Metal `prev_vp` (`metal.m:1074`). **Missing: a history color buffer and a resolve pass.**
- Quality tiers: `postfx3d_configure_quality_profile` (`rt_postfx3d.c:1222-1265`), PERFORMANCE/BALANCED/CINEMATIC (`rt_postfx3d.h:58-60`); GPU-scene-effect gating via `postfx3d_canvas_supports_gpu_scene_effects` + capability strings in `rt_canvas3d_backend_supports` (`_overlay.c:614-639`).

## 3. Design

### 3.1 HDR scene target (prerequisite)

- GL `ensure_scene_targets`: promote `scene_color_tex` to `GL_RGBA16F` when the driver supports it (GL 3.0+/`GL_ARB_texture_float`); retain the RGBA8 path as fallback with a caps flag. Ping-pong scratch targets promote alongside. D3D11/Metal already HDR — verify only.
- New capability string `"hdr-scene"` in `canvas3d_capability_from_name` so quality profiles and user code can query.

### 3.2 Bloom — real mip chain

Replace the in-shader threshold-add with a multi-pass chain (this is the first *structural* change to the single-shader model — introduce a small pass-sequencer per backend rather than more mode flags):
- Targets: `bloom_mip[N]` (half-res down to ~8px min dimension, N≈5-6), RGBA16F, created in `ensure_scene_targets`.
- Passes: threshold+downsample (13-tap Karis-average on the first downsample to kill fireflies) → progressive downsamples → progressive upsample+accumulate (3x3 tent) → composite into the tonemap input with `intensity`.
- Snapshot params reuse the existing `bloom threshold/intensity/passes` fields (`vgfx3d_postfx_snapshot_t`); `passes` now means chain depth (clamped to available mips).
- CPU fallback (`rt_postfx3d.c`): same chain at quarter resolution using the existing separable-blur helpers upgraded to downsample/upsample loops — keep cost bounded (CINEMATIC-on-software already gates heavy effects via `quality_fallback`).

### 3.3 SSAO — normal-aware, range-checked

- Reconstruct view-space position from `scene_depth_tex` + inverse projection (available: `scene_inv_vp` in the D3D11 history struct; add the GL/Metal equivalent uniform); reconstruct normals from depth derivatives (`cross(dFdx(P), dFdy(P))`) — **no G-buffer needed**, avoids touching the main pass.
- Hemisphere kernel (12-16 spiral taps, per-pixel rotation from an interleaved-gradient noise function — analytic, no noise texture), range check `smoothstep(0, 1, radius / abs(dz))`, intensity/radius from the existing snapshot fields, separable 4-tap bilateral blur (depth-aware) before composite.
- CPU fallback mirrors with the same math at half resolution.

### 3.4 TAA resolve

- New pass + targets: `taa_history_tex` (RGBA16F, persisted across frames), `taa_output` (can alias the ping-pong scratch).
- Resolve: reproject history via `scene_motion_tex` (already RG velocity), neighborhood min/max clamp (3x3, YCoCg optional-v2), blend factor 0.9 static / down-weighted by velocity; history invalidated on first frame, resize, and origin rebase (reuse the `canvas3d_clear_motion_history` trigger to also clear TAA history).
- Camera jitter: sub-pixel Halton(2,3) jitter applied to the projection matrix in `begin_frame` **only when TAA enabled** (jitter must also feed `uPrevViewProjection` unjittered pairing — store both jittered/unjittered VPs in the frame history structs).
- New enum entry `VGFX3D_POSTFX_EFFECT_TAA` (+ internal `POSTFX_TAA`) + snapshot fields (`taa_blend`, `taa_enabled`); exposed as `PostFX3D.AddTAA()`; FXAA and TAA are mutually exclusive in quality profiles (TAA replaces FXAA at CINEMATIC when supported).
- **Determinism note:** TAA is temporal — golden probes for TAA use a fixed N-frame warm-up under synthetic clock (the deterministic `RunFrames`/synthetic-input harness Canvas3D already has) so the accumulated result is reproducible.
- CPU fallback: skip TAA (software = FXAA path); `BackendSupports("taa")` reports capability.

### 3.5 Tonemap mode 0 gamma fix

In all four shader sources: mode 0 applies gamma-out (`pow(c, 1/2.2)`) when the scene target is linear-HDR, matching modes 1/2's output transform. Guard: only when the HDR target is active (LDR path keeps legacy passthrough to avoid double-gamma on existing content). Regenerate affected goldens deliberately (`./scripts/update_goldens.sh`) — expected diffs are brightness-curve only.

## 4. Implementation steps

1. HDR scene target (GL) + caps flag + goldens re-verified (should be near-identical pre-tonemap; regenerate if quantization shifts).
2. Tonemap-mode-0 gamma fix (×4 sources + CPU) + deliberate golden regen + a linear-ramp probe asserting monotone sRGB output.
3. Bloom mip chain: GL/MSL/SW + pass sequencer + emissive-sphere golden (wide halo visible at mip≥3 distances); HLSL code-complete (waiver).
4. SSAO upgrade (×4) + corner-darkening probe (box in corner: occlusion gradient present, no halo beyond radius).
5. TAA: history targets + jitter plumbing + resolve pass (Metal + GL first) + enum/API + stair-step stability probe under camera sway (synthetic clock, fixed frames).
6. Quality-profile updates (`postfx3d_configure_quality_profile`): CINEMATIC prefers TAA over FXAA when supported; BALANCED keeps FXAA; document.

## 5. Public API changes

- `Viper.Graphics3D.PostFX3D`: `RT_METHOD("AddTAA","obj()")` (+ optional blend param overload). Existing Add* signatures unchanged.
- `BackendSupports` strings: `"hdr-scene"`, `"taa"`.
- Docs: `docs/viperlib/graphics/rendering3d.md` post-processing section rewrite (document real bloom/SSAO/TAA behavior + the mode-0 gamma change as a migration note).

## 6. Tests

- Golden probes (Metal + SW where applicable): bloom halo, SSAO corner, TAA stability (N-frame deterministic), gamma ramp.
- Unit: TAA history invalidation on resize/rebase (mock: assert clear called); bloom mip-count clamp; SSAO parameter sanitation (existing `rt_postfx3d_add_ssao` param clamping tests extended).
- Regression: all non-postfx goldens unchanged except the deliberate tonemap regen set (list them in the commit body).

## 7. Verification gates

Full build + ctest + `-L slow`; golden set reviewed image-by-image before regen; Metal + SW verified locally, GL best-effort local-compile-only... **correction: GL does not compile on macOS** — GL is code-complete with Linux waiver like D3D11 (Windows waiver). Quality-profile behavior spot-checked via `game3d_showcase` deterministic scenario.

## 8. Risks & constraints

- **Four-source replication** is heaviest here (every pass ×4 + CPU fallback) — implement GLSL as reference, port mechanically, keep the pass sequencer's structure identical across backends.
- **Jitter leaks:** projection jitter must never reach shadow-VP building, frustum culling, or `ScreenToRay` (`rt_camera3d.c`) — apply it only at the backend `begin_frame` boundary, not in `rt_camera3d` matrices.
- **Golden churn:** tonemap/gamma changes shift every lit pixel — batch the regen in one reviewed commit; the normal-matrix-baseline lesson applies (a re-diff later must be distinguishable from a *new* regression, so document exactly why each baseline changed).
- **Zero external dependencies:** no reference shader code copied from external projects; formulas implemented from the published math (Karis average, Halton, IGN) with fresh code.
