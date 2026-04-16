# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### Overview

v0.2.5 continues the post-v0.2.4 cycle with five focused themes:

- **Runtime surface hardening** — owner-header discipline on the C runtime, typed IL return descriptors for Canvas accessors that produce `Pixels`, a magic-value guard on the `Canvas` object for defensive validation, a small correctness fix to the `ButtonGroup` selection state, and three new dynamic linker known-symbols (`uname`, `gethostname`, `sysctlbyname`) so `Viper.Machine.OS`/`Hostname` link cleanly.
- **GUI runtime expansion** — tab tooltips and title ellipsis, six new CodeEditor APIs (CanUndo/CanRedo, TabSize, WordWrap), pixel-position hit-testing (`GetLineAtPixel`/`GetColAtPixel`), `Widget.IsHovered` on the base widget, `SetVisible`/`IsVisible` for `Breadcrumb` and `Minimap`, HiDPI-aware tabbar and toolbar scaling, and a native-menubar zero-height layout fix.
- **GUI in-depth audit** — 13 verified bugs surfaced by a deep review of the widget toolkit, fixed and guarded by a dedicated regression suite (`test_vg_audit_fixes`), with a follow-on pass closing ownership and contract bugs in FileDialog, MenuBar/Toolbar, FindBar, CodeEditor folding/gutters, drag-and-drop hover state, and VideoWidget destruction. The original audit split was 3 P0 (dialog re-entrant UAF, tooltip dangling pointer, broken notification auto-dismiss), 5 P1 (UTF-8 find advance, tabbar scroll clamp, context-menu off-screen, dialog `set_on_close` user_data routing, codeeditor clamp split), 5 P2 (scrollbar div-by-zero, splitpane proportional clamp, scrollview hit-test gutter exclusion, flex non-stretch sizing, password-mask buffer).
- **Zia frontend & tooling** — four path-aware `Viper.Zia.Completion` APIs (`CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`) so completion resolves relative `bind` paths against the active file, and a Lowerer modularization that extracts binary-operator, call-argument, and collection lowering into three dedicated helper classes.
- **Demos & docs** — Pac-Man demo renamed and rebuilt as Crackman, Paint gains layers and undo/redo, ViperIDE wires up the new GUI APIs, all 10 demos built by `build_demos_mac.sh` carry tutorial-style annotations; new codemaps for the bytecode VM and graphics-disabled runtime stubs, plus clarifications to optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language.

#### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 8 | +8 |
| Source files | 2,869 | 2,877 | +8 |
| Production SLOC | ~450K | ~455K | ~+5K |
| Test SLOC | ~183K | ~184K | ~+1K |
| Demo SLOC | ~177K | ~184K | +~7K |

Counts produced by `scripts/count_sloc.sh` (`Production SLOC` = `src/` minus `src/tests/`). v0.2.5 is a small focused cycle: most growth is on the demo side (Crackman modularization + Paint feature pass) and the GUI runtime (audit fixes + widget surface expansion), with surgical edits on the runtime/IL side.

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

**Widget.Focus**

- `Viper.GUI.Widget.Focus()` promoted to the base widget runtime class (and mirrored on `CodeEditor`), so Zia code can call `editor.Focus()` directly to move keyboard focus. Previously every widget needed bespoke focus plumbing.

**Widget correctness pass**

