# Plan 06 — Implement whitespace rendering

## 1. Objective & scope

Implement the CodeEditor's declared-but-never-built `render_whitespace` option:
faint dots for spaces and arrows for tabs, plus expose it end-to-end — widget →
`runtime.def` → ViperIDE setting (Settings overlay + persisted key + View menu
toggle). The IDE already has trim-trailing-whitespace and ensure-final-newline
save options; users who care about those need to *see* whitespace.

**In scope:** paint implementation, three render modes (`none`, `boundary`,
`all` — boundary = leading/trailing only, the most useful default when on),
runtime registration, IDE setting + command, probe.

**Out of scope:** control-character rendering, indent guides (already exist:
`show_indent_guides`, `vg_ide_widgets_editor.h:185`).

## 2. Current state (verified anchors)

- Flag exists, documented as "Reserved: render space/tab markers (default off)"
  (`src/lib/gui/include/vg_ide_widgets_editor.h:186`), initialized false
  (`vg_codeeditor_lifecycle.inc:141`), and **never read anywhere else**
  (verified by grep across `src/lib/gui` and `src/runtime/graphics/gui`).
- Paint pipeline: `vg_codeeditor_paint.inc` draws per visible line — text run
  at `content_x - scroll_x` (`:465,490`), selection rects (`:426,451`), cursor
  (`:517`); per-char x positions are `content_x + col * char_width - scroll_x`
  (monospace, `:226`). Drawing primitives come from `vg_draw.h` (AA core —
  discs/lines available; see `src/lib/gui/src/core/vg_draw.c`).
- Theme colors: gutter/indent-guide colors derive from theme when 0
  (`indent_guide_color` doc, `vg_ide_widgets_editor.h:187`); follow that
  pattern (`whitespace_color`, 0 = derive faint from theme text color).
- Runtime registration pattern: display toggles like
  `CodeEditor.SetShowFoldGutter` (`runtime.def:2851`) — one RT_FUNC + one
  RT_METHOD.
- IDE settings plumbing: `core/settings.zia` (persisted keys),
  `ui/ide_overlays.zia` settings panel (e.g. `ReadWordWrap` at `:362` shows the
  editing-tab checkbox pattern), applied via
  `app/settings_applier.zia` and `view_commands.applyPersistentSettings`
  (`main.zia:236`). View-menu toggles live in `commands/view_commands.zia` with
  command ids in `commands/command_catalog.zia`.

## 3. Design

### 3.1 Widget

Replace the bool with a mode (keep the bool field name for ABI simplicity is
NOT required — internal struct, no ABI): change `bool render_whitespace` to
`int render_whitespace_mode` (0=none, 1=boundary, 2=all) + add
`uint32_t whitespace_color` (0 = derive: theme text color at ~25% alpha —
`vg_draw` supports alpha; check how indent guides derive their faint color and
copy it).

Paint (in the per-line text loop of `vg_codeeditor_paint.inc`):

- For each visible line, walk the visible column range only
  (`first_visible_col = scroll_x / char_width` … viewport cols — do not scan
  entire long lines).
- Space → filled disc of radius `max(1, char_width*0.12)` centered in the cell.
- Tab → small "→" drawn as a line + arrowhead spanning to the next tab stop's
  cell start (tab rendering already positions via tab width logic in the text
  run — align the glyph to the cell the tab occupies visually).
- Mode `boundary`: draw only for columns `< first_text_col` (reuse
  `codeeditor_leading_indent_len`, `vg_codeeditor_input.inc:1736`) or
  `>= last_text_col` (trailing — compute like the END-key logic at `:1840-1842`).
- Draw after selection rects, before text (so selection highlight sits under,
  glyphs never occlude real text — dots are in empty cells anyway).
- Skip entirely in minimap rendering (minimap reads the buffer directly and
  ignores these flags — verify no change needed in `vg_minimap.c`).

### 3.2 Runtime + IDE

