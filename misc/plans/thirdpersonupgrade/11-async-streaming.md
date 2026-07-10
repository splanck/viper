# Plan 11 — Worker-Backed Cell/Tile Streaming with Prefetch

## 1. Objective & scope

Kill the structural streaming hitch: `WorldStream3D` cell and terrain-tile payloads load **synchronously on the main thread** during `update(dt)`. Move file reads, VSCN parse, and heightmap decode to the worker pool with ordered main-thread commits (the proven async-asset architecture), add velocity-based prefetch, and expose stall telemetry. Docs already promise this ("Worker-backed decode/upload remains a future streaming layer", `game3d.md` §WorldStream3D).

**In scope:** (a) worker staging of cell VSCN + terrain heightmap/sidecar payloads; (b) budgeted deterministic main-thread commits; (c) prefetch along stream-center velocity; (d) `streamStallMs`/`prefetchedCellCount` telemetry.
**Out of scope:** HLOD proxies (plan 12), texture-upload budgets (exist), new manifest fields.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **Synchronous loads:** `game3d_world_stream_load_cell` (`rt_game3d_streaming.inc:1305`) and the terrain-tile path (`…:958,1249`) run inline from `update(dt)`; the .inc has **zero** worker/thread/async references (verified grep). A cell load = file read + VSCN parse + scene-subtree instantiation + collider/nav-source spawn, all in one frame.
- **Proven async pattern to copy:** async model assets stage bytes + decode on workers, snapshot a cache generation, and publish through the process commit queue — `rt_g3d_commit_queue.{c,h}` (`enqueue/enqueue_cost/drain_budget/pending`), drained by `World3D.tick`/steps with item + byte budgets; stale generations drop and count `Game3D.Diagnostics.StaleAsyncLoadsDropped` (`game3d.md` §Assets3D).
- **Budget/eviction semantics that must hold:** deterministic per-frame load budget, immediate unload of stale payloads, post-load residency-budget eviction, `pendingRequestCount` (`game3d.md` §WorldStream3D). The long-traversal and hitch probes pin these (`g3d_openworld_slice_long_traversal`, `g3d_openworld_slice_streaming_hitch_probe`).
- **Worker pool:** world-owned deterministic pool (`World3D.setWorkerCount`, `game3d.md` §World3D Defaults); asset workers already decode images/meshes off-thread.
- **Content-generation constraint:** scene-object construction (nodes, meshes, materials, colliders) is main-thread-only in the async-asset design (workers produce POD; commit builds runtime objects) — the same split applies here.

## 3. Design

### 3.1 Two-phase cell/tile lifecycle

Cell states: `Unloaded → Staging (worker) → Staged → Committing (main, sliced) → Resident`, plus `Cancelled` when the desired set changes mid-flight (generation snapshot, mirroring assets).

- **Worker stage:** read manifest-resolved files (cell `.vscn` bytes, terrain heightmap text, sidecar bytes), parse VSCN into the loader's intermediate POD (the parse half of `rt_scene3d_vscn_load.c` split into parse-to-POD [worker-safe] and build-from-POD [main]; heightmap text → height array). Missing/corrupt files produce a staged-error record (recoverable, cell skipped, diagnostic counter — never a trap off-thread).
- **Main commit:** budgeted slices through a stream-owned `rt_g3d_commit_queue`: node/mesh/material build, `Terrain3D` payload + `SetHeightmap`, heightfield collider entity, nav-source node, seam stitching (stitching stays main-thread — it touches two resident tiles). Byte costs use the existing manifest/measured estimates so `Assets3D.SetUploadBudget`-style pacing applies (`update` gains `setCommitBudget(bytes)` defaulting to the current frame budget).
- **Ordering/determinism:** desired-set computation stays synchronous and deterministic; staging jobs are enqueued in desired-order and commits publish FIFO (the commit queue is FIFO by contract), so a given center path + budgets yields an identical resident sequence at any worker count — same guarantee the asset path proves in its worker-parity tests.

### 3.2 Prefetch

