# Configurable Runtime Capacity Caps

**Status:** Completed — user-facing 2D physics, quadtree, ScreenFX, and in-game UI
capacity caps now grow from documented default reservations; per-node tuning remains fixed.
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
- `rt_physics2d.c`: `PH_MAX_BODIES 256` plus fixed scratch/pair structures.

A dense scene can exceed these caps in mixed ways: quadtree queries/pairs set truncation
flags, UI tables/dropdowns/modals trap at limits, text input truncates to 511 bytes, and
screen effects can drop new effects when all slots are in use. The common weakness is
not "all are silent"; it is that user content is bounded by fixed internal arrays unless
callers know to check a flag or handle a trap.

## Critical distinction (do not over-apply)

Two categories, treat differently:

- **Algorithm tuning (LEAVE FIXED by default):** `RT_QUADTREE_MAX_ITEMS`/`MAX_DEPTH`
  control per-node bucket size and tree depth — these are tuning knobs, not user capacity.
  Exposing them as "limits to raise" invites misuse. Leave fixed (or expose only as an
  advanced constructor hint).
- **User-facing capacity (MAKE GROWABLE/CONFIGURABLE):** quadtree result/pair buffers,
  physics bodies/contacts/pairs, UI rows/options/children/buttons, text bytes, and
  screen-effect slots. These bound how much *content* the user can put in.

## Goal & scope

- **In:** Audit every `RT_*_MAX_*` / `PH_MAX_*` in `src/runtime/game/`. For user-facing
  capacity caps, switch fixed-size arrays to **grow-on-demand** or accept a capacity at
  construction, and make overflow behavior explicit and consistent.
- **Out:** Per-node algorithm tuning; unbounded growth without a guard.

## Design

- For quadtree query/pair buffers: preserve existing `WasTruncated` APIs for
  compatibility, but add a growable path (`QueryAll`/`PairsAll` or dynamic internal
  buffers) so callers can ask for complete results without guessing capacity.
- For 2D physics: do **not** just raise `PH_MAX_BODIES`. The current implementation uses
  fixed arrays and narrow body indexes in multiple solver structures. Move bodies,
  contacts, and scratch pairs to world-owned growable heap storage with a default
  capacity and optional reservation.
- For UI widgets (table rows, dropdown options, modal children/buttons): grow backing
  arrays with default capacities and documented ceilings; keep existing no-arg
  constructors as default-capacity overloads.
- For text input: expose a configurable max length/bytes policy (`SetMaxLength` or
  constructor hint) and make truncation observable.
- For screen effects: grow or return a status/handle failure instead of silently dropping
  effects when all slots are active.
- **Overflow contract:** today some caps silently truncate. Pick one consistent policy
  per category — grow (preferred for content) or trap with a clear message — and apply
  it uniformly. Document it.

## Implementation steps

1. Inventory caps with `rg -n '#define[[:space:]]+.*(MAX|LIMIT|CAP)|PH_MAX_' src/runtime/game`;
   categorize each as *tuning*, *bounded-by-design*, or *user-facing capacity*.
2. Start with the correctness-impacting caps: quadtree result/pair buffers, 2D physics
   world body/contact capacity, and screenfx dropped effects.
3. Move UI tables/dropdowns/modals to growable arrays while preserving current defaults.
4. Make overflow behavior explicit (grow / trap / return failure) and consistent; remove
   silent truncation for user-facing content.
5. Update any `runtime.def` constructor signatures that gain a capacity arg; run
   `check_runtime_completeness.sh`.
6. Document new defaults + ceilings in `docs/viperlib/` (game section).

## Tests (`src/tests/runtime/`)

- Insert `old_cap + N` items into a quadtree region; assert complete-query APIs return
  **all** items and legacy APIs still report truncation accurately.
- Create a 2D physics world with `PH_MAX_BODIES + N` bodies; assert stepping, contacts,
  and removal remain correct and no narrow-index overflow occurs.
- Add `> RT_UITABLE_MAX_ROWS` rows and `> RT_UIDROPDOWN_MAX_OPTIONS` options; assert
  growth, correct render/iteration, and documented max-length behavior.
- Fill `RT_SCREENFX_MAX_EFFECTS` and add one more effect; assert the new contract (growth
  or failure status) rather than silent drop.
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

## Implementation notes

- Physics2D worlds now grow body, joint, contact, broad-phase pair, and force-snapshot
  storage from `PH_MAX_*` default reservations. `ContactOverflowed()` reports only contact
  allocation failure.
- Quadtree item, query-result, and pair buffers grow on demand; truncation flags report
  allocation failure.
- ScreenFX active-effect slots grow from `RT_SCREENFX_MAX_EFFECTS`.
- Game UI text input, table rows/columns, dropdown options, and modal buttons/children
  grow from their historical defaults.

## Verification

- `ctest --test-dir build -R 'test_rt_physics2d|test_rt_quadtree|test_rt_screenfx|test_rt_gameui|test_runtime_classes_catalog|test_runtime_surface_audit' --output-on-failure`
- `./scripts/check_runtime_completeness.sh`

## Risks / open questions

- **Real-time allocation in the game loop** is the main hazard — grow amortized and
  off-hot-path, or preallocate from a capacity hint.
- **ABI/signature changes** if constructors gain a capacity parameter — keep a default
  overload so existing code compiles.
- **2D physics index width:** body IDs and pair scratch state currently assume small
  fixed counts in places. Treat this as a storage redesign, not a constant bump.
