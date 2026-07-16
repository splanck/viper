---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0100: Profiling Depth — Per-Pass Draw Attribution + Hitch Tracer

Date: 2026-07-11

## Status

Accepted

## Context

GPU cost was one number (`FrameGpuTimeUs`) and streaming cost was invisible
until it hitched. Shipping-quality optimization needs attribution, not
totals: which pass owns the draws, and which subsystem owns the spikes.

## Decision

- **Pass ids:** `Game3D.RenderPass` constants (Shadow=0, Opaque=1,
  Transparent=2, PostFX=3, Overlay=4, Present=5), enum-accessor pattern.
- **Per-pass draw counters (CPU-exact, all backends):**
  `Canvas3D.PassDrawCount(pass)` / `PassInstanceCount(pass)` — tallied at
  the two submit seams the whole renderer funnels through
  (`canvas3d_submit_deferred` attributes Opaque/Transparent/Overlay from
  the deferred command's pass kind + blend requirement;
  `canvas3d_shadow_deferred` tallies the shadow pass), reset at frame
  begin with the existing counters. Out-of-range pass ids read 0.
- **Hitch tracer:** a 256-entry chronological ring on World3D of
  `{frame, source, ms}` with `SetHitchThresholdMs` (default 25),
  `HitchCount/HitchFrame/HitchSource/HitchMs/ClearHitches`, and
  `Game3D.HitchSource` constants. Fed post-step: a growth in the stream's
  `stream_stall_ms` watermark above threshold logs **StreamCommit**;
  otherwise a step whose wall time (via the monotonic `rt_clock_ticks_us`)
  exceeds the threshold logs **FrameTotal** — an attributed source
  suppresses the same step's FrameTotal so hitches never double-count.
  Wall-clock reads are telemetry-only and never touch simulation state.

## Consequences

- Deferred (recorded): per-pass **GPU timestamps** (Metal counter sample
  buffers, D3D11 disjoint queries, GL `GL_TIME_ELAPSED` — per-backend
  platform work; GL/D3D11 cannot even compile on the primary dev machine,
  so the whole timing column ships together later behind a
  `BackendSupports("pass-timing")` capability rather than shipping dead
  API now), SW CPU pass timers, PostFX/Present draw attribution (honest
  zeros in v1), AssetCommit/TextureUpload hitch sources, Debug3D overlay
  page 2, and the perf-harness `PASS:`/`HITCH:` lines.
- Test: `g3d_test_game3d_profiling_probe` — stable constants, a 3-opaque +
  1-blended scene attributes 3/1 to Opaque/Transparent with instance
  tallies, out-of-range ids read 0, a zero threshold logs FrameTotal
  hitches in chronological order, ClearHitches empties. VM == native.

## Links

- misc/plans/thirdpersonupgrade/30-profiling-depth.md
- src/runtime/graphics/3d/render/rt_canvas3d_occlusion.inc (submit seams)
- ADR 0083 (streamStallMs), rendering3d.md §Performance Telemetry