Stream tracks center velocity (`(center − prev_center)/dt`, smoothed). Desired set adds cells/tiles whose distance to `center + velocity × lookahead` (default 2 s, `setPrefetchLookahead`) is within load radius — staged (maybe committed) early but only counted resident by the normal radius rules. `prefetchedCellCount` telemetry. `setCenter` jumps (teleports) reset the velocity estimate (no phantom prefetch).

### 3.3 Telemetry + compatibility

- `streamStallMs`: wall-time of the worst single commit slice this update (observability for budget tuning) + lifetime max.
- `pendingRequestCount` now includes staging/staged cells (documented meaning-preserving: "desired but not resident").
- Blocking fallback: `setAsyncStreaming(false)` (default **on**) restores inline loads for bisection and low-spec determinism debugging; probes cover both.

## 4. Implementation steps

1. Split VSCN load into parse-to-POD / build-from-POD (pure refactor, blocking path unchanged); full test suite green.
2. Stream-owned commit queue + staged-state machine + generation snapshots; blocking mode still default at this step.
3. Worker staging jobs (cells) + budgeted commit slices; flip default to async; hitch probe extended.
4. Terrain tiles: worker heightmap/sidecar staging + main-thread payload/collider/nav/stitch commits.
5. Prefetch velocity model + lookahead prop + telemetry.
6. `streamStallMs` + docs (`game3d.md` §WorldStream3D rewrite of the "future layer" paragraph) + ADR.
7. Perf evidence: before/after worst-frame ms on `g3d_openworld_slice_long_traversal` quadrant jumps (harness metrics).

## 5. Public API changes (runtime.def)

`Viper.Game3D.WorldStream3D` additions (methods lowerCamel like existing `mountCells/setRadii`):

```
RT_METHOD("setAsyncStreaming","void(obj,i1)",…)   RT_METHOD("getAsyncStreaming","i1(obj)",…)
RT_METHOD("setCommitBudget","void(obj,i64)",…)
RT_METHOD("setPrefetchLookahead","void(obj,f64)",…)
RT_PROP("streamStallMs","f64",get)  RT_PROP("prefetchedCellCount","i64",get)
```

No new classes. ADR `00xx-worldstream-async.md`. `Game3D.Diagnostics` gains `StreamStagingErrors` (process counter, `Summary()` ordering appended last — stable-order contract).

## 6. Tests

- **No-hitch (probe):** Given the openworld_slice manifest — When the center jumps a quadrant with async on — Then no single `update` exceeds the budgeted slice bound while the same jump in blocking mode exceeds it (fail-before evidence) and final resident set matches blocking mode exactly.
- **Determinism:** long-traversal sequence at worker counts 1 vs 4 ⇒ identical resident-set timeline (per-update resident lists compared) and identical final capture; replay ×2 identical.
- **Cancellation:** center reverses while a cell is staging ⇒ staged result dropped, `StaleAsyncLoadsDropped`-style counter increments, no resident leak (resident bytes return to baseline).
- **Corrupt cell:** malformed staged VSCN ⇒ cell skipped, `StreamStagingErrors` increments, stream continues (no trap).
- **Prefetch:** constant-velocity center path stages the next cell before its radius crossing (assert staged state at crossing time); teleport does not prefetch.
- **Budget zero:** `setCommitBudget(0)` holds commits pending (hitch-probe zero-budget pattern) and releases on restore.

## 7. Verification gates

Full build + ctest; the entire streaming lane (`g3d_openworld_slice_*`) green in async and blocking modes; TSAN lane (`scripts/g3d_tsan_concurrency_lane.sh`) extended to the streaming jobs; determinism gate; `-L slow`; `-L graphics3d`.

## 8. Risks & constraints

- **VSCN parse split** is the risk center: the parser touches shared string/asset resolvers — the POD boundary must capture resolved absolute paths at desire-time (main thread) so workers only do IO+parse. Audit `rt_scene3d_vscn_load.c` dependencies first.
- **Seam stitching** across a resident/incoming pair must run after the incoming tile's commit completes — stitch is a commit-queue item enqueued behind the tile's final slice.
- **Meaning of `residentBytes`** must not drift; staged bytes are *not* resident (tests pin).
- Blocking mode stays supported indefinitely (test matrix cost is two probe lanes, acceptable).
