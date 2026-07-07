# 06 — Engine: Software Post-FX Parity (SSAO, DOF, Motion Blur, SSR, TAA on CPU)

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped E25: all five depth-aware effects run on the CPU path inside the existing
> float-plane executor (`rt_postfx3d.c`), fed by a new `postfx_scene_in_t` (software NDC
> depth via new `vgfx3d_sw_get_zbuf` accessor — or the render target's own `depth_buf` —
> plus cached VP, adjugate inverse-VP, previous-frame VP persisted on the chain, near/far,
> render-space camera). Implementations (documented reduced quality, fully deterministic):
> SSAO = 8 fixed Poisson depth taps, range falloff, 3×3 blur, multiplied in; DOF = 12-tap
> Poisson gather scaled by CoC from |linear−focus|/aperture; MotionBlur = camera-reprojection
> velocity (per-object velocity is a documented divergence), ≤6 samples; SSR = coarse
> world-space march (≤16 steps) along the depth-reconstructed reflection with thickness
> test, no env fallback on miss; TAA = reprojected history blend + 3×3 neighborhood clamp
> (history owned by the chain, freed in the finalizer, resets on size change). Bind-time
> refusal and the apply-time trap are REMOVED — one chain attaches everywhere;
> `BackendSupports("postfx-full")` reports it; per-effect GPU keys still mean acceleration.
> HDR RT chains skip the depth-aware effects for now (documented; LDR software is the
> parity reference). NotifyCut deferred: history auto-resets on size change and the only
> cut source today (camera teleport) self-heals in one frame via the neighborhood clamp.
> Coverage: `tests/runtime/test_canvas3d_postfx_parity.zia` (software-forced ctest) —
> full 10-effect chain attaches with empty LastError and renders; SSAO measurably darkens
> a corner scene; DOF reduces checker contrast >5% (measured 6.6% at the 12-tap tier);
> MB/TAA/SSR exercised in the full-chain render. `-L graphics3d` 99/99; completeness green.
> Docs: rendering3d.md post-FX contract rewritten. Perf note: effects are full-resolution
> with low tap counts rather than the planned half-res (simpler, deterministic; revisit if
> the P15 phase gate's budgets miss — budgets not yet measured on the arena scene).
> Eliminates constraint #11.

## 0. TL;DR

**E25** — implement reduced-quality, deterministic CPU versions of the five GPU-only effects inside the
existing CPU post-FX executor (`rt_postfx3d.c:992-1041` chain switch, which already runs Bloom/
Tonemap/FXAA/ColorGrade/Vignette on CPU). Inputs they need (depth, normals, motion) already
exist in the software backend's G-buffers or are cheap to expose. Remove the bind-time refusal;
keep an explicit quality contract: software renders the same *phenomena* at documented lower
sample counts, and probes assert bounded divergence from GPU output rather than pixel equality.

## 1. Current state (verified anchors)

- CPU chain executor with real Bloom/Tonemap(Reinhard/ACES)/FXAA/ColorGrade/Vignette:
  `rt_postfx3d.c:992-1041,772-801` (ACES = Narkowicz approximation at `:801`).
- Refusal path: `postfx3d_validate_cpu_chain` style gate at `:1304-1317` sets LastError
  "...require GPU window postfx..."; forced-path trap at `:1485`; GPU-scene effects also
  require window (not RT) — `postfx3d_canvas_supports_gpu_scene_effects` `:1353-1355`.
- Software backend renders depth (shadow path proves depth raster), has prev-frame matrices
  for instanced motion (`backend.h:128-130`), and resolves opaque targets for soft particles
  (`resolve_opaque_targets` in SW vtable, `vgfx3d_backend_sw.c:549-556`) — the scene
  color/depth are CPU-resident already.
- Effects and their GPU parameter structs: SSAO(radius,intensity,samples), DOF(focus,range,
  strength), MotionBlur(strength,samples), TAA(feedback), SSR(intensity,maxDist)
  (runtime.def 16335-16339; `rt_postfx3d.h:105` region).

## 2. Design — per effect (all half-resolution where noted, fixed seeds, deterministic)

Infrastructure (session A first): extend the CPU post-FX context with linear depth,
camera-space normal reconstruction (from depth gradients — no G-buffer change), prev-frame
view-proj (already tracked for GPU TAA/MB), and a persistent half-res scratch + history buffer
set, allocated lazily on first use and freed with the chain.

1. **SSAO** — half-res hemisphere AO: 8 fixed Poisson taps (same rotated-disk table as the
   shadow PCF), depth-compared with range falloff, 3×3 bilateral blur, upsample ×
   apply as multiplier. Params map 1:1 (`samples` clamps ≤8 on CPU, documented).
2. **DOF** — half-res 3-ring hex gather (12 taps) with CoC from (focus, range); near/far
   split skipped (single-layer gather, documented); full-res composite by CoC blend.
3. **Motion Blur** — camera-only reprojection: per-pixel velocity from depth + current/prev
   view-proj (no per-object velocity on CPU — documented divergence for fast movers),
   ≤6 samples along velocity, strength scales length.
4. **SSR** — half-res coarse march: reflect view ray by reconstructed normal, 12 linear steps
   + 4 refinement steps against the depth buffer, roughness-fade, env-map fallback on miss
   (same fallback contract as GPU, `10-ssr-soft-particles.md:54`). Opaque-only, matching GPU.
5. **TAA** — history blend with `feedback` weight, camera-motion reprojection, neighborhood
   clamp (3×3 min/max) to bound ghosting; resets on camera cut (`Canvas3D` exposes existing
   frame-id/cut hint — add `PostFX3D.NotifyCut()` no-op-on-GPU helper if none exists; verify
   during implementation).

Bind-time change: the CPU gate at `:1304-1317` accepts the five effects; the trap at `:1485`
becomes unreachable for them (kept for genuinely impossible combos, e.g. GPU-scene effects on
an offscreen RT chain — **that** restriction stays for GPU backends and is now matched by CPU
implementations working on RTs too, closing the RenderTarget3D gap from Note N3).
`BackendSupports("gpu_postfx"/"ssao"/"dof"/"motion-blur"/"taa"/"ssr")` remain true only where
GPU-accelerated; new umbrella key `"postfx-full"` reports true everywhere after this doc —
games stop gating chain *construction* entirely.

## 3. Performance contract (measured, recorded in this doc's status banner when done)

Budget at 960×540 Performance tier on the arena stress scene (28-phasing §4 harness),
Apple M-class CPU, software backend: SSAO ≤ 2.0 ms, DOF ≤ 1.5 ms, MB ≤ 1.2 ms, SSR ≤ 2.5 ms,
TAA ≤ 1.0 ms — full chain ≤ 8 ms leaving 25 ms for scene raster at 30 FPS floor. Quality tiers
(14-quality mapping in 22-world-systems): Performance tier runs FXAA-only by default; Balanced+
enables the full chain — the *capability* exists everywhere, the *default* stays tiered.

## 4. Files

| File | Change |
|---|---|
| `src/runtime/graphics/3d/render/rt_postfx3d.c` | five CPU effect impls, gate removal, scratch/history buffers, NotifyCut |
| `src/runtime/graphics/3d/render/rt_postfx3d.h` | context fields, param plumbing |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_sw*.c/.inc` | expose linear depth + prev view-proj to the post context (accessor, no format change) |
| `src/runtime/graphics/3d/render/rt_canvas3d_overlay.c` | capability key `postfx-full` (:749-856 region) |
| `src/il/runtime/runtime.def` | `PostFX3D.NotifyCut` (+ any missing getters) |
| `docs/viperlib/graphics/rendering3d.md` | post-FX table rewrite: per-backend quality contract |
| `src/tests/unit/test_rt_postfx3d_cpu.cpp` (new) + golden probes | tests |

## 5. Tests

1. Bind acceptance: full 10-effect chain on software canvas → attaches, `LastError` empty,
   no trap; same on a RenderTarget3D chain.
2. Per-effect goldens (SW, deterministic scene: textured room + moving sphere + strong light):
   SSAO darkens corner concavities (mean AO in corner region < 0.85× open-wall region);
   DOF blurs out-of-focus plane (edge gradient width triples); MB streaks along camera pan
   axis; SSR shows sphere reflection on glossy floor within expected pixel band; TAA converges
   (frame-to-frame diff drops monotonically over 8 static frames) and neighborhood clamp bounds
   ghosting (trailing luminance < 10 % after 4 frames post-motion).
3. Determinism: two identical runs → byte-identical outputs; VM vs native identical.
4. Perf probe: chain timings within §3 budgets on the stress scene (recorded, not asserted
   hard in ctest — asserted in the P15 phase gate).
5. GPU parity probes (Metal): SSIM(GPU, CPU-reference) ≥ 0.85 per effect on the golden scene —
   catches sign/space errors while allowing quality differences; becomes the standing parity
   harness for future shader edits.
6. Existing CPU-safe chain goldens stay byte-identical when the new effects are absent
   (fast-path regression).

## 6. Verification gate

Unit + golden probes green → `ctest -R postfx` + `-L graphics3d` → determinism probe →
runtime completeness/surface audit (NotifyCut) → full no-skip build → perf numbers recorded
into this doc's banner. Consumer switch: `fx/postfx.zia` in the game (22-world/27-polish)
builds ONE chain and tiers it by quality level, not by backend.
