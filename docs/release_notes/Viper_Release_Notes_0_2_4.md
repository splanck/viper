# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 focuses on game engine test coverage, demo quality, and documentation accuracy.

- **Game Engine Test Coverage** — 4 new unit test suites covering Canvas frame helpers, SaveData persistence, Canvas text layout, and DebugOverlay.
- **Demo Refactoring** — Pac-Man refactored to use StateMachine and ButtonGroup as reference implementation.
- **Documentation Fixes** — StateMachine API docs corrected, canvas.md and game.md verified.

#### By the Numbers

| Metric | Value |
|--------|-------|
| Test count | 1,355 |

---

### Test Coverage Improvements

Four new unit test suites added for previously untested game engine runtime classes:

#### Canvas Frame Helpers (1 test binary, 14 assertions)

Tests for `SetDTMax()` and `BeginFrame()` — the Canvas frame management methods added in v0.2.3.

- **Null safety** — all functions safe with NULL canvas
- **DeltaTime clamping** — upper bound, lower bound (clamps to 1), negative dt, exact boundaries, within range
- **SetDTMax toggle** — enable clamping with positive max, disable with 0, negative treated as 0
- **BeginFrame logic** — returns 1 when `ShouldClose == 0`, returns 0 when closing or NULL

Uses fake `rt_canvas` structs (heap-allocated, no display required) for struct-level testing via conditional `VIPER_ENABLE_GRAPHICS` compilation.

#### SaveData Persistence (1 test binary, 17 tests)

Tests for the `SaveData` key-value persistence system (JSON-backed, cross-platform paths).

- **Null safety** — all 10 functions safe with NULL handle
- **Constructor validation** — empty game name traps
- **Key-value CRUD** — SetInt/GetInt, SetString/GetString with overwrite
- **Type mismatch defaults** — GetString on int key returns default (and vice versa)
- **Type overwrite** — int key overwritten with string value
- **HasKey / Remove / Clear / Count** — full CRUD lifecycle
- **Path computation** — save path contains game name and `save.json`
- **Save/Load round-trip** — 5 entries (int + string) survive JSON serialization
- **Load nonexistent** — returns 0, no crash
- **Save overwrite** — second save replaces first, load sees latest
- **Edge cases** — empty key ignored, JSON-special characters in values (`"`, `\`, `\n`), INT64_MAX/MIN

Uses PID-unique game names to avoid collisions in parallel test runs. Cleans up save files after each test.

#### Canvas Text Layout (1 test binary, 14 tests)

Tests for `TextCentered()`, `TextRight()`, `TextCenteredScaled()`, and `TextScaledWidth()`.

- **Null safety** (7 tests) — null canvas, null text, zero/negative scale
- **TextScaledWidth math** (4 tests) — `strlen * 8 * scale` formula with basic, scaled, empty, and null inputs
- **Fake canvas integration** (3 tests) — layout functions execute without crash on canvases with NULL `gfx_win` (drawing is no-op, computation path exercised)

#### DebugOverlay (1 test binary, 20 tests)

Tests for the `DebugOverlay` development debug panel.

- **Null safety** — all 11 functions safe with NULL handle
- **Enable/Disable/Toggle** — disabled by default, toggle flips state
- **FPS rolling average** (6 tests) — zero frames, steady 60 FPS, steady 30 FPS, rolling transition, partial fill (4 frames), zero dt (division-by-zero guard)
- **Watch variables** (6 tests) — add/update, duplicate updates in-place, clear all, max capacity (17th silently ignored), null name, slot reuse after removal
- **Draw safety** — null canvas, disabled overlay

---

### Demo Refactoring

#### Pac-Man — StateMachine + ButtonGroup Reference Implementation

Refactored the Pac-Man game (`examples/games/pacman/game.zia`) to replace manual integer state management with the `Viper.Game.StateMachine` and `Viper.Game.ButtonGroup` runtime classes.

**StateMachine integration:**
- 8 game states registered: Menu, Playing, Paused, GameOver, Win, Ready, Dying, HighScores
- All `gameState = config.STATE_X` assignments → `sm.Transition(config.STATE_X)`
- All `gameState == config.STATE_X` comparisons → `sm.IsState(config.STATE_X)`
- `sm.Update()` and `sm.ClearFlags()` called each frame

**ButtonGroup integration:**
- 3 menu items (Start, High Scores, Quit) registered as button IDs 0, 1, 2
- Manual wraparound arithmetic → `menuBtns.SelectNext()` / `menuBtns.SelectPrev()`
- Menu highlighting → `menuBtns.IsSelected(id)` instead of `menuSelection == id`

---

### Documentation Fixes

- **StateMachine API** (game.md) — corrected stale callback-based API (`AddState(id, enter, update, exit)`, `SetState(id)`) to match actual implementation (numeric state IDs with `AddState(id)`, `Transition(id)`, `IsState(id)`, property-based edge flags). Added Zia example and reference implementation link.
- **ButtonGroup** (game.md) — added reference implementation link to Pac-Man demo.
- **Canvas** (canvas.md) — `last-verified` updated.
- **SaveData** (persistence.md) — `last-verified` updated.
- **DebugOverlay** (debug.md) — `last-verified` updated.

---

### New Tests

| Category | Tests | Description |
|----------|:-----:|-------------|
| Canvas frame helpers | 1 | SetDTMax clamping, BeginFrame logic, null safety (14 assertions) |
| SaveData persistence | 1 | Key-value ops, save/load round-trip, JSON escaping, null safety (17 tests) |
| Canvas text layout | 1 | TextCentered/Right/CenteredScaled null safety, TextScaledWidth math (14 tests) |
| DebugOverlay | 1 | FPS rolling avg, watch CRUD, enable/toggle, null safety (20 tests) |

**Total new tests:** 4 test binaries, 65 individual test cases.

---

### Files Changed

| File | Change |
|------|--------|
| `src/tests/runtime/RTCanvasFrameTests.cpp` | New — Canvas SetDTMax/BeginFrame tests |
| `src/tests/runtime/RTSaveDataTests.cpp` | New — SaveData persistence tests |
| `src/tests/runtime/RTCanvasTextLayoutTests.cpp` | New — Canvas text layout tests |
| `src/tests/runtime/RTDebugOverlayTests.cpp` | New — DebugOverlay tests |
| `src/tests/unit/CMakeLists.txt` | Register 4 new test targets |
| `examples/games/pacman/game.zia` | StateMachine + ButtonGroup refactor |
| `examples/games/pacman/main.zia` | Updated feature list |
| `docs/viperlib/game.md` | Fixed StateMachine docs, added reference links |
| `docs/viperlib/game/debug.md` | Updated last-verified |
| `docs/viperlib/game/persistence.md` | Updated last-verified |
| `docs/viperlib/graphics/canvas.md` | Updated last-verified |

---

### Cumulative Statistics

| Metric              | v0.2.3    | v0.2.4 (draft) | Change     |
|---------------------|-----------|----------------|------------|
| Test Count          | 1,351     | 1,355          | +4         |