- `TabBar`: close-click polling now stores a stable tab index instead of a tab pointer, so `GetCloseClickedIndex()` remains valid even when `auto_close` destroys the tab during the same click. Close-button hit testing now uses the full close-rect bounds, default tooltips continue to mirror renamed titles until explicitly overridden, hovered tabs surface their per-tab tooltip through the standard tooltip manager, modified (`" *"`) markers participate in width measurement, and `WasChanged()` is no longer spuriously primed on first poll.
- `Toolbar` / `StatusBar`: pointer hit testing now uses widget-local coordinates, fixing hover and click offsets when the widget is not positioned at `(0, 0)`. Toolbar overflow is now a real popup, backed by input capture and overlay painting, so hidden actions stay reachable. Top-level disabled menus now paint and behave as disabled, toolbar/menu pixel-icon setters create real image icons instead of reinterpret-casting pointers as glyph codes, and runtime `ToolbarItem.SetText()` updates invalidate layout/overflow bookkeeping immediately.
- `VBox` / `HBox`: flex layout now budgets child margins when distributing remaining space, eliminating overflow and clipping of later siblings.
- `SplitPane`: pane sizes are clamped non-negative during tiny resizes instead of allowing negative widths/heights.
- `MenuBar`: desktop dropdown width now expands to fit the widest visible item/shortcut row, and outside clicks close the menu without firing the currently highlighted action.
- `ScrollView`: auto-hide scrollbar selection now iterates until horizontal/vertical visibility stabilizes, covering the cross-axis case where one scrollbar forces the other. Viewport hit testing respects clipped ancestors, and scrollbar-thumb drags keep pointer capture until mouse-up even when released outside the widget.
- `ListBox`: virtual-mode painting now clips to the viewport before drawing overscanned cached rows.
- `TextInput.SetText`: programmatic text assignment no longer fires the user-edit `on_change` callback.
- `CodeEditor`: line-slot reuse now clears stale per-line syntax-color metadata during `SetText` and multi-line compaction. This fixes a ViperIDE crash on file open / language switch where `SetLanguage` could free a recycled `line->colors` pointer from a previous document shape. Word-wrap now drives cursor movement, scrollbar math, pixel hit-testing, `ScrollToLine`, and runtime cursor-pixel helpers instead of being paint-only, editor content is clipped to the viewport during paint, and scrollbar drags keep capture until mouse-up outside the widget. Hiding line numbers now collapses the line-number gutter completely, `SetLineNumberWidth()` is stored in character cells so it tracks font changes, and fold gutters/regions now render, toggle from gutter clicks, and hide folded body lines consistently across painting, scrolling, cursor clamps, and pixel-position helpers.
- `FindBar`: `SetVisible()` now routes through normal widget visibility handling, live `GetFindText()` / `GetReplaceText()` reads reflect the current input contents, and `Replace()` returns `0` when there is no bound editor or current match instead of reporting false success.
- `Widget` drag/drop: drag-over state now remains active while the pointer is stationary over a valid drop target, and an empty accepted-type list once again means "accept any type" instead of rejecting typeless drags.
- `VideoWidget`: new explicit `Destroy()` tears down the widget subtree and releases the owned `VideoPlayer` immediately; the object also hardens its failure paths and post-destroy method guards.
- `Tooltip`: tooltip panels now wrap long text, draw an opaque background/border, follow live tooltip-text changes on the hovered widget, and hide automatically when the hovered/anchored widget is hidden or disabled.
- `Minimap`: click-to-scroll reworked. New internal helpers (`minimap_document_line_count`, `minimap_line_from_local_y`, `minimap_scroll_editor_to_line`, `minimap_trimmed_line_bounds`) resolve a click's local-Y to a document line and scroll the bound editor there, handling blank-line trimming so the minimap maps to visible content rather than the raw buffer.
- `Toolbar` (overflow popup expansion): the overflow now uses the full `ContextMenu` plumbing — `toolbar_ensure_overflow_popup` / `rebuild_overflow_popup` / `sync_popup_capture` / `dismiss_overflow_popup` / `show_overflow_popup` / `forward_popup_event` — with input capture, overlay painting, and proper hover/click forwarding so hidden toolbar items behave identically to primary items.

**GUI in-depth audit fixes**

A focused review of the GUI subsystem surfaced 13 verified bugs spanning crashes, visible defects, and polish issues. All are fixed and each is guarded by a regression test in `src/lib/gui/tests/test_vg_audit_fixes.c`. Severity tags follow the audit plan.

