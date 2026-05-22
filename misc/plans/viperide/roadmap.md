# ViperIDE: From Barebones to an Impressive, Powerful IDE

## Context

ViperIDE today is functional but flaky and shallow. It is the designated tool for **two** roles: the Zia/BASIC **code editor** and the game engine's **visual scene/level editor** (the major ViperIDE pillar of the v0.3.x roadmap). The goal of this initiative is to make it a credible, polished, powerful IDE on both fronts — a daily-driver code editor *and* a Godot-style integrated level editor.

This is a multi-increment program, not a single change. Per project rules each phase is a **coherent, shippable, always-green unit (<50 files)**, implemented **directly (no code-writing agents)**, zero new dependencies, fully cross-platform. No IL-spec changes are involved (all work is additive runtime/IDE), so no ADR is required.

**Decisions locked with the user:**
1. **Sequencing:** Harden/deepen the code editor first, *then* build the scene editor on the stable base.
2. **Level format:** Data-driven JSON, round-trippable. The runtime gains a level/tilemap *writer*; games migrate to loading JSON levels.
3. **Integration:** The scene editor lives as **integrated tabs in one window** — opening a `.level`/`.scene` file swaps the editor area into the visual editor, sharing the tab bar, file tree, and status chrome.

## Current State (grounded assessment)

**Architecture.** The IDE is ~5K lines of **Zia running on the Viper VM**, drawing through Viper's own zero-dependency C stack: `vipergui` (~55K LOC, `src/lib/gui/`) over `vipergfx` (~9K LOC, software-rendered, `src/lib/graphics/`). It is a **single frame-driven loop** in `examples/apps/viperide/main.zia` that polls input, ticks IntelliSense controllers, dispatches commands, and renders. Builds **shell out** to the `zia` compiler (`build/build_system.zia`, `Exec.ShellFull`) and parse errors via regex; live IntelliSense uses **in-runtime** `Viper.Zia.Completion.*ForFile` APIs (`src/frontends/zia/rt_zia_completion.cpp`).

**Solid today:** text buffer + undo/redo + cursor (`vg_codeeditor.c`); real Zia syntax highlighting (reuses the lexer); completion/hover/diagnostics (debounced, multi-surface); multi-tab document model; file tree; zoom/theme/fullscreen/settings persistence; find/replace (delegated to `GUI.FindBar`); minimap; breadcrumb; status bar; symbol outline.

**Flaky / barebones:**
- **Go-to-definition is in-file only** and uses fragile string parsing (`commands/edit_commands.zia:102-155`). No cross-file index, no find-references, no rename.
- **Project search results aren't clickable** — yet the data (`path:line`) is *already stored* per row (`commands/search_commands.zia:96`); only a click handler is missing.
- **Signature help is tooltip-only** (`main.zia:279-296`) — no parameter highlighting or overloads.
- **Multi-cursor exists in the widget but is unexposed** — no keybindings reach it.
- **File tree** has no rename/delete/drag and hardcoded excludes, no `.gitignore` (`core/project_manager.zia`).
- **The command dispatcher is one giant linear `if`-chain** (`main.zia:376-455`) — readable now, but it will not scale to a whole scene-editor mode.
- **Documents are text-only** (`core/document.zia`) — the editor area hosts exactly one `CodeEditor`; there is no concept of a non-text document surface.
- **Build output is a `ListBox`**, not a real console.

**Verified findings (corrected during exploration):**
- The reported "inverted project-search bug" is a **false alarm**. `Viper.String.IndexOf` → `rt_str_index_of` (`src/runtime/core/rt_string_ops.c:990`) is **1-based, returns 0 when not found** (INSTR-style), so `if Str.IndexOf(line, query) != 0` is correct. The real (smaller) issue is **documentation divergence**: the Zia bible documents `IndexOf` as 0-based/`-1`-not-found, contradicting the implementation.
- **`LevelData` is load-only** — `src/runtime/game/rt_leveldata.h` exposes only `load` + getters; there is **no save/set/new**. The format cannot round-trip yet. (`Tilemap` *does* have `rt_tilemap_save_to_file`, `rt_tilemap.h:289` — a model to mirror.)
- **No drawable-viewport widget exists** in `vipergui`. A scene view must be a new widget that renders the engine's tilemap/entities into a `vg_widget` framebuffer.

## Roadmap

