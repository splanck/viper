# Plan 12 — File-tree drag-and-drop moves

## 1. Objective & scope

Let users move files and folders by dragging them in the Explorer tree, wired
through the same safe-path machinery the explorer's rename/delete flows already
use. Dropping `a.zia` onto folder `src/util/` moves the file, updates open
tabs/documents, watchers, the project index, and Quick Open caches, with a
confirmation overlay for overwrites.

**Depends on:** Plan 22 (TreeView DnD runtime surface: `SetDragDropEnabled`,
`WasDropReceived`, `GetDropSourceData/GetDropTargetData/GetDropPosition`,
`ClearDrop`).

**In scope:** Explorer wiring, move execution + rollback-free ordering, open
document/tab path fixups, index/watcher/session refresh, conflict overlay,
multi-root guard rails, probe.

**Out of scope:** copy-on-drag (Ctrl modifier), drag between IDE windows,
dragging INTO the editor, reordering within a folder (trees are
sorted/filesystem-ordered; only INTO drops are enabled per plan 22).

## 2. Current state (verified anchors)

- Tree nodes carry **absolute paths as node data**
  (`viperide/docs/architecture.md` Projects section;
  `viperide/src/core/project_manager.zia` populates via
  `PopulateTree`/`PopulateTreeChildren`, `:289,329`).
- Explorer file operations live in `viperide/src/app/explorer_action_runner.zia`
  (394 lines — create/rename/delete/duplicate flows) with UI overlays in
  `viperide/src/ui/explorer_actions.zia` (name input + description/detail
  labels, `:107-114,296`), file ops helpers in `core/project_file_ops.zia`.
- Rename already solves the hard parts this plan reuses: adjusting open
  documents' paths, watcher reset, index update, session recents — read the
  rename flow in `explorer_action_runner.zia` end-to-end first and extract any
  duplicated pieces into shared helpers rather than copying.
- Post-change refresh: `projMgr.Refresh`/repopulation + `indexer` +
  `workspace_watcher.PumpWorkspaceWatcher` handle externally-moved files
  already (`main.zia:624-627`) — a move done *by* the IDE should update state
  directly instead of waiting for watcher detection.
- Context-menu dispatch pattern for explorer actions:
  `app/context_menu_dispatcher.zia` + `explorerOverlay` pump in `main.zia:405-409`
  — the DnD pump slots in beside these.
