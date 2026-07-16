---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0083: Worker-Backed WorldStream3D Staging with Ordered Main-Thread Commits

Date: 2026-07-10

## Status

Accepted

## Context

WorldStream3D cell and terrain-tile payloads loaded synchronously on the main
thread inside `update(dt)`: file read, VSCN parse, scene instantiation, and
collider/nav spawn all landed in one frame. A quadrant jump therefore paid the
whole load burst as a single hitch. The async-asset pipeline already proved
the architecture to copy — workers produce POD only, results ride a commit
queue, and the main thread builds runtime objects — and its worker discipline
(snapshotted C-string paths, no rt-object access off-thread, atomic-flag
handshakes) is the project's threading contract for the 3D runtime.

## Decision

Streaming becomes worker-backed by default with a blocking fallback
(`SetAsyncStreaming(false)`).

- **Worker stage (pure IO + POD):** read `.vscn` text bytes, sidecar bytes,
  and heightmap text; heightmap text parses to a malloc'd height grid
  off-thread (`game3d_heightmap_parse_text`, split from the blocking loader).
  Workers never touch rt objects.
- **Result delivery:** staged payloads ride a process-global
  `rt_g3d_commit_queue` drained at the start of every stream `update`. Each
  job retains its stream, so a stream with staging in flight cannot finalize
  under a live worker; queue cancel hooks free payloads at process teardown.
  Per-kind generation counters (`cell_generation` / `terrain_generation`)
  drop late results after a remount — a *shared* counter proved wrong in
  testing because mounting terrain invalidated in-flight cell staging.
- **Main commit (deterministic):** staged payloads commit inside the existing
  nearest-first recompute pass. `rt_scene3d_load_from_memory` (new internal
  split of the VSCN loader) parses staged text without touching the disk;
  terrain builds its heightmap Pixels from the staged POD grid. Commits are
  **order-gated**: when a nearer desired cell is not yet staged, farther cells
  hold, so the resident sequence equals blocking mode at any worker count.
- **Budgets:** the per-update item budget still applies; `SetCommitBudget`
  adds a staged-byte budget per update (`0` holds commits, oversized-first
  commits alone, mirroring the commit-queue drain rule). `streamStallMs`
  records the worst single commit slice for tuning.
- **Prefetch:** the stream smooths a center-velocity estimate and stages
  (never commits) cells/tiles within the load radius of
  `center + velocity × lookahead` (default 2 s). Jumps beyond the unload
  radius are teleports and reset the estimate. `prefetchedCellCount` is the
  telemetry.
- **Failure semantics:** a missing/corrupt cell payload is recoverable — the
  cell skips with a reload cooldown and `StreamStagingErrors` counts it; a
  missing tile heightmap loads a blank tile exactly like the blocking path.
  Staged payloads dropped without committing count `StreamStaleStagesDropped`.
  Both counters append last in `Diagnostics3D.Summary()` (stable-order
  contract).

## Consequences

- Resident *sequence* is worker-count independent, but residency now lands
  frames after desire: tests and tools that need settled state loop `update`
  until `pendingRequestCount == 0`. Existing synchronous-semantics unit tests
  pin blocking mode explicitly.
- The `native_run` selective-linker gained its first Graphics→Localization
  dependency edge in the same phase; component-closure rules must be kept in
  sync when the graphics runtime grows cross-archive calls.
- `residentBytes` meaning is unchanged — staged bytes are not resident, and
  the mounted-manifest accounting baseline is preserved.
- TSAN lane (`scripts/g3d_tsan_concurrency_lane.sh`) covers the staging path
  via `test_rt_game3d_streaming_async`.
- Tests: `test_rt_game3d_streaming_async` (parity, corrupt-cell recovery,
  cancellation, commit-budget hold/release, prefetch + teleport, blocking
  fallback); the openworld hitch probe asserts a teleport update stages
  off-thread instead of loading inline and still converges to the blocking
  resident set.
