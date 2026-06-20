# Configurable Runtime Capacity Caps

**Status:** Verified (caps are real `#define`s) — but scope carefully; several caps are
deliberate per-node tuning, not user-facing limits.
**Area:** `src/runtime/game/`
**Effort:** S–M
**Roadmap fit:** v0.3.x P3 (missing features) / P2 (engine bug work)

## Problem

Several game systems carry hard-coded capacity limits as compile-time `#define`s.
Confirmed examples:

- `rt_quadtree.h`: `RT_QUADTREE_MAX_RESULTS 256`, `RT_QUADTREE_MAX_ITEMS 8`,
  `RT_QUADTREE_MAX_DEPTH 8`
- `rt_gameui_widgets.c`: `RT_UITABLE_MAX_ROWS 512`, `RT_UITABLE_MAX_COLUMNS 16`,
  `RT_UIDROPDOWN_MAX_OPTIONS 32`, `RT_UIMODAL_MAX_CHILDREN 16`, `RT_UIMODAL_MAX_BUTTONS 4`
- `rt_gameui_textinput.c`: `RT_UITEXTINPUT_MAX_BYTES 512`
- `rt_screenfx.h`: `RT_SCREENFX_MAX_EFFECTS 8`

A dense scene can silently exceed `MAX_RESULTS`/`MAX_ROWS` and **truncate** content.

## Critical distinction (do not over-apply)

Two categories, treat differently:

- **Algorithm tuning (LEAVE FIXED):** `RT_QUADTREE_MAX_ITEMS`/`MAX_DEPTH` control
  per-node bucket size and tree depth — these are tuning knobs, not user capacity.
  Exposing them as "limits to raise" invites misuse. Leave fixed (or expose only as an
  advanced constructor hint).
- **User-facing capacity (MAKE GROWABLE/CONFIGURABLE):** `MAX_RESULTS`, `MAX_ROWS`,
  `MAX_OPTIONS`, `MAX_CHILDREN`, `MAX_BYTES` — these bound how much *content* the user
  can put in, and truncation is a correctness surprise.

## Goal & scope

- **In:** Audit every `RT_*_MAX_*` in `src/runtime/game/` (and check `rt_physics2d*`
  for a body cap — the review *claimed* 256 bodies but it was **not** found in the grep;
  verify before acting). For user-facing capacity caps, switch fixed-size arrays to
  **grow-on-demand** or accept a capacity at construction, and make overflow behavior
  explicit and consistent.
- **Out:** Per-node algorithm tuning; unbounded growth without a guard.

## Design

- For result/collection buffers (quadtree query results): replace the fixed stack array
  with a grow-on-demand buffer that doubles capacity **outside the hot path** (grow on
  insert, never realloc mid-traversal), or return a count + let the caller pass a sized
  output. Preserve the existing fast path for the common small case (small-buffer
  optimization: stack array up to N, heap beyond).
- For UI widgets (table rows, dropdown options): accept capacity at `New`/`Configure`,
  or grow the backing store; cap with a generous, documented hard ceiling to bound
  worst-case memory.
- **Overflow contract:** today some caps silently truncate. Pick one consistent policy
  per category — grow (preferred for content) or trap with a clear message — and apply
  it uniformly. Document it.

## Implementation steps

1. `grep -rnE '#define[[:space:]]+RT_[A-Z0-9_]*(MAX|LIMIT)' src/runtime/game/` → build
   the full inventory; categorize each as *tuning* vs *capacity*.
2. For each capacity cap: introduce a growable backing store or a `New(capacity)` param;
   keep the small-size fast path.
3. Make overflow behavior explicit (grow / trap) and consistent; remove silent truncation.
4. Update any `runtime.def` constructor signatures that gain a capacity arg; run
   `check_runtime_completeness.sh`.
5. Document new defaults + ceilings in `docs/viperlib/` (game section).

## Tests (`src/tests/runtime/`)

- Insert `cap + N` items into a quadtree region; assert **all** are returned (no
  truncation) and counts are correct.
- Add `> RT_UITABLE_MAX_ROWS` rows; assert growth, correct render/iteration.
- Real-time check: growth happens off the per-frame hot path (assert no realloc during a
  steady-state frame once capacity is reached).
- Memory ceiling honored where a hard cap is intentionally retained.

## Cross-platform

Pure C; no platform concerns. Watch real-time allocation behavior equally on all targets.

## Documentation

- Document the new defaults, growth behavior, and the (now consistent) overflow contract
  per widget/system in the `docs/viperlib/` game section; **remove stale references to
  the old hard limits**.
- Document any new capacity constructor parameters and their defaults.
- One concise release-notes line.

## Risks / open questions

- **Real-time allocation in the game loop** is the main hazard — grow amortized and
  off-hot-path, or preallocate from a capacity hint.
- **ABI/signature changes** if constructors gain a capacity parameter — keep a default
  overload so existing code compiles.
- **Verify the physics-body cap claim** before writing code for it; it was unconfirmed.
