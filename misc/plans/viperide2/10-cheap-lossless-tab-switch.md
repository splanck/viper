# Plan 10 — Cheap, lossless tab switching

## 1. Objective & scope

Make switching tabs O(1)-feeling and visually lossless. Today a switch pays a
full `SetText` (document re-split into lines + full syntax-cache invalidation),
clears semantic tokens and multi-cursors, and forces the semantic controllers
to recompute everything — so big files visibly flash unstyled text and lose
view state. After this plan, a tab switch is a buffer-handle swap: instant,
with syntax colors, semantic tokens, folds, selection, and scroll intact.

**Depends on:** Plan 21 (`Viper.GUI.EditorBuffer`). **Builds on:** Plan 01
(which already swaps buffers for undo). This plan finishes the job: keep
*derived* state (semantic tokens, highlight spans, syntax color cache) alive
per buffer and stop the controllers from re-running on unchanged documents.

**In scope:** buffer-resident derived state verification, controller
re-trigger suppression on switch, tab-switch path cleanup, latency probe.

**Out of scope:** split editors (plan 17), language-service sync transport
(plan 08 — composes: with both, a switch triggers zero semantic recompute AND
zero full-text copies).

## 2. Current state (verified anchors)

- Switch path (`viperide/src/main.zia:562-582`): SaveEditorState →
  `docMgr.SetActive` → `engine.LoadDocument(switchDoc)` →
  `engine.editor.ClearCursors()` → `shell.SelectSurface` → breadcrumb/status.