### Phase 0 — Foundations, correctness, quick wins
*Goal: prepare the architecture for everything that follows; bank cheap user-visible wins.*
- **Command registry.** Generalize the `triggered/triggeredNoMenu/triggeredMenu` helpers (`main.zia:636-654`) into a small dispatch table mapping command-id → handler, so new features (scene editor, debug, refactor) register cleanly instead of growing the linear chain.
- **`DocumentKind` abstraction.** Add a kind field (`code` | `scene`) to `core/document.zia` + `core/document_manager.zia`; on tab switch, the main loop selects which surface mounts into `shell.editorArea`. (Scene surface arrives in Phase 5; this just makes the area swappable.)
- **Clickable search results.** Wire a click handler on `shell.outputListBox`, mirroring the diagnostics-panel pattern (`main.zia:458-467`). Data is already attached (`search_commands.zia:96`). Add case-insensitive + whole-word options to the search prompt.
- **Session restore.** Persist open file paths + active tab + per-file cursor in settings (`core/settings.zia`) and reopen on launch.
- **Docs fix.** Reconcile the `IndexOf` contract — make the implementation (1-based/0) canonical; correct the bible pages. (Docs-only; no build.)
- *Critical files:* `main.zia`, `core/document.zia`, `core/document_manager.zia`, `core/settings.zia`, `commands/search_commands.zia`.

### Phase 1 — Code-editor depth (the "powerful" part)
*Goal: cross-file intelligence + pro editing ergonomics.*
- **Project symbol index → real go-to-definition + find-all-references.** Extend the semantic engine (`src/frontends/zia/rt_zia_completion.cpp` / `ZiaCompletion`) with project-wide `DefinitionForFile`/`ReferencesForFile`, exposed through new `Viper.Zia.Completion.*` bridge entries (+ `runtime.def`). Replace the string-parsing go-to-def in `edit_commands.zia`.
- **Rename-symbol refactor**, built on the reference index.
- **Expose multi-cursor** (already in `vg_codeeditor.c`) with keybindings (add-next-occurrence, column select).
- **Upgrade signature help:** dedicated signature widget with active-parameter highlighting + overloads (extend `SignatureHelpForFile`), replacing the reused hover tooltip.
- **File-tree operations:** rename, delete, drag-move, `.gitignore`-aware filtering (`core/project_manager.zia`).
- *Critical files:* `src/frontends/zia/rt_zia_completion.cpp`, `src/runtime/runtime.def`, `editor/completion.zia`, `commands/edit_commands.zia`, `core/project_manager.zia`, `src/lib/gui/src/widgets/vg_codeeditor.c`.

### Phase 2 — Run / debug experience
*Goal: close the inner loop. Largest single item; severable if appetite is limited.*
- **Real output console** replacing the build-output `ListBox` (scrollback, monospace, preserved clickable error nav).
- **Integrated debugger** (the big sub-project): gutter breakpoints, step/continue, call stack + locals, leveraging VM hooks via a thin debug adapter the IDE drives. Flag as its own multi-increment effort; can ship after Phase 5 if scene editor is the priority.
- *Critical files:* new `src/lib/gui/src/widgets/vg_console.*` (+ stub), VM debug hooks, `build/build_system.zia`.

### Phase 3 — Scene-editor data foundation (runtime)
*Goal: make the level format round-trippable — the prerequisite for any visual editor.*
- **Add `LevelData` writer + mutators** to the runtime: `rt_leveldata_save`, dimension/theme/player-start setters, object add/remove/update, and a JSON serializer mirroring the loader (`rt_leveldata.c`) and `rt_tilemap_save_to_file` (`rt_tilemap.h:289`).
- **Register** every new function in `runtime.def` with both `RT_FUNC` and `RT_METHOD`; pass `./scripts/check_runtime_completeness.sh`.
- **Tests:** fail-before/pass-after unit tests + a **golden JSON round-trip** (load → save → byte-stable).
- **Dogfood:** migrate one sample (e.g. xenoscape level 0) to load a JSON level, proving the format is editor-ready. Keep the procedural path working during migration.
- *Critical files:* `src/runtime/game/rt_leveldata.h/.c`, `src/runtime/graphics/rt_tilemap_io.c`, `src/runtime/runtime.def`, `docs/viperlib/game/leveldata.md`, `examples/games/xenoscape/`.

