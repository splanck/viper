# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### Overview

v0.2.5 is a polish-and-hardening cycle concentrated on the runtime, the GUI widget toolkit, and the 3D graphics stack, with smaller touches to the IL, Zia frontend, linker, and build system.

- **Runtime surface hardening** — owner-header discipline across the C runtime, typed IL return descriptors for `Canvas` accessors, a magic-value guard on the `Canvas` object, `ButtonGroup` selection correctness, and runtime-string-handle threading in `AchievementTracker`.
- **Graphics runtime** — parallax camera rendering, UTF-8 codepoint iteration for bitmap fonts and drawing, sprite/spritebatch/spritesheet refinements, tilemap polish, and scene-graph touches.
- **Graphics3D correctness pass** — terrain/heightfield normal math, skeletal animation hierarchy and bone-index validation, crossfade bind-pose TRS decomposition, capsule collision Y-only contract, navmesh edge hashing, Canvas3D 2D/3D state consistency, morph weight clamp, particle spawn catchup, and Gerstner wave direction.
- **GUI widget toolkit** — broad widget correctness pass plus new APIs on CodeEditor (word-wrap driving all coord math, fold gutters, Can/Redo/TabSize), TabBar (tooltips, ellipsis, stable close-index), Toolbar/MenuBar (overflow popup, disabled-menu rendering, pixel-icon contracts), FindBar (live reads, SetVisible), ScrollView (auto-hide stabilization, drag capture), Tooltip (multi-line wrap, destroy notify), Dialog (re-entrancy, dual user_data), HiDPI scaling across tabbar/toolbar, and several lifetime fixes (dialog use-after-free, tooltip dangling pointer, notification auto-dismiss).
- **Platform input** — macOS Fn+arrow translation no longer intercepts bare arrow keys; mouse wheel payload no longer destroyed by coordinate localization (mouse/wheel union aliasing).
- **Zia frontend** — path-aware `Viper.Zia.Completion` APIs, Lowerer modularization into three helper classes, unified type display strings, and tightened Sema error messages.
- **Linker** — `uname`, `gethostname`, `sysctlbyname` added to the dynamic-symbol policy so `Viper.Machine.OS` / `Hostname` link cleanly.
- **Demos & docs** — Pac-Man renamed and rebuilt as Crackman, Paint gains layers/undo/redo, ViperIDE wires up the new GUI APIs, all ten demos built by `build_demos_mac.sh` carry tutorial-style annotations; new codemaps for the bytecode VM and graphics-disabled runtime stubs, plus clarifications to optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language.

#### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 11 | +11 |
| Source files | 2,869 | 2,877 | +8 |
| Production SLOC | ~450K | ~458K | ~+8K |
| Test SLOC | ~183K | ~185K | ~+2K |
| Demo SLOC | ~177K | ~184K | +~7K |

Counts produced by `scripts/count_sloc.sh` (`Production SLOC` = `src/` minus `src/tests/`). Growth concentrates on the demo side (Crackman modularization + Paint feature pass) and the GUI runtime, with surgical edits across the rest of the tree.

---

### Runtime (`src/runtime/`)

**Runtime header discipline.** `rtgen` picks up runtime headers and `RuntimeSurfacePolicy` as CMake dependencies, so changes to the authoritative runtime surface invalidate the generated bindings. Canvas3D internal helper declarations moved out of ad-hoc local externs and into a new owning `rt_canvas3d_internal.h`; callers (`rt_canvas3d.c`, `rt_canvas3d_overlay.c`, `rt_model3d.c`, `rt_scene3d.c`, `rt_scene3d_vscn.c`, `rt_fbx_loader.c`, `rt_skeleton3d.c`, `rt_morphtarget3d.c`, `rt_animcontroller3d.c`) include the owning header instead of redeclaring prototypes locally. Stray `extern rt_*` forward declarations across `rt_canvas.c`, `rt_countdown.c`, `rt_gui_app.c`, `rt_input.c`, `rt_sprite.c`, and the game runtime (`rt_config.c`, `rt_debugoverlay.c`, `rt_entity.c`, `rt_leveldata.c`, `rt_lighting2d.c`, `rt_raycast2d.c`, `rt_scenemanager.c`, `rt_animstate.c`) replaced with includes of the owning runtime headers.