- `LoadDocument` (`viperide/src/editor/editor_engine.zia:64-101`):
  `editor.SetText(doc.content)` (full re-split; clears undo; resets scroll/cursor)
  + `editor.ClearSemanticTokens()` (`:75` — "drop the previous document's
  semantic overlay") + `SetLanguage` + manual cursor/scroll restore.
- Syntax colors are cached per line with generation counters
  (`vg_code_line_t.colors`, `highlight_generation`,
  `vg_ide_widgets_editor.h:82-88,225`) — `SetText` frees all lines, so the
  cache dies with them; after a switch every visible line re-lexes
  (`line_highlight_calls` counter, `:105`).
- Semantic tokens/highlight spans live on the widget and are cleared/re-added
  per document by controllers (`editor/semantic_tokens.zia`,
  `editor/diagnostics.zia:334-341`).
- Controllers detect document change via per-frame path/revision comparison in
  `language_tool_frame.BeginFrame` (`viperide/src/app/language_tool_frame.zia`)
  and re-queue work through `editor/scheduler.zia` — a switch looks like a
  giant edit to them today because revision/path both change and caches were
  cleared.
- Plan 21 moved into the buffer: lines (with their color caches), history,
  cursor/selection/extra cursors, scroll, folds, semantic tokens, highlight
  spans, revision, modified (see `21-editorbuffer-runtime.md` §3.1).

## 3. Design

### 3.1 What plan 01 already gives (verify, don't redo)

After plan 01, `LoadDocument` does `AttachBuffer` — lines + colors + tokens +
spans survive by construction. This plan's widget-side work is verification +
gap-filling:

- Confirm attach invalidates only *view* caches (layout cache etc.) and does
  NOT bump `highlight_generation` or free line color arrays — first paint after
  re-attach must reuse cached colors (assert via `line_highlight_calls` /
  `syntax_state_line_scans` counters: switch back to a clean buffer → ~0 new
  highlight calls for already-highlighted visible lines).
- `SetLanguage` on every switch (`editor_engine.zia:79-88`): calling it with
  the *same* language must be a no-op (not a highlighter reinstall that dirties
  the cache). Check `rt_codeeditor` SetLanguage / `rt_gui_codeeditor_syntax.c`;
  add an early-return if the language is unchanged for this buffer. Track
  last-language per buffer.

### 3.2 Controller-side: don't recompute the unchanged

`language_tool_frame.BeginFrame` + each controller currently key their caches by
(path, revision). A switch changes path but not that document's revision — the
needed behavior:

- **Diagnostics** (`diagnostics.zia`): keep last results per path (bounded map,
  e.g. 32 entries LRU) so the Problems panel and inline underlines restore
  instantly without a re-check; still schedule a refresh at normal idle cadence.
  The inline highlight spans themselves live in the buffer already (per plan 21)
  — the Problems panel rows are the IDE-side cache.
- **Semantic tokens** (`semantic_tokens.zia`): tokens live in the buffer; add a
  per-path `lastTokenRevision` so the controller skips re-requesting when
  revision is unchanged.
- **Symbols/outline** (`symbols.zia`): cache per path+revision, restore on switch.
- **Completion/hover/signature**: transient by nature — no caching; just ensure
  a switch cancels in-flight jobs (scheduler generation bump — existing
  `Cancel()` semantics, `scheduler.zia:91-94`) rather than letting stale
  results land in the new document (stale-result rejection reportedly exists —
  verify path checks in each controller's result-apply site).
- Delete `engine.editor.ClearCursors()` (`main.zia:570`) if plan 01 didn't
  already; delete the `ClearSemanticTokens` call in `LoadDocument` (tokens are
  buffer-resident now).

### 3.3 Measurable definition of "cheap"

Instrument with existing counters: a switch to an already-visited 5k-line file
must cost, on the widget side, zero `full_text_copies`, ~0
`line_highlight_calls` for cached visible lines, and no semantic-job dispatch
when revision is unchanged.

## 4. Implementation steps

1. Verify/fix attach-preserves-derived-state at the C level (counters test in
   `src/lib/gui/tests/`: highlight all visible lines, swap out, swap in, paint,
   assert `line_highlight_calls` delta ≈ 0).
2. Per-buffer language memo + `SetLanguage` same-language early-return.
3. Controller caches (§3.2): diagnostics rows LRU, tokens/symbols
   revision-memos, in-flight cancellation audit (grep each controller's
   result-apply for path guards).
4. Remove the now-redundant clears in the switch path.
5. Probe `viperide/src/probes/tab_switch_probe.zia`: open two documents
   (one large synthetic ~200KB), warm both (let diagnostics/tokens land),
   switch A→B→A; assert: text identical, revision unchanged, editor perf
   counters show no re-highlight storm, scheduler queued-job flags stayed
   false during the quiet switch. Register `LABELS "zia;viperide;editor;perf"`.
6. Manual: large real file (e.g. `src/runtime/graphics/gui/rt_gui_app.c`
   opened as text, or a big .zia), rapid Ctrl+Tab-style switching — no flash of
   unhighlighted text, no CPU spike, folds/selection intact.
7. Full no-skip build + test run.

## 5. Files to modify

- `src/lib/gui/src/widgets/vg_codeeditor_api.inc` (attach invalidation
  boundaries, if gaps found), `rt_gui_codeeditor_syntax.c` /
  `rt_gui_codeeditor.c` (language memo).
- `viperide/src/editor/editor_engine.zia` — remove clears; language memo use.
- `viperide/src/main.zia` — switch path cleanup.
- `viperide/src/editor/diagnostics.zia`, `semantic_tokens.zia`, `symbols.zia`
  — per-path caches/memos.
- `viperide/src/app/language_tool_frame.zia` — switch-detection plumbing.
- `src/lib/gui/tests/` — counter test.
- `viperide/src/probes/tab_switch_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

C counter test (step 1) + probe (step 5) + existing intellisense/diagnostics
probes for stale-result regressions. Manual large-file feel test (step 6).

## 7. Acceptance criteria

- Switching between two warmed documents shows correct syntax + semantic
  colors on the very first frame (no flash), preserves folds/selection/scroll/
  cursors, and dispatches zero semantic jobs when revisions are unchanged.
- Problems panel repopulates instantly from cache on switch.
- Counters (§3.3) meet their targets in the probe.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` (Zia-only steps need only this).
- Full Viper header on modified C files; Zia module-header style per
  `viperide/docs/architecture.md`.
- 100% cross-platform; no platform code involved.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