### Phase 4 — Scene viewport widget (GUI / C)
*Goal: a pannable/zoomable canvas that renders a level.*
- **New `GUI.SceneView` widget** (`src/lib/gui/src/widgets/vg_sceneview.c` + `rt_gui_sceneview.c` binding + Zia API). Renders tilemap layers + entity markers + grid into the widget framebuffer; pan/zoom and mouse→world-tile mapping reuse `Camera` world/screen transforms (`rt_camera.h`).
- **Both real impl AND a non-graphics stub** under `#ifdef VIPER_ENABLE_GRAPHICS` (per project rule for new `rt_*` graphics fns).
- *Critical files:* `src/lib/gui/src/widgets/vg_sceneview.c`, `src/runtime/graphics/rt_gui_sceneview.c`, `src/runtime/graphics/rt_camera.h`, `runtime.def`.

### Phase 5 — Scene-editor UI (Zia, integrated tabs)
*Goal: the actual level editor, docked in the IDE.*
- **`scene_editor` module** mounts into `shell.editorArea` when the active document's `DocumentKind` is `scene` (from Phase 0), sharing tabs/tree/status chrome.
- **Tools:** tile **palette** (load tileset via `Assets.Load`, `rt_asset.h`); **brushes** (paint/fill/erase); **layer panel** (16 layers: show/hide/select); **entity tool** (place/move/delete `objects`: type/id/x/y); **property inspector** (per-tile collision type, tile key-value props, object props) reusing `vg_colorswatch.c`, `vg_listbox`, `vg_treeview`.
- **Selection + gizmos** drawn from Canvas primitives (box/frame/line). **Undo/redo** for scene edits.
- **Save** writes JSON via the Phase 3 writer; **Play** runs the game on the current level (shell-out, current model; embedded preview is a later enhancement).
- *Critical files:* new `examples/apps/viperide/scene/*.zia`, `main.zia` (surface swap), `core/document.zia`.

### Phase 6 — Polish & dogfood
- Dark-first theming pass (user accessibility), command-palette + shortcut coverage for all new commands, consolidated docs (single release-notes line), `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh`.

## Reusable assets (don't reinvent)
- Dispatch helpers `triggered*` (`main.zia:636-654`); diagnostics click-to-navigate (`main.zia:458-467`); search rows already carry `path:line` (`search_commands.zia:96`).
- `ZiaCompletion` `*ForFile` engine (`rt_zia_completion.cpp`) — extend for defs/refs/rename.
- `vg_codeeditor.c` multi-cursor (implemented, unexposed).
- `rt_tilemap_save_to_file` (`rt_tilemap.h:289`) — template for the LevelData writer.
- `Camera` world/screen transforms (`rt_camera.h`) — viewport pan/zoom.
- `vg_colorswatch.c`, `vg_listbox`, `vg_treeview` — inspector/palette panels.
- `Assets.Load` (`rt_asset.h`) — tileset images for the palette.

## Verification
- **Per phase:** `./scripts/build_viper.sh` (Debug, zero warnings) + `ctest --test-dir build --output-on-failure`; each feature ships a fail-before/pass-after test.
- **Runtime changes (Phase 3+):** `./scripts/check_runtime_completeness.sh`; golden level-JSON round-trip via `./scripts/update_goldens.sh`.
- **Manual UI validation:** run the IDE (`viper run examples/apps/viperide/`) and exercise each feature in-window (open/edit/save, go-to-def across files, paint a level, save→reload→byte-stable, Play). Static checks confirm correctness, not feature-correctness — the IDE must be driven by hand.
- **Cross-platform:** `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh` before any phase is reported done; `.\scripts\build_viper.cmd` parity on Windows for runtime/GUI changes.

## Key risks & decisions
- **Dispatcher scaling** — mitigated by the Phase 0 command registry before scene-editor commands pile on.
- **Cross-file index depends on the `ZiaCompletion` engine** exposing enough semantic data; may need new C bridge entry points + `runtime.def` (verify feasibility early in Phase 1).
- **`GUI.SceneView` is net-new C** in `vipergui`; must ship graphics + stub builds together.
- **Debugger (Phase 2) is the largest, most uncertain sub-project** — explicitly severable; can land after the scene editor.
- **Game level migration** (procedural Zia → JSON) is a behavior change for examples; keep the procedural path working during transition.
- **No feature toggle required** — scene editing activates per `DocumentKind`; nothing global to gate.
