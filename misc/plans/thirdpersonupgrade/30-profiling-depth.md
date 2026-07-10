# Plan 30 — Profiling Depth: Per-Pass GPU Timings, Hitch Tracer, Overlay Breakdown

## 1. Objective & scope

Shipping-quality optimization needs attribution, not totals. Today GPU cost is one number (`FrameGpuTimeUs`) and streaming cost is invisible until it hitches. Add per-pass GPU timestamps (shadow/opaque/transparent/post-FX), per-pass draw counters, a streaming/commit hitch tracer with a readable ring buffer, and a `Debug3D` breakdown page rendering it all in-game.

**In scope:** (a) backend timestamp-query plumbing (Metal/D3D11/GL; SW = CPU timers); (b) pass boundaries + `PassGpuTimeUs(pass)`; (c) per-pass draw/instance counters; (d) `WorldStream3D`/commit-queue timing ring + `streamStallMs` integration (plan 11); (e) Debug3D page 2.
**Out of scope:** external capture-file formats, per-draw GPU timing (pass granularity only), CPU sampling profilers.

**Zero external dependencies — absolute.** Native GPU query APIs are platform SDK surface, not third-party.

## 2. Current state (verified anchors)

- **Whole-frame GPU time exists:** `FrameGpuTimeUs` — "latest completed backend GPU frame time in microseconds, or 0 when unsupported" (`rendering3d.md` §Visibility Controls table); backends already resolve one query per frame asynchronously.
- **Pass structure is explicit in the render path:** shadow slot rendering (`rt_canvas3d_shadow.inc`), main scene draw ordering (opaque → skybox → transparent, `rt_canvas3d_render_pass.inc`/`_draw.inc`), post-FX application (`rt_canvas3d_frame_postfx.inc`) — pass boundaries are code-visible seams; the timestamp hooks bracket them.
- **Telemetry conventions:** counter properties on Canvas3D/World3D (`DrawCount`, `OccludedDrawCount`, `TextureUploadBytes`, `ShadowSlotsUsed` — `rendering3d.md` §Performance Telemetry/§Visibility Controls); the perf harness parses `PERF:` lines (`g3d_openworld_slice_perf_harness`, `game3d.md` §Tests).
- **Overlay diagnostics:** `Debug3D.ShowOverlay` renders backend/FPS/quality/counters post-post-FX (`game3d.md` §Debug3D).
- **Async-readback precedent:** lens-flare depth probes read GPU results one frame late without stalls (`rendering3d.md` §Visibility Controls) — timestamps use the same never-stall rule.
- **Platform APIs:** Metal command-buffer/stage timestamps (`MTLCommandBuffer` GPU start/end, `MTLCounterSampleBuffer` where available), D3D11 `D3D11_QUERY_TIMESTAMP(_DISJOINT)`, GL `GL_TIME_ELAPSED` queries — all standard SDK surface behind the backend vtable (approved adapter layer for platform checks).

## 3. Design

### 3.1 Pass model

Fixed pass ids (constants class `Game3D.RenderPass`): `Shadow=0, Opaque=1, Transparent=2, PostFX=3, Overlay=4, Present=5`. Backend vtable gains optional `pass_begin(pass)/pass_end(pass)` hooks the canvas calls at the existing seams; backends without support leave them null (capability `BackendSupports("pass-timing")`).

### 3.2 Backend timers

- **Metal:** command-buffer stage boundaries or counter sample buffers where the device supports them; per-pass encoder boundaries make sampling natural; results harvested on the *next* frame's begin (one-frame latency, never a wait).
- **D3D11:** disjoint query per frame + timestamp pairs per pass; resolved when available (typically next frame).
- **GL:** `GL_TIME_ELAPSED` query objects per pass, ring of 3 to avoid stalls.
- **SW:** monotonic CPU timers around the same seams (its "GPU" time is CPU raster time — honest and useful).
- Surfaced as `Canvas3D.PassGpuTimeUs(pass)` (latest completed) + `PassGpuTimeAvgUs(pass)` (16-frame rolling average — the overlay reads this).

### 3.3 Per-pass draw counters

CPU-side (all backends): draws, instances, and state-batch counts tallied per pass in the existing submission bookkeeping (`DrawCount` machinery) — `PassDrawCount(pass)`, `PassInstanceCount(pass)`. Zero GPU dependency, exact.