- Workspace roots: multi-root support exists (`project_manager` "additional
  workspace roots") — moving across roots is legal filesystem-wise; keep it
  allowed but confirm via overlay.

## 3. Design

### 3.1 Enable + pump

In `projMgr.Setup` (after tree creation): `sidebarTree.SetDragDropEnabled(true)`.

New pump function `explorer_action_runner.PumpTreeDrops(shell, projMgr, docMgr,
engine, tabs, sessionMgr, fileWatchCtrl, indexer, explorerOverlay)` called once
per frame from `main.zia` next to `PumpContextMenus` (`main.zia:405`):

```zia
if shell.workbench.sidebarTree.WasDropReceived() {
    var src = shell.workbench.sidebarTree.GetDropSourceData();
    var dst = shell.workbench.sidebarTree.GetDropTargetData();
    shell.workbench.sidebarTree.ClearDrop();
    BeginMove(src, dst, ...);
}
```

### 3.2 Validation (before any I/O)

Reject with a status-bar + toast reason (pattern: `main.zia:393-396`):

- `src == dst`, `dst` is not a directory (defense — plan 22's into-only should
  guarantee it), `dst` is inside `src` (moving a folder into its own subtree —
  compare normalized path prefixes), `src` is a workspace root itself,
  destination path `dst + "/" + basename(src)` equals `src` (no-op drop on own
  parent).
- Destination exists → confirmation overlay (reuse the delete-confirmation
  overlay style in `explorer_actions.zia`; message
  "Replace existing '<name>' in '<folder>'?"); on confirm proceed as overwrite
  (move-with-replace), on cancel abort. Never silently overwrite.

### 3.3 Execution order (crash-safe, watcher-quiet)

1. If any open modified document lives under `src` (file or folder subtree):
   prompt to save first (reuse the save-before-build preflight pattern in
   `file_commands`), or proceed and rewrite paths of modified buffers without
   losing content (documents are memory-resident; path fixup is enough — choose
   this: no forced save, just fixup).
2. Perform the filesystem move via the same primitive rename/duplicate use in
   `project_file_ops.zia` (grep for the move/rename helper — POSIX rename
   semantics through `Viper.IO`; cross-device fallback: if the runtime's move
   fails across filesystems, fall back to copy+delete only for files; for
   folders across devices, surface an error — keep scope tight).
3. Fix up open state for every affected document (exact-path match for a file
   drop; prefix match for a folder drop): `doc.filePath`, tab tooltip/label
   (`tabs.SetTooltip`), breadcrumb if active, `sessionMgr` recents, watcher
   re-arm (`fileWatchCtrl.ResetPendingReload()` — the rename flow shows the
   exact calls), SCM view refresh if visible.
4. Tell the index: `indexer` remove/add path (mirror what the rename flow
   calls), invalidate Quick Open cache via the existing project-manager hook.
5. Refresh the two affected tree branches (`projMgr` targeted refresh if
   available; else full `PopulateTree()` — acceptable v1).
6. Status bar: "Moved <name> to <folder>".

### 3.4 UX details

- The tree highlights valid INTO targets during hover (already painted by the
  widget, `vg_treeview.c:756-766`).
- Escape cancels an in-progress drag (widget-level; verify plan 22 covers it —
  if not, note it as a plan-22 follow-up, not blocking here).

## 4. Implementation steps

1. Read the full rename flow in `explorer_action_runner.zia` +
   `project_file_ops.zia`; extract shared "path moved" fixup helper if rename
   inlines it.
2. Enable DnD in `projMgr.Setup`; add `PumpTreeDrops` + `main.zia` call.
3. Validation set (§3.2) with unit-style probe coverage (pure path checks —
   put them in `services/` if a path-utils module exists; grep
   `services/locations.zia`/`file_utils.zia` first).
4. Execution (§3.3) for files; then folder subtrees (path-prefix fixups over
   `docMgr` documents).
5. Overwrite-confirmation overlay flow.
6. Probe `viperide/src/probes/tree_move_probe.zia`: temp workspace with
   `a/one.zia`, `b/`; simulate drop (plan 22's probe shows how to drive the
   gesture; or call the runner's `BeginMove` directly for the logic layer —
   do BOTH: gesture smoke via widget events + direct-call edge cases);
   assert file moved on disk, open document path updated, no-op/invalid cases
   rejected, overwrite prompts. Register `LABELS "zia;viperide;explorer"`.
7. Manual: move an open modified file; move a folder containing the active
   file; drop onto own parent (no-op), onto own subtree (reject);
   cross-root move; verify SCM view and Quick Open see the new path.
8. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/core/project_manager.zia` — enable DnD; targeted branch refresh.
- `viperide/src/app/explorer_action_runner.zia` — pump + move logic.
- `viperide/src/core/project_file_ops.zia` — move primitive (if not present).
- `viperide/src/ui/explorer_actions.zia` — overwrite confirmation overlay.
- `viperide/src/main.zia` — pump call.
- `viperide/src/services/` — path-relation helpers if not already present.
- `viperide/src/probes/tree_move_probe.zia` — **new**; `src/tests/CMakeLists.txt`.
- `viperide/docs/status.md` — Explorer feature list line.

## 6. Testing

Probe (step 6) covers logic + gesture smoke; the rename-flow regression probes
(`file_tree_probe.zia`, `context_menu_probe.zia`) must stay green — shared
helpers extracted in step 1 are exercised by both.

## 7. Acceptance criteria

- Drag file → folder moves it; open tab keeps content, shows new path; undo
  history unaffected; watchers don't fire a spurious "file missing" conflict.
- Folder drag moves the subtree and fixes every open document under it.
- Invalid drops (subtree cycles, roots, self) are rejected with a visible reason.
- Overwrite requires explicit confirmation.

## 8. Repo rules (read before starting)

- Zia-only plan (runtime surface comes from plan 22): rebuild with
  `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; shared rules go down into `services/`, not
  duplicated in command/UI modules (ownership rules in architecture.md).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
