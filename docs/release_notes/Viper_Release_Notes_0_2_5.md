# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### Overview

v0.2.5 continues the post-v0.2.4 cycle with five focused themes:

- **Runtime surface hardening** — owner-header discipline on the C runtime, typed IL return descriptors for Canvas accessors that produce `Pixels`, a magic-value guard on the `Canvas` object for defensive validation, a small correctness fix to the `ButtonGroup` selection state, and three new dynamic linker known-symbols (`uname`, `gethostname`, `sysctlbyname`) so `Viper.Machine.OS`/`Hostname` link cleanly.
- **GUI runtime expansion** — tab tooltips and title ellipsis, six new CodeEditor APIs (CanUndo/CanRedo, TabSize, WordWrap), pixel-position hit-testing (`GetLineAtPixel`/`GetColAtPixel`), `Widget.IsHovered` on the base widget, `SetVisible`/`IsVisible` for `Breadcrumb` and `Minimap`, HiDPI-aware tabbar and toolbar scaling, and a native-menubar zero-height layout fix.
- **Zia frontend & tooling** — four path-aware `Viper.Zia.Completion` APIs (`CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`) so completion resolves relative `bind` paths against the active file, and a Lowerer modularization that extracts binary-operator, call-argument, and collection lowering into three dedicated helper classes.
- **Demo breadth and polish** — Pac-Man demo renamed and rebuilt as Crackman, Paint app gains layers and undo/redo, ViperIDE wires up new GUI APIs, ViperSQL and Xenoscape annotated as learnable tutorials.
- **Review-readiness documentation** — new codemaps for the bytecode VM and graphics-disabled runtime stubs, plus clarifications to the optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language.

#### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 6 | +6 |
| Source files | 2,869 | 2,876 | +7 |
| Production SLOC | ~450K | ~451K | ~+1K |
| Test SLOC | ~183K | ~184K | ~+1K |
| Demo SLOC | ~177K | ~184K | +~7K |

Counts produced by `scripts/count_sloc.sh` (`Production SLOC` = `src/` minus `src/tests/`). v0.2.5 is a small focused cycle: most growth is on the demo side (Crackman modularization + Paint feature pass), with surgical edits on the runtime/IL side.

---

### Runtime Surface Hardening

**AchievementTracker string ABI**

- `Viper.Game.AchievementTracker` now threads runtime string handles end-to-end instead of raw C strings. The tracker retains and releases `rt_string` values consistently and draws notifications through the real graphics string ABI rather than transient C-string conversions.
- Contract tests in `src/tests/runtime/RTAchievementTests.cpp` extended to cover the handle lifecycle.
- New AArch64 native smoke probe for achievement drawing at `src/tests/e2e/achievement_draw_native_probe.zia` + `test_achievement_draw_native.sh`, wired into ctest.

**Runtime header discipline**

- `rtgen` picks up runtime headers and `RuntimeSurfacePolicy` as CMake dependencies, so changes to the authoritative runtime surface invalidate the generated bindings.
- Canvas3D internal helper declarations moved out of ad-hoc local externs and into a new owning `rt_canvas3d_internal.h`. Callers (`rt_canvas3d.c`, `rt_canvas3d_overlay.c`, `rt_model3d.c`, `rt_scene3d.c`, `rt_scene3d_vscn.c`, `rt_fbx_loader.c`, `rt_skeleton3d.c`, `rt_morphtarget3d.c`, `rt_animcontroller3d.c`) include the owning header instead of redeclaring prototypes locally.
- Stray `extern rt_*` forward declarations across `rt_canvas.c`, `rt_countdown.c`, `rt_gui_app.c`, `rt_input.c`, `rt_sprite.c`, and the game runtime (`rt_config.c`, `rt_debugoverlay.c`, `rt_entity.c`, `rt_leveldata.c`, `rt_lighting2d.c`, `rt_raycast2d.c`, `rt_scenemanager.c`, `rt_animstate.c`) replaced with includes of the owning runtime headers.

**Canvas object guard + typed `Pixels` returns**

- `rt_canvas` gains an `RT_CANVAS_MAGIC` guard field and an `rt_canvas_checked()` helper in `src/runtime/graphics/rt_graphics_internal.h`. The Canvas `GetPixel` / `CopyRect` / `SaveBmp` / `SavePng` paths in `rt_drawing.c` route every incoming `void *` through this validator so a caller passing a non-Canvas object fails safely (returns 0 / NULL) instead of reinterpret-casting arbitrary memory. Paired edits to `rt_canvas.c`, `rt_gui_filedialog.c`, `rt_gui_system.c`, `rt_spritebatch.c` pick up the same validator where applicable.
- `src/il/runtime/runtime.def` now types `Canvas.CopyRect` and `Canvas.Screenshot` as `obj<Viper.Graphics.Pixels>` in both the `RT_FUNC` and `RT_METHOD` rows. The IL type system now carries the `Pixels` return type through to callers, eliminating the previous `obj` erasure.

