# Plan 01 — Per-document undo history (undo survives tab switches)

## 1. Objective & scope

Fix the highest-impact silent data-loss bug in ViperIDE: switching tabs destroys
the undo/redo history of the document you switched away from. After this plan,
every open document keeps its full undo stack (plus cursor, selection, scroll,
folds) for the lifetime of the IDE session, across any number of tab switches.

**Depends on:** Plan 21 (`Viper.GUI.EditorBuffer`) — this plan is its first
consumer. A fallback approach without plan 21 is described in §3.4 but the
EditorBuffer path is the intended one.

**In scope:** ViperIDE document/tab plumbing (`Document`, `DocumentManager`,
`EditorEngine`, tab-switch path in `main.zia`, close/save flows). Probe coverage.

**Out of scope:** making tab switches *cheap* (plan 10 — same substrate, its own
plan), split editors (plan 17), undo granularity (plan 02).

## 2. Current state (verified anchors)

- One shared editor widget for all tabs: `EditorEngine.Setup` creates a single
  `GUI.CodeEditor` (`viperide/src/editor/editor_engine.zia:53-58`); `main.zia`
  owns one `EditorEngine` for the whole app.
- Tab switch path (`viperide/src/main.zia:562-582`): `tabBar.WasChanged()` →
  `file_commands.SaveEditorState(docMgr, engine)` → `docMgr.SetActive(newIdx)` →
  `engine.LoadDocument(switchDoc)` → `engine.editor.ClearCursors()`.
- `LoadDocument` calls `editor.SetText(doc.content)`
  (`editor_engine.zia:68`), and `SetText`'s documented contract is "Replace the
  entire document with new text **and clear undo/redo history**"
  (`src/lib/gui/include/vg_ide_widgets_editor.h:377-380`).
- `SaveToDocument` (`editor_engine.zia:107-115`) captures only
  content/cursorLine/cursorCol/scrollLine into the `Document` — history,
  selection, and folds are simply lost.
- `Document` fields: `core/document.zia` stores path, name, language, kind,
  content, flags, disk metadata, cursor, scroll, notification state
  (`viperide/docs/architecture.md` "Documents" section).
- Session restore reloads content from disk/recovery and re-`SetText`s — undo
  history across IDE restarts is out of scope (industry-standard behavior).

## 3. Design

### 3.1 Ownership: `Document` owns an `EditorBuffer`

Add to `core/document.zia`:

```zia
expose GUI.EditorBuffer buffer;   // null until first opened in the editor
```

(`document.zia` must `bind Viper.GUI as GUI;` — new dependency, acceptable: the
document layer already stores editor-coupled state like cursor/scroll.)

Lifecycle:

- Created lazily the first time a document is loaded into the editor:
  `buffer = GUI.EditorBuffer.New(doc.content)`.
- From then on, **the buffer is the source of truth for content** while the
  document is open; `doc.content` becomes a snapshot updated by
  `SaveToDocument` (unchanged callers keep working).
- Dropped (set null) when the tab closes — `DocumentManager` close path — so
  memory for closed files is reclaimed.

### 3.2 New switch path in `EditorEngine`

Replace the body of `LoadDocument` (`editor_engine.zia:64-101`) with:

```zia
expose func LoadDocument(doc: document.Document) {
    if doc == null { return; }
    if doc.buffer == null {
        doc.buffer = GUI.EditorBuffer.New(doc.content);
    }
    var previous = editor.AttachBuffer(doc.buffer);
    // previous belongs to the previously active Document, which already holds
    // the same handle — nothing to do with it.
    editor.SetReadOnly(doc.isReadOnlyPreview);
    // language / semantic-token handling unchanged:
    ...SetLanguage mapping as today...
    cachedTextSnapshot = "";          // invalidate; revision-keyed cache refills lazily
    cachedTextRevision = -1;
}
```

Notes:

- Cursor/scroll restore code (`editor_engine.zia:90-101`) is **deleted** — the
  buffer carries caret/selection/scroll (plan 21 §3.1), which is strictly better
  (selection + column-affinity survive too). Keep `doc.cursorLine/scrollLine`
  fields updated in `SaveToDocument` for session persistence only.
- `main.zia:570` `engine.editor.ClearCursors()` after switch is **deleted** —
  extra cursors are per-buffer now and intentionally survive.
- `SaveToDocument` still snapshots `doc.content = GetTextSnapshot()` so every
  existing consumer (save, search-in-open-docs, session) is untouched.