- *P0 — `vg_dialog_close` re-entrancy guard.* New `closing_in_progress` flag plus snapshotted callback locals prevent double dispatch when an `on_result` handler calls `vg_dialog_close` again. The header gains `on_result_user_data` and `on_close_user_data` so each callback receives its own context (legacy `user_data` field preserved as an alias).
- *P0 — Tooltip dangling pointer.* New `vg_tooltip_manager_widget_destroyed()` API called from `vg_widget_destroy` clears the manager's `hovered_widget` and the active tooltip's `anchor_widget` when the underlying widget is freed, eliminating a use-after-free on the next hover update.
- *P0 — Notification auto-dismiss.* `vg_notification_manager_update` lazily stamps `created_at` on first observation instead of leaving it at 0, restoring duration-based dismissal (toasts were vanishing on the first frame).
- *P1 — Find/Replace UTF-8 advance.* `perform_search` advances by `match_len` (or 1 byte when zero) instead of `pos++`, keeping the cursor on UTF-8 codepoint boundaries and producing non-overlapping matches.
- *P1 — TabBar scroll clamp.* `tabbar_measure` re-clamps `scroll_x` against the new `total_width`, so removing tabs no longer strands remaining tabs offscreen.
- *P1 — ContextMenu off-screen positioning.* Screen-edge clamping moved from `vg_contextmenu_show_at` into `contextmenu_paint`, where the window handle is reliably available via the canvas argument; the never-set `impl_data` dependency is gone.
- *P1 — Dialog `set_on_close` user_data routing.* The dual-slot fix above; the previous setter silently dropped `user_data` whenever `on_result` was already registered.
- *P1 — `clamp_editor_position` split helpers.* New `clamp_editor_line` / `clamp_editor_col` static helpers let future selection-anchor code clamp axes independently. The atomic helper is preserved with documented behavior.
- *P2 — CodeEditor scrollbar div-by-zero.* `scroll_range > 0` guard on the scrollbar `scroll_ratio` divisor prevents NaN propagating into an `(int32_t)` cast when content exactly fits the viewport.
- *P2 — SplitPane proportional min clamp.* New `resolve_first_size()` helper distributes available space proportionally when `min_first + min_second` exceeds available, instead of silently collapsing the first pane to zero. Used by both horizontal and vertical branches.
- *P2 — ScrollView hit-test gutter exclusion.* `vg_widget_hit_test` excludes a 16-px scrollbar-gutter strip from child recursion when the parent is a `VG_WIDGET_SCROLLVIEW`, so scrollbar clicks reach the scrollview rather than passing through to a child whose bounds extend underneath.
- *P2 — Flex non-stretch sizing.* `vg_layout` only subtracts margins from cross-axis size in `VG_ALIGN_STRETCH` mode; non-stretch alignments preserve the child's measured cross size, fixing a few-pixel clip on descenders/icons.
- *P2 — TextInput password mask buffer.* Password-mode mask render allocates dynamically (256-byte stack buffer for short input, `malloc` beyond) so long pasted secrets are no longer silently capped at 1023 asterisks.

**Per-widget tick (animation hook)**

- New `rt_gui_tick_widget_tree` walks the widget tree from `rt_gui_app_render` once per frame with a clamped `dt`, dispatching `vg_textinput_tick` (cursor blink), `vg_progressbar_tick` (animation), and `vg_codeeditor_tick`. Visible command palettes are also ticked. This replaces the previous per-widget time tracking with a single clock domain.

**FileDialog layout pass**

- Layout metrics extracted into `FILEDIALOG_*` named constants (title height, sidebar width, row height, button dimensions, save-mode bottom strip).
- New `get_parent_screen_origin` helper resolves anchor coordinates so the dialog positions correctly inside nested containers.
- Substantial layout rewrite for HiDPI scaling and the save-mode bottom strip.
- Accepted path lists are now snapshotted on successful `Show()` / `OpenMultiple()` completion instead of aliasing backend-owned arrays, fixing repeat-show and destroy-time lifetime bugs for multi-select dialogs.

**Spinner / Image surface expansion**

