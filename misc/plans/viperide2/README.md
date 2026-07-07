# ViperIDE Overhaul 2 — Plan Index

25 implementation plans produced by a comprehensive review of ViperIDE (`viperide/src/`)
and the C runtime layers beneath it (`src/lib/gui/`, `src/runtime/graphics/gui/`,
`src/lib/graphics/`). Plans 01–20 are the review recommendations; plans 21–25 are
runtime features the IDE needs that do not exist yet (verified missing against
`src/il/runtime/runtime.def`).

Every plan is self-contained: it carries its own evidence anchors (file:line
references verified 2026-07-06), design, phased implementation steps, files to
modify, testing strategy, acceptance criteria, and the repo rules block. An
implementer should be able to execute any single plan with no other context.

**Boundaries for the whole effort:** no CI workflow changes, no scene-editor
work, no external dependencies, never commit.

## Plans

### Runtime enablers (build these before their consumers)

| Plan | Title | Enables |
| --- | --- | --- |
| [21](21-editorbuffer-runtime.md) | `Viper.GUI.EditorBuffer` — detachable per-document editor state | 01, 10, 17 |
| [22](22-treeview-dnd-runtime.md) | TreeView drag-and-drop runtime surface | 12 |
| [23](23-app-pollwait-runtime.md) | `App.PollWait` — blocking event wait with timeout | 03, 09 |
| [24](24-palette-livequery-runtime.md) | CommandPalette live-query API | 19 |
| [25](25-debug-adapter-structured-vars.md) | Debug adapter structured variable expansion | 13 |

### Bug fixes

| Plan | Title |
| --- | --- |
| [01](01-per-document-undo-history.md) | Per-document undo history (undo survives tab switches) |
| [02](02-typing-undo-coalescing.md) | Typing undo coalescing (word/time-grouped undo) |
| [03](03-idle-cpu-spin.md) | Stop the idle 100%-CPU spin |
| [04](04-horizontal-scrolling.md) | Horizontal scrollbar + trackpad/Shift-wheel scrolling |
| [05](05-drag-select-autoscroll.md) | Edge autoscroll during selection drag |
| [06](06-render-whitespace.md) | Implement whitespace rendering |

### Performance

| Plan | Title |
| --- | --- |
| [07](07-damage-region-rendering.md) | Damage-region (partial) rendering |
| [08](08-incremental-language-service-sync.md) | Incremental text sync to language services |
| [09](09-background-throttling.md) | Throttle background work when unfocused |
| [10](10-cheap-lossless-tab-switch.md) | Cheap, lossless tab switching |

### UI polish

| Plan | Title |
| --- | --- |
| [11](11-resizable-bottom-panel.md) | Unified resizable bottom panel |
| [12](12-filetree-drag-move.md) | File-tree drag-and-drop moves |
| [13](13-debugger-structured-inspection.md) | Structured debugger inspection UI |
| [14](14-pixel-smooth-scrolling.md) | Pixel-smooth scrolling |
| [15](15-multiple-terminals.md) | Multiple integrated terminals |
| [16](16-scm-gutter-and-progress.md) | SCM gutter change markers + job progress |

### Feature depth

| Plan | Title |
| --- | --- |
| [17](17-split-editor.md) | Split editor (two panes side-by-side) |
| [18](18-welcome-surface.md) | Real Welcome surface + panel empty states |
| [19](19-unified-palette-modes.md) | Unified palette modes (`>` `#` `:` prefixes) |
| [20](20-crash-safe-recovery.md) | Crash-safe swap-file recovery |

## Recommended execution order

Waves are independent of each other where not annotated; plans inside a wave
are mutually independent unless a dependency is listed.

- **Wave 1 — independent quick wins (all Zia- or widget-local):**
  02, 03, 04, 05, 06, 09, 14
- **Wave 2 — runtime enablers:**
  21, 22, 23, 24, 25 (23 can also land before/with 03 in Wave 1)
- **Wave 3 — consumers of Wave 2:**
  01 and 10 (need 21) → then 17 (needs 21+10); 12 (needs 22); 13 (needs 25);
  19 (needs 24)
- **Wave 4 — standalone larger efforts:**
  07, 08, 11, 15, 16, 18, 20

Dependency edges (complete list): 01←21, 10←21, 17←21+10, 12←22, 13←25, 19←24,
03←23 (soft — 03 has a self-contained fallback), 09←23 (soft).

Each plan is sized to be implemented in a single focused session, with phased
steps that keep the build green between phases. `07-damage-region-rendering.md`
is the largest; its phases are explicitly shippable one at a time.

## Shared context in one paragraph

ViperIDE is a frame-driven poll-mode GUI app written in Zia (`viperide/src/main.zia`
owns the loop). Widgets come from the from-scratch C library at
`src/lib/gui/src/widgets/` (`vg_*`), bridged to Zia by `src/runtime/graphics/gui/rt_gui_*.c`
and registered in `src/il/runtime/runtime.def` (`RT_FUNC` + `RT_METHOD`/`RT_PROP` pairs).
The platform layer (`src/lib/graphics/src/vgfx_platform_{macos.m,win32.c,linux.c}`)
owns windows, events, and the software framebuffer. The IDE binary builds via
`./scripts/build_ide.sh`; runtime/C changes need `./scripts/build_viper_unix.sh`
first. IDE regression tests are probe programs under `viperide/src/probes/`
registered in `src/tests/CMakeLists.txt` with the `viperide` ctest label.