- `LoadDocument` for a **read-only preview reload** (file changed on disk,
  `file_watch_controller` reload path) must refresh content: add
  `expose func ReloadDocumentFromText(doc, text)` that does
  `editor.SetText(text)` (history clear is CORRECT for an external reload) —
  find those callers via grep for `LoadDocument` and audit each one:
  tab switch and session restore keep buffers; external-reload and
  revert-to-disk call the reload variant.

### 3.3 Close/save flows

- `DocumentManager` close: null the buffer reference (GC frees; plan 21
  finalizer rules make this safe whether or not currently attached — the active
  document is never closed without switching first; audit
  `file_commands.zia:893` and the close-tab handlers to confirm ordering, and
  detach explicitly if a case closes the active tab:
  `engine.editor.AttachBuffer(nextDoc.buffer)` happens before the old ref drops).
- Save: `engine.ClearModified()` today (`editor_engine.zia:134-139`) — route to
  `doc.buffer.ClearModified()` semantics via the widget as today; verify
  `IsModified` still reads through the attached buffer (plan 21 keeps
  `modified` in the buffer, so the existing widget calls remain correct).

### 3.4 Fallback (only if plan 21 is not available)

Store a bounded snapshot of the widget's history per document by adding C-side
`vg_codeeditor_detach_history()/attach_history()` (history handle only). This
preserves undo but not folds/selection and still pays full `SetText` cost.
Do not build this if plan 21 is merged — it is strictly worse.

## 4. Implementation steps

1. Add `buffer` to `Document` + null-handling in `DocumentManager` open/close.
2. Rewrite `EditorEngine.LoadDocument` per §3.2; add `ReloadDocumentFromText`;
   audit every `LoadDocument` caller (grep in `viperide/src`): tab switch
   (`main.zia:569`), session restore (`main.zia:273-276`), open-file flow
   (`explorer_action_runner.zia`), external reload (`file_watch_controller` /
   `file_commands`), revert. Route reload-type callers to the reload variant.
3. Delete the now-wrong `ClearCursors` at `main.zia:570`.
4. Update `SaveEditorState`/`SaveToDocument` semantics comment (content is a
   snapshot; buffer is truth while open).
5. Zia probe `viperide/src/probes/undo_across_tabs_probe.zia`:
   open two documents; type into A (via editor insert API); switch to B; type;
   switch back to A; `editor.Undo()`; assert A's text reverted and B untouched;
   also assert selection/scroll survived the round trip. Register in
   `src/tests/CMakeLists.txt`, `LABELS "zia;viperide;editor"`.
6. Run the full viperide probe suite — tab-related probes
   (`phase0_phase1_probe`, `smoke_probe`, `editor_hot_path_probe`) must pass.
7. `./scripts/build_ide.sh`, manual smoke: open real project, edit two files,
   switch repeatedly, undo in both; close-tab prompt flow; save-all;
   external-modify a file and accept reload (history correctly resets there).
8. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/core/document.zia` — buffer field.
- `viperide/src/core/document_manager.zia` — close-path release.
- `viperide/src/editor/editor_engine.zia` — LoadDocument rewrite + reload variant.
- `viperide/src/main.zia` — remove ClearCursors; switch-path audit.
- `viperide/src/commands/file_commands.zia` — reload/revert call sites.
- `viperide/src/app/file_watch_controller.zia` — external-reload call site.
- `viperide/src/probes/undo_across_tabs_probe.zia` — **new**; `src/tests/CMakeLists.txt`.
- `viperide/docs/status.md` + `architecture.md` — document the new behavior
  (single line each; keep docs honest).

## 6. Testing

- New probe (step 5) is the regression net for the bug itself.
- Existing probes cover the untouched flows; the external-reload path is
  exercised manually (step 7) and by any existing file-watch probe coverage.

## 7. Acceptance criteria

- Type in A → switch → switch back → Ctrl+Z undoes the typing. Redo works too.
- Selection, extra cursors, folds, and scroll position survive tab switches.
- External-change reload and revert-to-disk still reset history (correct).
- Closing a tab frees its buffer (no growth after open/close cycles — observe
  via repeated open/close of a large file and process RSS).
- All viperide-labeled ctest probes pass.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Zia-only changes rebuild with `./scripts/build_ide.sh` alone.
- Full Viper header on any touched C file; Zia modules follow
  `viperide/docs/architecture.md` comment style.
- Zia code binds namespace aliases, never inline `Viper.X.Y.Z()`.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
