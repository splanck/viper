# Plan 07 — Editor and Search Depth

Date: 2026-07-17 · Track: A (Zia app) · Loop: Zia + small C · Size: L

## 1. Objective

The daily-driver power features a flagship IDE is judged by: project-wide
replace, a side-by-side diff view, real split-view depth, tab drag-reorder,
and symbol breadcrumbs.

## 2. Scope

1. **Project-wide replace.** Extend the docked search panel
   (`commands/search_commands.zia`) with a replace field and per-match
   preview tree (checkboxes per match/file). Apply through the existing
   multi-file `services/workspace_edits.zia`; edits to open dirty buffers
   route through the document manager (never raw file writes for open docs);
   per-file undo entries; summary notification with counts.
2. **Diff view.** Hand-written Myers diff in a new
   `services/diff_engine.zia` (pure Zia, probe-testable). View = two
   read-only CodeEditors in a `vg_splitpane` with synced scrolling, aligned
   line gutters, intraline char-LCS highlights. Entry points: SCM panel
   file diff (HEAD vs worktree via the existing `scm/scm_git.zia` plumbing;
   replaces the unified-text-only diff), and "Compare with Saved" on dirty
   documents.
3. **Split depth.** Persist/restore split layout in `core/session.zia`
   state; focus routing between panes; same-document coherence: verify
   `vg_codeeditor` buffer ownership early — if the C editor owns its buffer,
   add `Zanna.GUI.CodeEditor.AttachSharedBuffer(other)` (runtime checklist +
   Plan 04 ADR); otherwise a Zia-side mirrored-edit sync. Decision recorded
   before the split work starts.
4. **Tab drag-reorder.** Small C addition to `vg_tabbar.c`: drag capture,
   drop-index ghost indicator, reorder event; Zia handler reorders the
   document list (`editor/editor_tabs.zia`).
5. **Breadcrumb symbols.** Feed existing document symbols
   (`editor/symbols.zia`) into `vg_breadcrumb` segments on debounced caret
   moves; clicking a segment opens the symbol picker scoped to that level.

## 2a. As-built record (2026-07-18)

- **Project-wide replace**: `commands/replace_commands.zia` applies over the
  current search results (KIND_SEARCH locations grouped by file), re-matching
  each recorded line at apply time (drift-safe); active doc via the undoable
  `ReplaceWholeDocument` path, open buffers in memory + modified flag,
  closed files on disk. Panel gained a Replace input + "Replace All" button
  (results refresh after). Probe: `project_replace_probe`.
- **Diff view**: `services/diff_engine.zia` — bounded Myers O(ND) with
  aligned side-by-side row builder (SAME/CHANGED/ADDED/REMOVED, exact
  reconstruction proven by probe `diff_view_probe`). `ui/diff_view.zia` —
  reusable floating-panel overlay, theme-colored rows, close/Escape. Entry
  points: SCM "Side-by-Side Diff" button (new `scm_git.StartShowHead`
  HEAD-vs-worktree job) and the `comparewithsaved` palette command
  (saved-vs-buffer). Intraline char-LCS emphasis staged as follow-up.
- **Split restore**: `session.splitActive` flag saved on exit, re-applied
  after restore (`SplitRight` when needed). Shared-buffer sync between panes
  remains the documented v1 limit.
- **Tab drag-reorder**: the C tabbar + RT polling surface (`WasReordered`/
  `GetReorderedFrom/To`) already existed but was never consumed — added
  `DocumentManager.MoveDocument` (active follows its new index) and the
  main-loop consumption, aligning doc order with the visual reorder.
- **Breadcrumb symbols**: on caret-line change in Zia documents the
  breadcrumb shows `path › EnclosingFunction` via the pure
  `function_scan.EnclosingZiaFunctionName`.
- Model-level probe: `editor_depth_probe` (reorder semantics, split-flag
  round-trip, enclosing-function inference). Label total: 45 green.

## 3. Runtime surface

Possible `AttachSharedBuffer` (see §2.3) and the tabbar reorder event —
additive, through the Plan 04 consolidated ADR + full checklist.

## 4. Tests / verification (exit gate)

New probes registered with the `zannastudio` label:
`project_replace_probe.zia` (multi-file replace incl. a dirty open buffer;
undo restores), `diff_view_probe.zia` (engine goldens on synthetic inputs +
view wiring), `split_restore_probe.zia`, `tab_reorder_probe.zia`,
`breadcrumb_symbols_probe.zia`. C test for tabbar drag in
`src/lib/gui/tests/`. Incremental build + targeted ctest;
`build_ide.sh` loop for iteration.

## 5. Risks

- Replace-in-dirty-buffer edge cases — single code path through the document
  manager is the design rule.
- Shared-buffer scope creep — the §2.3 decision gate bounds it.
- Diff performance on large files — Myers with the standard d-path bound +
  fall back to line-hash prefilter; budget in the probe.