**Canvas (2D).** `rt_canvas` gains an `RT_CANVAS_MAGIC` guard field and an `rt_canvas_checked()` helper. The `GetPixel` / `CopyRect` / `SaveBmp` / `SavePng` paths in `rt_drawing.c` route every incoming `void *` through this validator so callers passing a non-Canvas object fail safely (0 / NULL) instead of reinterpret-casting arbitrary memory. Paired edits to `rt_gui_filedialog.c`, `rt_gui_system.c`, `rt_spritebatch.c` pick up the same validator. New `rt_canvas_next_codepoint` lets text measurement and draw walk UTF-8 codepoints rather than raw bytes, fixing glyph picks for multi-byte characters.

**Pixels.** `Canvas.CopyRect` and `Canvas.Screenshot` are now typed `obj<Viper.Graphics.Pixels>` in `src/il/runtime/runtime.def` (both `RT_FUNC` and `RT_METHOD` rows), eliminating the previous `obj` erasure through the IL type system. Minor transform-helper cleanup in `rt_pixels_transform.c`.

**Camera.** New `camera_draw_parallax_transformed` applies zoom, rotation, and scroll factors to each parallax layer independently with integer-floor-div tile wrapping so the ground scroll stays seamless at any zoom level.

**BitmapFont.** New `bf_next_codepoint` UTF-8 iterator and an owning `bf_release_font` release path; text measure/draw now walk codepoints, and old font objects are correctly released when replaced.

**Sprite / SpriteBatch / SpriteSheet.** Batching lifecycle and sheet indexing refinements; `rt_spritebatch.c` picks up the Canvas magic-guard validator.

**Tilemap / Scene.** Tilemap rendering polish; `rt_scene` surface refinements for scene-graph parent/child invariants.

**Graphics3D — Canvas3D.** `Begin2D` now caches the full V·P product (matching `Begin3D`) instead of just the projection. Overlay code that unprojects through `cached_vp` sees consistent semantics across modes; for an identity 2D view the numeric result equals projection, but the shape of the math matches 3D. Internal header (`rt_canvas3d_internal.h`) picks up additional surface bits used by the morph and material paths.

**Graphics3D — Camera3D.** New input sanitizers — `sanitize_aspect` (≥1e-6), `sanitize_clip_planes` (near ≥ 0.1, far ≥ near + 0.1 with a 1000-unit default), `sanitize_fov` ([1°, 179°]), and `sanitize_ortho_size` (≥1e-6) — normalize pathological values before they reach the projection builders. Degenerate `look_at` (eye == target) falls back to the default forward axis while *preserving* the camera translation, instead of returning an identity matrix that also zeroed the camera position.

**Graphics3D — Mesh3D.** Reference-slot ownership is now explicit. New `mesh_assign_ref` / `mesh_release_ref` helpers replace ad-hoc pointer manipulation on the skeleton / morph-target / material back-references; assignments retain-then-release and releases free on zero, closing a class of use-after-free and double-release bugs on mesh reassignment. The earlier bone-index `uint8_t` range check is retained.