**ButtonGroup selection correctness**

- `rt_buttongroup`: removing the currently selected button now clears the active selection and marks the selection as changed. Previously `selected_index` could stay stale and `is_selected()` could return `true` for an already-removed id. Header comment updated to document the new invariant.

**Dynamic linker known-symbols**

- `src/codegen/common/linker/DynamicSymbolPolicy.hpp` adds `uname`, `gethostname`, and `sysctlbyname` to the known dynamic-symbol list (Linux/macOS). These are required by `Viper.Machine.OS` / `Viper.Machine.Hostname` and by the IDE's macOS font-fallback chain; without them the linker produced static-resolution failures.

These moves together close a long-standing layering smell (runtime callers no longer advertise their own C ABI for functions they don't own) and extend the runtime-object safety net from "pointer non-null" to "pointer is actually the object type we expect."

---

### GUI Runtime Expansion

**Tab tooltips and title ellipsis**

- `Tab.SetTooltip(text)` added to the `Viper.GUI.Tab` runtime class. New tabs default their tooltip to the tab title. When a tab title exceeds the available pixel width, the tabbar renders it with trailing `...` ellipsis while the full text remains available on hover via the tooltip.

**CodeEditor new APIs**

- `CodeEditor.CanUndo` / `CodeEditor.CanRedo` — query whether the undo/redo stack has an available entry.
- `CodeEditor.SetTabSize(n)` / `CodeEditor.GetTabSize` — configurable tab width in spaces (clamped 1–16).
- `CodeEditor.SetWordWrap(flag)` / `CodeEditor.GetWordWrap` — display-only word wrapping toggle; enabling resets horizontal scroll to zero.
- `CodeEditor.AddHighlight` gains a 5th `color` parameter (previously the color was implicit via token type). All known callers updated.
- All new functions include graphics-disabled stubs.

**HiDPI tabbar and toolbar scaling**

- Tabbar: `tab_height`, `tab_padding`, `close_button_size`, and `max_tab_width` all scale by `theme.ui_scale` at creation time; close-button gap scales proportionally.
- Toolbar: new `toolbar_ui_scale()` and `get_scaled_icon_pixels()` helpers replace ad-hoc per-site scale calculations. Text-only buttons no longer reserve icon width. Label vertical centering uses font metrics instead of raw `font_size` multiplication. `item_spacing` scales with `ui_scale`.

**Native menubar zero-height fix**

- When `native_main_menu` is active (macOS), the managed menubar widget now measures to zero height so it doesn't consume layout space alongside the system menu bar.

**Pixel-position hit-testing**

- `Viper.GUI.CodeEditor.GetLineAtPixel(y)` and `GetColAtPixel(x, y)` map screen-relative pixel coordinates to text positions. Required by mouse-driven hover tooltips so they track the cursor under the mouse rather than the caret.
- `Viper.GUI.Widget.IsHovered()` promoted to the base widget class — any widget can now be queried for hover state without bespoke tracking.

**Breadcrumb and Minimap visibility**

- `Viper.GUI.Breadcrumb.SetVisible` / `IsVisible` and `Viper.GUI.Minimap.SetVisible` / `IsVisible` added so apps can hide chrome elements when there's no relevant content (e.g., the breadcrumb when no file is open).

**Widget correctness pass**

- `TabBar`: close-click polling now stores a stable tab index instead of a tab pointer, so `GetCloseClickedIndex()` remains valid even when `auto_close` destroys the tab during the same click. Close-button hit testing now uses the full close-rect bounds, and default tooltips continue to mirror renamed titles until explicitly overridden.
- `Toolbar` / `StatusBar`: pointer hit testing now uses widget-local coordinates, fixing hover and click offsets when the widget is not positioned at `(0, 0)`. Toolbar overflow is now a real popup, backed by input capture and overlay painting, so hidden actions stay reachable.
- `VBox` / `HBox`: flex layout now budgets child margins when distributing remaining space, eliminating overflow and clipping of later siblings.
- `SplitPane`: pane sizes are clamped non-negative during tiny resizes instead of allowing negative widths/heights.
- `ScrollView`: auto-hide scrollbar selection now iterates until horizontal/vertical visibility stabilizes, covering the cross-axis case where one scrollbar forces the other.
- `ListBox`: virtual-mode painting now clips to the viewport before drawing overscanned cached rows.
- `TextInput.SetText`: programmatic text assignment no longer fires the user-edit `on_change` callback.

---

### Zia Frontend & Tooling

**Path-aware completion APIs**

- Four new `Viper.Zia.Completion` overloads take a `sourcePath` parameter alongside the source text: `CompleteForFile(text, path, line, col)`, `CheckForFile(text, path)`, `HoverForFile(text, path, line, col)`, `SymbolsForFile(text, path)`. Previously the engine had no way to resolve relative `bind` paths, so any non-trivial multi-file project lost completion accuracy as soon as the user opened a file outside the IDE's working directory.
- The original argument-less overloads remain for in-memory snippets.
- Implemented in `rt_gui_features.c` with stubs in `rt_zia_completion_stub.c`; plumbed end-to-end through `CompilerBridge.cpp`.

**Lowerer modularization**

- The monolithic `Lowerer_Expr_Binary.cpp`, `Lowerer_Expr_Call.cpp`, and `Lowerer_Expr_Collections.cpp` shed roughly 540 LOC into three focused helper classes:
  - `BinaryOperatorLowerer` — arithmetic, comparison, string concatenation, bitwise.
  - `CallArgumentLowerer` — named-argument ordering, default parameters, variadic packing.
  - `CollectionLowerer` — list/set/map literals, tuples, index access.
- The `Lowerer` class befriends each helper and exposes the necessary state hooks; the entry-point methods now delegate. No behavioural change, but expression-lowering surface area is now navigable in three discrete files instead of one ~1200-LOC file.

---

### Demos

- Pac-Man demo renamed **Crackman** with modular layout, persistent XP, and tile-aligned actor speeds; Chess gains matching UI decomposition; Paint gains layers, undo/redo, and zoom; ViperIDE gains per-file IntelliSense, mouse-position hover, and custom-font support; ViperSQL and Xenoscape annotated as learnable tutorials.

### Documentation

- New codemaps for the bytecode VM and graphics-disabled runtime stubs; API docs updated for Toolbar, Tab, and CodeEditor signatures; minor corrections across `docs/` for the Crackman rename and review-readiness wording.

---

### Build

- `src/buildmeta/VERSION` bumped from `0.2.4-dev` → `0.2.5-snapshot` to close out v0.2.4 and open the v0.2.5 development cycle.
- `src/lib/gui/CMakeLists.txt` now keys GUI test targets off `VIPER_BUILD_TESTING`, matching the rest of the tree. This fixes a long-standing registration bug where the GUI tier tests existed in source but were silently absent from generated CTest targets.

---

### Tests

- `zia_smoke_crackman` — runs `examples/games/pacman/smoke_probe.zia`, expects `RESULT: ok`, 30 s timeout, labels `zia;smoke`.
- `test_achievement_draw_native` — AArch64 native smoke probe exercising the runtime-string-handle draw path for achievement notifications.
- Extended `RTAchievementTests.cpp` contract coverage for the `rt_string` retain/release lifecycle.
- New `src/tests/unit/runtime/TestConfig.cpp` covering the `Viper.Game.Config` runtime class, wired into `src/tests/CMakeLists.txt` + `src/tests/unit/CMakeLists.txt`.
- Three new GUI tests in `test_vg_tier2_fixes.c`: `tabbar_metrics_follow_theme_scale` (HiDPI scaling), `tab_tooltip_can_be_replaced`, `native_menubar_measures_to_zero_height`.
- Extended GUI tier coverage with regression cases for TabBar close-index lifetime/full-rect hit testing, toolbar/statusbar local hit testing, VBox/HBox flex margin budgeting, SplitPane tiny-resize clamping, ScrollView cross-axis auto-hide, ListBox virtual clipping, Toolbar overflow popup activation, and silent `TextInput.SetText`.
- Extended `RTGuiRuntimeTests.c` CodeEditor multicursor test with tab-size clamping, word-wrap toggle, and `CanUndo`/`CanRedo` assertions.
- Added `test_tabbar_close_click_index_survives_auto_close` to `RTGuiRuntimeTests.c`.
- New `tests/zia_runtime/41_runtime_reference_types.zia` exercising `Viper.Network.Url.Parse`, property access (`Host`, `Path`), `Url.Clone()` returning `Url?`, and `!` unwrap on the optional.
- New `MacPlannerMapsMachineAndHostSyscallsToLibSystem` case in `test_platform_import_planners.cpp` covering the new `uname` / `gethostname` / `sysctlbyname` symbols routed through the macOS dynamic-symbol policy.

---

### Commits Included

| Commit | Date | Summary |
|---|---|---|
| `d58df4f98` | 2026-04-14 | `chore(demos,build,docs)`: pacman → crackman binary rename, chess + crackman UI polish, VERSION → 0.2.5-snapshot |
| `74f4ec4c7` | 2026-04-14 | `feat(crackman)`: rename Pac-Man demo to Crackman, split into session/progression/frontend, add smoke probe and audio banks |
| `8126432f6` | 2026-04-15 | Harden runtime surface and Crackman progression |
| `a34c3d555` | 2026-04-15 | `chore`: annotate vipersql/xenoscape demos, expand paint app, runtime polish |
| `06c33c339` | 2026-04-15 | `feat(gui)`: tab tooltips, CodeEditor APIs, HiDPI toolbar/tabbar polish |
| `d54e03b9d` | 2026-04-16 | `feat(viperide,zia,gui)`: per-file IntelliSense, pixel-position hover, lowerer refactor, custom fonts |

<!-- END DRAFT -->