```c
RT_FUNC(GuiCodeEditorSetWhitespaceMode, rt_codeeditor_set_whitespace_mode, "Viper.GUI.CodeEditor.SetWhitespaceMode", "void(obj,i64)")
RT_FUNC(GuiCodeEditorGetWhitespaceMode, rt_codeeditor_get_whitespace_mode, "Viper.GUI.CodeEditor.GetWhitespaceMode", "i64(obj)")
```

(+ RT_METHOD entries in the CodeEditor class block; bridge in
`rt_gui_codeeditor.c`; setter invalidates paint only — no layout change.)

IDE:

- Setting key `renderWhitespace` = `"none" | "boundary" | "all"` in
  `core/settings.zia` (string, default `"none"`); load/save next to the other
  editor settings.
- Settings overlay: dropdown/radio in the Editing section of
  `ui/ide_overlays.zia` (follow the existing editing-options widgets; expose
  `ReadRenderWhitespace()`); applied in `settings_applier.ApplySettingsFromOverlay`
  and `view_commands.applyPersistentSettings` (both call
  `engine.editor.SetWhitespaceMode(...)` mapping string→int in one helper in
  `view_commands.zia`).
- Command `togglerenderwhitespace` ("View: Toggle Render Whitespace",
  no default shortcut) in `commands/command_catalog.zia` + handler in
  `commands/view_commands.zia` cycling none→boundary→all→none, persisting the
  setting, status-bar note of the new mode.

## 4. Implementation steps

1. Widget: mode field migration (init, `refresh_layout_state` no-op — paint
   only), paint implementation with visible-range walk.
2. C test (`src/lib/gui/tests/`): golden-ish assertion via perf/paint hooks is
   overkill — instead unit-test the *classification* helper: factor
   `codeeditor_whitespace_cell_visible(line, col, mode)` and test
   boundary/all/none against lines with leading/trailing/interior
   spaces/tabs. Paint correctness is verified by the probe + manually.
3. Runtime bridge + `runtime.def` + `./scripts/check_runtime_completeness.sh`.
4. IDE setting + overlay row + applier + command + catalog entry.
5. Probe: `viperide/src/probes/syntax_render_probe.zia` already exercises
   editor rendering paths — extend it: set mode 2, render a frame, assert via
   `GetWhitespaceMode` round-trip and that rendering doesn't trap; keep the
   classification-level assertions in the C test. Register nothing new if
   extending; otherwise new probe with `LABELS "zia;viperide;editor"`.
6. Manual: toggle from the command palette and Settings; verify dots/arrows,
   boundary mode, dark-theme legibility (faint, not shouting), zoom scaling
   (`char_width` scales — radii derive from it).
7. Full no-skip build + test run.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h` — mode field + color + docs.
- `src/lib/gui/src/widgets/vg_codeeditor_paint.inc` — rendering.
- `src/lib/gui/src/widgets/vg_codeeditor_lifecycle.inc` — init.
- `src/runtime/graphics/gui/rt_gui_codeeditor.c` — bridge.
- `src/il/runtime/runtime.def` — 2 entries + class methods.
- `viperide/src/core/settings.zia`, `ui/ide_overlays.zia`,
  `app/settings_applier.zia`, `commands/view_commands.zia`,
  `commands/command_catalog.zia` — setting + UI + command.
- `src/lib/gui/tests/` — classification test.
- `viperide/src/probes/syntax_render_probe.zia` — extension.

## 6. Testing

C classification test + extended render probe + manual visual pass (step 6).
Existing syntax/paint probes guard the untouched paint paths.

## 7. Acceptance criteria

- Mode `all`: every space/tab in the viewport shows a faint marker; `boundary`:
  only leading/trailing; `none`: byte-identical rendering to today.
- Setting persists across IDE restarts; palette command cycles modes live.
- No measurable paint-time regression with mode `none` (the added branch is
  per-line, not per-char, when disabled).

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD` entries; run
  `./scripts/check_runtime_completeness.sh` after `runtime.def` edits.
- Full Viper header on modified C files.
- 100% cross-platform; no platform code involved. Dark theme first (the user
  works in dark themes exclusively) — verify contrast there.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