**Graphics3D — Skeleton3D & Animation.** `rt_skeleton3d_add_bone` rejects `parent_index` values below `-1` (previously only the upper bound was checked, so `-2`, `-100`, etc. survived validation and corrupted the hierarchy). `rt_mesh3d_set_bone_weights` now range-checks each influence against `VGFX3D_MAX_BONES` before the `uint8_t` cast — previously the cast silently wrapped `256` → `0` and `-1` → `255`, driving the skinning palette with the wrong bone. Out-of-range indices drop the influence (weight 0). In the animation player, the crossfade bind-pose fallback decomposes the full 4×4 bind-pose matrix into TRS (scale from column magnitudes, rotation via Shepperd's method on the orthonormalized basis, translation from column 3) instead of snapping to identity rotation and unit scale when the target animation has no channel for a bone.

**Graphics3D — MorphTarget3D.** `rt_morphtarget3d_set_weight` clamps weights to `[-1, 1]` so callers can't silently over-extrude vertices past the target mesh. Packed position/normal delta arrays (`packed_pos_deltas` / `packed_nrm_deltas`) are now rebuilt lazily, gated on a generation counter: `morphtarget_touch_payload` bumps the generation when shapes are added or deltas edited; weight-only changes skip the rebuild. Backends that prefer GPU-side morphing (Metal, OpenGL, D3D11) consume the shared packed buffer directly.

**Graphics3D — Backends.** `vgfx3d_backend.h` plus the four concrete backends (`vgfx3d_backend_d3d11.c`, `vgfx3d_backend_metal.m`, `vgfx3d_backend_opengl.c`, `vgfx3d_backend_sw.c`) extend their morph-handoff surface to accept the shared packed-delta buffers from `rt_morphtarget3d`. `vgfx3d_backend_prefers_gpu_morph` is formalized on the selector; the software backend gets matching CPU-fallback paths so feature behavior stays uniform across all four backends.

**Graphics3D — Lights / PostFX / Scene.** `rt_light3d` refinements for colour/intensity initialization and finalizer plumbing so lights are safely retained/released through scene-graph mutations. `rt_postfx3d` picks up a small header/surface touch. `rt_scene3d` gains reference-handling refinements matching the new mesh-reference pattern.

**Graphics3D — Physics3D / Collider3D.** The heightfield collider normal formula now multiplies the Y component by both horizontal scales and carries the perpendicular horizontal scale on each lateral component; this fixes tilted lighting and misaligned physics normals on non-uniformly scaled heightfields. `capsule_axis_endpoints` documents its Y-only contract; an opt-in `RT_PHYSICS3D_STRICT_CAPSULE_AXIS` build flag traps on non-identity capsule orientation so the limitation becomes visible instead of silently corrupting collision.

**Graphics3D — NavMesh3D.** Edge keys switched from `lo * 1_000_000 + hi` to a bit-packed `(lo << 32) | hi`. The old formula collided on meshes with any vertex index ≥ 1M — e.g. edges `(1, 2000000)` and `(2, 1000000)` both hashed to `3_000_000`, falsely marking unrelated triangles adjacent and silently breaking pathfinding.

**Graphics3D — Terrain3D.** `rt_terrain3d_get_normal_at` and the chunk build helper both compute the Y component of the finite-difference normal as `2 * scale[0] * scale[2]` (previously `2 * scale[0]` only); lateral components carry the perpendicular horizontal scale. Non-uniformly scaled terrain now reports correct lighting and collision normals.

**Graphics3D — Water3D / Particles3D.** Water Gerstner phase switched to the standard `k·x − ω·t` form — waves now travel in the user-specified `+direction` instead of against it. Particles clamp `delta_time` at the spawn accumulator (≈4 frames' worth) so a frame hiccup with a high rate no longer spawns thousands of particles in a single frame.

**GUI (runtime, `rt_gui_*`).** `Widget.Focus()` promoted to the base widget runtime class and mirrored on `CodeEditor`. New `rt_gui_tick_widget_tree` walks the widget tree from `rt_gui_app_render` once per frame with a clamped `dt`, dispatching `vg_textinput_tick` (cursor blink), `vg_progressbar_tick`, and `vg_codeeditor_tick`; visible command palettes are also ticked. Runtime `ToolbarItem.SetText()` invalidates layout/overflow bookkeeping immediately; toolbar/menu pixel-icon setters create real image icons instead of reinterpret-casting pointers as glyph codes. Wheel-event delivery in `rt_gui_send_event_to_widget` no longer localizes mouse coords on wheel events (see Event Dispatch under GUI Library).

**CodeEditor (runtime API).** Six new/updated APIs: `CanUndo` / `CanRedo` (query undo stack), `SetTabSize(n)` / `GetTabSize` (1–16 spaces), `SetWordWrap(flag)` / `GetWordWrap` (display-only wrapping; resets horizontal scroll). `AddHighlight` gains a 5th `color` parameter. `GetLineAtPixel(y)` / `GetColAtPixel(x, y)` map screen-relative pixel coordinates to text positions. All functions carry graphics-disabled stubs. Word-wrap now drives cursor movement, scrollbar math, pixel hit-testing, `ScrollToLine`, and runtime cursor-pixel helpers rather than being paint-only.

**TabBar (runtime API).** `Tab.SetTooltip(text)` added; new tabs default their tooltip to the tab title. `WasChanged()` no longer spuriously primed on first poll, and `GetCloseClickedIndex()` remains valid when `auto_close` destroys the tab during the same click.

**Breadcrumb / Minimap (runtime API).** `SetVisible` / `IsVisible` added on both so apps can hide chrome when there's no content. `Widget.IsHovered()` promoted to the base widget class — any widget can now be queried for hover state without bespoke tracking.

**AchievementTracker.** `Viper.Game.AchievementTracker` threads runtime string handles end-to-end instead of raw C strings; the tracker retains and releases `rt_string` values consistently and draws notifications through the real graphics string ABI rather than transient C-string conversions.

**ButtonGroup.** Removing the currently selected button now clears the active selection and marks the selection as changed; previously `selected_index` could stay stale and `is_selected()` could return `true` for an already-removed id.

---

### GUI Library (`src/lib/gui/`)

**Event dispatch (`vg_event.c`).** `VG_EVENT_MOUSE_WHEEL` removed from `event_has_widget_local_mouse_coords`; the mirror list in `rt_gui_send_event_to_widget` is also trimmed. `vg_event_t`'s `mouse` and `wheel` payloads share an anonymous union — `mouse.x` overlaps `wheel.delta_x` at byte 0, `mouse.y` overlaps `wheel.delta_y` at byte 4 — so any path that wrote `mouse.x/y` on a wheel event silently zeroed the scroll delta before the handler could consume it. Wheel events carry `screen_x/y` for hit-test routing but need no widget-local coords.

**Widget core & hit testing (`vg_widget.c`).** `vg_widget_hit_test` excludes a scrollbar-gutter strip from child recursion when the parent is a `VG_WIDGET_SCROLLVIEW` — but only for scrollviews wider than ~32 px, so narrow embedded scrollviews (color-picker channels, completion popups) still hit-test their children. New `vg_tooltip_manager_widget_destroyed()` is called from `vg_widget_destroy` to clear any manager references (hovered widget, anchor widget) so subsequent hover updates don't dereference freed memory.

**Layout (`vg_layout.c`).** Flex layout in non-stretch alignment modes preserves the child's measured cross size instead of subtracting margins twice, fixing a few-pixel clip on descenders/icons. `VBox` / `HBox` budget child margins when distributing remaining space, eliminating overflow and clipping of later siblings. `SplitPane` pane sizes are clamped non-negative during tiny resizes; when `min_first + min_second` exceeds available, a new `resolve_first_size()` helper distributes space proportionally instead of collapsing the first pane to zero.

**Dialog (`vg_dialog.c`).** New `closing_in_progress` flag plus snapshotted callback locals prevent double dispatch when an `on_result` handler calls `vg_dialog_close` again. The header gains `on_result_user_data` and `on_close_user_data` so each callback receives its own context (legacy `user_data` preserved as an alias); previously `set_on_close` silently dropped its user_data whenever `on_result` was registered.

**TabBar (`vg_tabbar.c`).** `tabbar_measure` re-clamps `scroll_x` against the new `total_width` so removing tabs can't strand remaining tabs offscreen. Close-click polling stores a stable tab index instead of a tab pointer; close-button hit testing uses the full close-rect bounds; default tooltips mirror renamed titles until explicitly overridden; hovered tabs surface their per-tab tooltip through the standard tooltip manager; modified (`" *"`) markers participate in width measurement.

**Toolbar / MenuBar / StatusBar.** Toolbar overflow is now a real popup backed by `ContextMenu` plumbing (`toolbar_ensure_overflow_popup`, `rebuild_overflow_popup`, `sync_popup_capture`, `dismiss_overflow_popup`, `show_overflow_popup`, `forward_popup_event`) with input capture, overlay painting, and hover/click forwarding. Toolbar/StatusBar pointer hit testing uses widget-local coordinates, fixing hover and click offsets when the widget is not positioned at `(0, 0)`. Top-level disabled menus paint and behave as disabled. MenuBar desktop dropdown width expands to fit the widest visible item/shortcut row; outside clicks close without firing the highlighted action. Tabbar and toolbar size fields (`tab_height`, `tab_padding`, `close_button_size`, `max_tab_width`, `item_spacing`, scaled icon pixels, label centering via font metrics) scale by `theme.ui_scale` for HiDPI. The managed menubar measures to zero height when `native_main_menu` is active (macOS).

**CodeEditor (`vg_codeeditor.c`).** Line-slot reuse clears stale per-line syntax-color metadata during `SetText` and multi-line compaction (fixes a ViperIDE crash on file open / language switch where `SetLanguage` could free a recycled `line->colors` pointer). Scrollbar `scroll_ratio` divisor is guarded (`scroll_range > 0`) to prevent NaN feeding an `(int32_t)` cast when content exactly fits the viewport. `clamp_editor_position` is split into `clamp_editor_line` + `clamp_editor_col` helpers so future selection-anchor code can clamp axes independently. Editor content is clipped to the viewport during paint; scrollbar drags keep capture until mouse-up outside the widget. Hiding line numbers collapses the gutter completely; `SetLineNumberWidth()` is stored in character cells so it tracks font changes. Fold gutters/regions render, toggle from gutter clicks, and hide folded body lines consistently across painting, scrolling, cursor clamps, and pixel-position helpers.

**FindBar (`vg_findreplacebar.c`).** `SetVisible()` routes through normal widget visibility; live `GetFindText()` / `GetReplaceText()` reads reflect the current input contents; `Replace()` returns `0` when there is no bound editor or current match instead of reporting false success. `perform_search` advances by `match_len` (or 1 byte when zero) rather than `pos++`, keeping the cursor on UTF-8 codepoint boundaries and producing non-overlapping matches.

**ContextMenu (`vg_contextmenu.c`).** Screen-edge clamping moved from `vg_contextmenu_show_at` into `contextmenu_paint`, where the window handle is reliably available via the canvas argument; the never-set `impl_data` dependency is gone.

**FileDialog (`vg_filedialog.c`).** Layout metrics extracted into `FILEDIALOG_*` named constants (title height, sidebar width, row height, button dimensions, save-mode bottom strip). New `get_parent_screen_origin` helper resolves anchor coordinates so the dialog positions correctly inside nested containers. Substantial layout rewrite for HiDPI scaling and the save-mode bottom strip. Accepted path lists are now snapshotted on successful `Show()` / `OpenMultiple()` completion instead of aliasing backend-owned arrays — fixes repeat-show and destroy-time lifetime bugs for multi-select dialogs.

**ScrollView (`vg_scrollview.c`).** Auto-hide scrollbar selection iterates until horizontal/vertical visibility stabilizes (covers the cross-axis case where one scrollbar forces the other). Viewport hit testing respects clipped ancestors; scrollbar-thumb drags keep pointer capture until mouse-up even when released outside the widget.

**Dropdown (`vg_dropdown.c`).** New `dropdown_resolve_panel_rect` helper shared by `dropdown_panel_hit` and `dropdown_paint_overlay` flips the panel above the trigger when it would overflow the window below and there's more vertical room up than down; hit-testing and paint agree on the final rect.

**ListBox / TreeView.** Virtual-mode painting clips to the viewport before drawing overscanned cached rows. `vg_treeview_collapse` re-clamps `scroll_y` against the new visible-row count so collapsing a scrolled node no longer leaves blank space at the bottom.

**TextInput (`vg_textinput.c`).** Password-mode mask render allocates dynamically (256-byte stack buffer for short input, `malloc` beyond) so long pasted secrets are no longer silently capped at 1023 asterisks. `SetText()` (programmatic assignment) no longer fires the user-edit `on_change` callback.

**Tooltip (`vg_tooltip.c`).** Multi-line wrap, opaque background/border, follows live tooltip-text changes on the hovered widget, and hides automatically when the hovered/anchored widget is hidden or disabled. A progress guard in the wrap loop forces at least one byte of advance per outer iteration so whitespace-only input (`"   "`) no longer spins the wrap code and hangs the UI.

**Notification (`vg_notification.c`).** `vg_notification_manager_update` lazily stamps `created_at` on first observation instead of leaving it at 0 (previously all toasts with `duration_ms > 0` vanished on the first frame). Fade math now guards `fade_duration_ms > 0` and snaps opacity to 0/1 when no fade is configured, replacing the NaN opacity that silently hid notifications.

**VideoWidget (`rt_videowidget`).** New explicit `Destroy()` tears down the widget subtree and releases the owned `VideoPlayer` immediately; failure paths are hardened and post-destroy method calls are guarded.

**Minimap.** Click-to-scroll reworked. New internal helpers (`minimap_document_line_count`, `minimap_line_from_local_y`, `minimap_scroll_editor_to_line`, `minimap_trimmed_line_bounds`) resolve a click's local-Y to a document line and scroll the bound editor there, handling blank-line trimming so the minimap maps to visible content rather than the raw buffer.

**Drag / drop.** Drag-over state remains active while the pointer is stationary over a valid drop target. An empty accepted-type list once again means "accept any type" instead of rejecting typeless drags. Drag start is gated to `VGFX_MOUSE_LEFT`.

**Spinner / Image.** Spinner gains additional size variants and theming hooks; Image gains additional fit modes and colorization paths.

---

### Graphics Platform (`src/lib/graphics/`)

**macOS (`vgfx_platform_macos.m`).** The Fn+arrow translation block that gated on `NSEventModifierFlagFunction` has been removed. Cocoa sets that modifier flag on every arrow-key press because arrow keys are themselves classified as function keys, so the gate intercepted bare Up/Down/Left/Right and emitted `VGFX_KEY_PAGE_UP` / `PAGE_DOWN` / `HOME` / `END` — breaking both editor navigation and game input. Real Fn+arrow on compact keyboards already arrives as `NSPageUpFunctionKey` / `NSHomeFunctionKey` etc. through the `chars` argument and is handled by the character switch.

---

### IL

**`runtime.def`.** `Canvas.CopyRect` and `Canvas.Screenshot` now declare `obj<Viper.Graphics.Pixels>` return types on both the `RT_FUNC` and `RT_METHOD` rows so the IL type system carries the `Pixels` return type through to callers instead of erasing it to a bare `obj`.

---

### Frontend — Zia

**Completion.** Four new `Viper.Zia.Completion` overloads take a `sourcePath` parameter: `CompleteForFile(text, path, line, col)`, `CheckForFile(text, path)`, `HoverForFile(text, path, line, col)`, `SymbolsForFile(text, path)`. Relative `bind` paths now resolve against the active file, so multi-file projects keep completion accuracy when the IDE's working directory differs from the file's directory. The original argument-less overloads remain for in-memory snippets. Implemented in `rt_gui_features.c` with stubs in `rt_zia_completion_stub.c`; plumbed end-to-end through `CompilerBridge.cpp`.

**Lowerer.** The monolithic `Lowerer_Expr_Binary.cpp`, `Lowerer_Expr_Call.cpp`, and `Lowerer_Expr_Collections.cpp` shed roughly 540 LOC into three helper classes: `BinaryOperatorLowerer` (arithmetic, comparison, string concatenation, bitwise), `CallArgumentLowerer` (named-argument ordering, default parameters, variadic packing), and `CollectionLowerer` (list/set/map literals, tuples, index access). The `Lowerer` class befriends each helper; entry-point methods delegate. No behavioural change; expression-lowering surface is now navigable in three discrete files instead of one ~1200-LOC file.

**Types display strings.** `Types.cpp` / `Types.hpp` consolidate scattered type-to-string formatters into one recursive `appendTypeString(ss, type, developerFacing)` helper covering every `TypeKindSem` case (Integer / Number / Boolean / String / Byte / Unit / Void / Error / Ptr / Optional / generic type args) and rendering type arguments as `[T1, T2, ...]`. A new `toDisplayString()` accessor is the user-facing entry point; `developerFacing` lets diagnostics show terser user-grade names (e.g. `String?`) while debug dumps can still request the fully-qualified form.

**Sema error messages.** Every Sema error that names a type (type mismatch, member-access on Optional without null check, missing member, cast failure, pattern-literal vs scrutinee mismatch, tuple index on non-tuple) renders through `toDisplayString()` — users see `String?` instead of `Optional<String>`, and `List[Integer]` instead of the internal representation. New check: `var x = null;` without an explicit type annotation fails with *"Cannot infer type from null initializer; add an explicit type annotation such as 'String?', 'MyType', or 'GUI.Font'"* instead of silently producing `Optional<Unknown>`. Optional member access (`opt.field` instead of `opt?.field` / `opt!.field`) produces a targeted error naming the Optional type rather than a generic "has no member" message. Tighter assignment type-conversion check: mixed Unknown-type operands no longer bypass the convertibility assertion.

---

### Linker

**Dynamic symbol policy (`DynamicSymbolPolicy.hpp`).** `uname`, `gethostname`, and `sysctlbyname` added to the known dynamic-symbol list on Linux and macOS. Required by `Viper.Machine.OS` / `Viper.Machine.Hostname` and by the IDE's macOS font-fallback chain; without these entries the linker produced static-resolution failures.

---

### Build

- `src/buildmeta/VERSION` bumped from `0.2.4-dev` → `0.2.5-snapshot`.
- `src/lib/gui/CMakeLists.txt` keys GUI test targets off `VIPER_BUILD_TESTING`, matching the rest of the tree. Fixes a long-standing registration bug where GUI tier tests existed in source but were silently absent from generated CTest targets.

---

### Tests

- `zia_smoke_crackman` — runs `examples/games/pacman/smoke_probe.zia`, expects `RESULT: ok`, 30 s timeout, labels `zia;smoke`.
- `test_achievement_draw_native` — AArch64 native smoke probe exercising the runtime-string-handle draw path for achievement notifications.
- Extended `RTAchievementTests.cpp` for the `rt_string` retain/release lifecycle.
- New `src/tests/unit/runtime/TestConfig.cpp` covering the `Viper.Game.Config` runtime class.
- New `test_vg_audit_fixes` suite (19 cases) covering dialog re-entry guard, two-slot user_data routing, tooltip dangling-pointer cleanup, notification lazy timestamp, find/replace UTF-8 advance, tabbar scroll clamp, contextmenu independence from `impl_data`, codeeditor cursor clamp, scrollbar finite scroll, splitpane proportional clamp, scrollview hit-test gutter (including narrow-widget re-test), flex non-stretch sizing, password-mask long content, wheel-delta survives localize, dropdown flip-above without window, tooltip wrap termination on whitespace, treeview collapse re-clamp, and notification zero-fade snap.
- Three new cases in `test_vg_tier2_fixes.c`: `tabbar_metrics_follow_theme_scale`, `tab_tooltip_can_be_replaced`, `native_menubar_measures_to_zero_height`. Additional tier coverage for TabBar close-index lifetime/full-rect hit testing, toolbar/statusbar local hit testing, VBox/HBox flex margin budgeting, SplitPane tiny-resize clamping, ScrollView cross-axis auto-hide, ListBox virtual clipping, Toolbar overflow popup, `ToolbarItem.SetText()` invalidation, fold-gutter click toggling, silent `TextInput.SetText`, CodeEditor line-slot metadata reuse, hidden-widget focus/capture cleanup, tab-tooltip hover propagation, scrollbar drag capture, wrap-aware pixel helpers, folded-line pixel mapping, line-number-width font tracking, live FindBar semantics, menu/toolbar pixel-icon contracts, and `TabBar.WasChanged()` edge triggering.
- Extended `RTGuiRuntimeTests.c` with CodeEditor tab-size clamping, word-wrap toggle, `CanUndo`/`CanRedo`, folded-line pixel helpers, line-number-width tracking, live FindBar text/no-op replace checks, menu/toolbar pixel-icon checks, and `test_tabbar_close_click_index_survives_auto_close`.
- Extended `RTVideoWidgetContractTests.cpp` with explicit destroy coverage.
- Extended `test_rt_morphtarget3d.cpp` with weight-clamp cases plus packed-payload generation tracking (shape-add and delta-edit bump the generation; weight-only edits do not) and packed position/normal export assertions.
- `test_rt_canvas3d.cpp` (+208 LOC) and `test_rt_canvas3d_gpu_paths.cpp` (+60 LOC) extended for the new VP caching, overlay paths, and backend hand-off.
- New case in `test_rt_scene3d.cpp` guarding scene-graph reference handling through mesh reassignment.
- New shared-contract cases in `test_vgfx3d_backend_d3d11_shared.c`, `test_vgfx3d_backend_metal_shared.c`, `test_vgfx3d_backend_opengl_shared.c`, and `test_vgfx3d_backend_utils.c` for the packed-delta handoff across every backend.
- Extended `RTParticles3DContractTests.cpp` contract coverage. Full 3D suite verification on the prior pass: 318 tests green across `test_rt_morphtarget3d`, `test_rt_skeleton3d`, `test_rt_canvas3d`, `test_rt_physics3d`, `test_rt_animcontroller3d`.
- Extended `RTCameraTests.cpp`, `test_rt_bitmapfont.cpp`, `test_rt_tilemap_layers.cpp`, `RTCanvasFrameTests.cpp`, `RTCanvasTextLayoutTests.cpp`, `RTCanvasUnavailableTests.cpp`, `RTPixelsTests.cpp`, and `test_rt_sprite_consolidated.cpp` for parallax-camera rendering, UTF-8 codepoint text, and the refined sprite/tilemap surfaces.
- New `tests/zia_runtime/41_runtime_reference_types.zia` exercising `Viper.Network.Url.Parse`, property access (`Host`, `Path`), `Url.Clone()` returning `Url?`, and `!` unwrap.
- New `MacPlannerMapsMachineAndHostSyscallsToLibSystem` case in `test_platform_import_planners.cpp` for the new dynamic-symbol entries.

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
| `6c600dbd9` | 2026-04-16 | `chore(gui,runtime,zia)`: GUI correctness sweep (dialog, tooltip, notification, findreplace, tabbar, contextmenu, codeeditor, scrollview, splitpane, flex layout, textinput), per-widget tick, FileDialog/Spinner/Image expansion, lowerer refactor |
| `deccd1978` | 2026-04-16 | `chore(gui,runtime,docs)`: macOS arrow-key translation, mouse-wheel union-aliasing, dropdown flip-above, scrollview narrow-gutter, tooltip wrap termination, treeview scroll clamp, notification fade, widget polish sweep |
| `c5b0685af` | 2026-04-16 | `chore(graphics3d,graphics,runtime)`: skeleton parent/bone-index validation, terrain/heightfield normals, capsule Y-only contract, navmesh edge hash, crossfade bind-pose TRS, Canvas3D cached_vp, morph clamp, particle dt, water direction, parallax camera, UTF-8 text |
| `cd07b31af` | 2026-04-16 | `chore(graphics3d,runtime,docs)`: Camera3D input sanitizers, Mesh3D reference-slot helpers, MorphTarget3D lazy packed-payload rebuild, backend packed-delta handoff (d3d11/metal/opengl/sw), release-notes re-organized by area |

<!-- END DRAFT -->