### 3.4 Hitch tracer

Ring buffer (256 entries) of `{frame, source, milliseconds}` where source ∈ {StreamCommit, AssetCommit, TextureUpload, FrameTotal} — fed by plan 11's `streamStallMs` slices, the asset commit-queue drain timing, the texture-upload budget path, and any frame whose total CPU time exceeds `setHitchThresholdMs` (default 25). API: `World3D.hitchCount()/hitchFrame(i)/hitchSource(i)/hitchMs(i)/clearHitches()`. The perf harness gains `HITCH:` summary lines (count, worst, by-source).

### 3.5 Debug3D page 2

`Debug3D.SetOverlayPage(world, 1)` — second page renders: per-pass GPU µs bars (avg + last), pass draw counts, upload bytes, stream residency + pending, worst recent hitches (source + ms). Same overlay path/text budget as page 1.

## 4. Implementation steps

1. Pass-id constants + vtable hooks + CPU per-pass draw counters + SW timers; unit tests on counter attribution (a known scene's opaque/transparent split).
2. Metal timestamps + one-frame harvest (local verify); capability key.
3. D3D11 + GL implementations (waiver pass).
4. Hitch ring + threshold + sources (wire plan-11 slices; asset/texture paths).
5. Debug3D page 2 + `SetOverlayPage`.
6. Perf-harness `HITCH:`/`PASS:` line emission + harness parser update.
7. runtime.def + audits + ADR + docs (`rendering3d.md` §Performance Telemetry expansion).

## 5. Public API changes (runtime.def)

```
Canvas3D: RT_METHOD("PassGpuTimeUs","i64(obj,i64)",…) RT_METHOD("PassGpuTimeAvgUs","i64(obj,i64)",…)
          RT_METHOD("PassDrawCount","i64(obj,i64)",…) RT_METHOD("PassInstanceCount","i64(obj,i64)",…)
World3D:  setHitchThresholdMs(f64), hitchCount()->i64, hitchFrame(i64)->i64,
          hitchSource(i64)->i64, hitchMs(i64)->f64, clearHitches()
"Viper.Game3D.RenderPass": Shadow/Opaque/Transparent/PostFX/Overlay/Present consts
"Viper.Game3D.HitchSource": StreamCommit/AssetCommit/TextureUpload/FrameTotal consts
Debug3D: SetOverlayPage(world, i64)
```

Leaves `RenderPass`/`HitchSource` unique. `BackendSupports("pass-timing")` documented. ADR `00xx-pass-timing-telemetry.md`.

## 6. Tests

- **Counter attribution (C unit):** scene with 3 opaque + 2 transparent draws ⇒ `PassDrawCount` reports 3/2; shadow pass counts shadow-slot draws only (fail-before: no API).
- **SW timing sanity:** SW `PassGpuTimeUs(Opaque)` > 0 for a non-empty scene and passes sum to ≈ frame raster time (±10%).
- **Never-stall:** Metal frame time with timing enabled within noise of disabled (perf probe records both; no synchronous waits — code-review gate + timing assert).
- **Hitch ring:** synthetic 30 ms stream commit (test hook) logs a StreamCommit entry with plausible ms; threshold respected; ring wraps at 256.
- **Overlay page 2:** structural capture assert (labels present) on SW.
- **Harness:** `PASS:`/`HITCH:` lines parsed by the updated harness with required keys.

## 7. Verification gates

Full build + ctest; GPU opt-in smoke on Metal records pass timings; SW goldens unaffected (overlay page 2 off by default); platform lint (backend adapters); `-L graphics3d`; `-L slow`; surface audits; GL/D3D11 waiver.

## 8. Risks & constraints

- **Never stall the pipeline** — every readback is N+1-frame latched (lens-flare precedent); a synchronous query wait is an automatic review reject.
- **Metal counter availability varies by device** — fall back to command-buffer whole-GPU time split proportionally by pass draw counts? No: report 0 for unsupported passes (honest zeros beat estimated numbers; capability key tells tools why).
- **Timer overhead:** 6 query pairs/frame is negligible, but keep timing behind `SetPassTimingEnabled(bool)` (default on where supported, off on GL if the ring proves costly — decided by measurement, recorded here).
- Hitch sources must not double-count (a stream commit inside a slow frame logs once as StreamCommit; FrameTotal only fires when no attributed source explains ≥ half the overshoot).