- Spinner gains additional size variants and theming hooks; Image gains additional fit modes and colorization paths. Both pick up vtable-coverage assertions in `test_vg_widgets_new.c`.

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

**Types display-string unification**

- `Types.cpp` / `Types.hpp` consolidate scattered type-to-string formatters into one recursive `appendTypeString(ss, type, developerFacing)` helper that handles every `TypeKindSem` case (Integer / Number / Boolean / String / Byte / Unit / Void / Error / Ptr / Optional / generic type args) and renders type arguments via `[T1, T2, ...]`. A new `toDisplayString()` accessor is the user-facing entry point.
- The `developerFacing` flag lets diagnostic messages show terser user-grade names (e.g., `String?`) while internal debug output can still request the fully-qualified form. Removes the drift between error-message formatting and debug-dump formatting.

**Sema error-message fixes**

- Every Sema error that names a type (type mismatch, member-access on Optional without null check, missing member, cast failure, pattern-literal vs scrutinee mismatch, tuple index on non-tuple) now renders through `toDisplayString()`, so users see `String?` instead of `Optional<String>` and `List[Integer]` instead of the internal representation.
- New check for inference-from-`null`: `var x = null;` without an explicit type annotation now fails with "Cannot infer type from null initializer; add an explicit type annotation such as 'String?', 'MyType', or 'GUI.Font'". Previously this silently produced `Optional<Unknown>` and downstream errors were confusing.
- Optional member access (`opt.field` instead of `opt?.field` or `opt!.field`) produces a targeted error naming the Optional type rather than a generic "has no member" message.
- Tighter assignment type-conversion check: mixed Unknown-type operands no longer bypass the convertibility assertion.

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
- Extended GUI tier coverage with regression cases for TabBar close-index lifetime/full-rect hit testing, toolbar/statusbar local hit testing, VBox/HBox flex margin budgeting, SplitPane tiny-resize clamping, ScrollView cross-axis auto-hide, ListBox virtual clipping, Toolbar overflow popup activation, `ToolbarItem.SetText()` invalidation, fold-gutter click toggling, silent `TextInput.SetText`, and CodeEditor line-slot metadata reuse during `SetText` / deletion compaction.
- Added focused GUI regressions for hidden-widget focus/capture cleanup, tab-tooltip hover propagation, ScrollView and CodeEditor scrollbar drag capture, CodeEditor wrap-aware runtime pixel helpers, folded-line pixel mapping, line-number-width font tracking, live FindBar text/no-op replace semantics, menu/toolbar pixel-icon contracts, and `TabBar.WasChanged()` edge triggering.
- New `test_vg_audit_fixes` regression suite — 13 tests, one per audit fix (dialog re-entry guard, two-slot user_data routing, tooltip dangling-pointer cleanup, notification lazy timestamp, find/replace UTF-8 advance, tabbar scroll clamp, contextmenu independence from `impl_data`, codeeditor cursor clamp, scrollbar finite scroll, splitpane proportional clamp, scrollview hit-test gutter, flex non-stretch sizing, password-mask long content). Registered under the `tui` label, runs in 0.01 s.
- Extended `RTGuiRuntimeTests.c` coverage with CodeEditor tab-size clamping, word-wrap toggle, `CanUndo`/`CanRedo`, folded-line pixel helpers, line-number-width tracking, live FindBar text/no-op replace checks, menu/toolbar pixel-icon checks, and `test_tabbar_close_click_index_survives_auto_close`.
- Extended `RTVideoWidgetContractTests.cpp` with explicit destroy coverage for widget-tree teardown and owned-player release.
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
| `2fe3b9a1e` | 2026-04-16 | `chore(demos,gui)`: tutorial-comment sweep across 8 demos, GUI widget correctness pass, Widget.Focus runtime API |
| `6c600dbd9` | 2026-04-16 | `chore(gui,runtime,zia)`: GUI in-depth audit fixes (13 bugs + regression suite), per-widget tick, FileDialog/Spinner/Image expansion, lowerer refactor |

<!-- END DRAFT -->
