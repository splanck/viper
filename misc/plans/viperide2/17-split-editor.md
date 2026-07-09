# Plan 17 — Split editor (two panes side-by-side)

## 1. Objective & scope

Add a two-pane vertical split: two CodeEditor views side-by-side, each with its
own active document, with commands "Split Editor Right", "Focus Other Pane",
"Close Split" and click-to-focus. This is the most structural ViperIDE plan —
the entire app currently assumes exactly one editor widget.

**Depends on:** Plan 21 (EditorBuffer — panes attach buffers) and Plan 10
(buffer-swap tab switching — the switch machinery this plan generalizes).

**Scope decisions (v1, keep it shippable):**

- Exactly two panes max, vertical split (side-by-side). No grids, no nested splits.
- **One shared tab strip** (v1): the tab bar lists all open documents; clicking
  a tab opens that document in the *focused* pane. No per-pane tab strips.
- The same document MAY be open in both panes: both attach the same
  EditorBuffer — edits appear in both. **Constraint from plan 21:** cursor/
  selection/scroll live in the buffer, so two views of one buffer share caret
  state in v1. Acceptable (document it); per-view carets are a follow-up.
- Semantic controllers (completion/hover/signature/tokens/diagnostics) operate
  on the **focused pane only** — exactly today's behavior, just retargetable.
- Find bar, minimap, breadcrumb, status bar track the focused pane.

**Out of scope:** horizontal splits, drag-tab-to-split, per-pane tab bars,
independent carets on a shared buffer, >2 panes.

## 2. Current state (verified anchors)

The single-editor assumption, enumerated (this list is the work plan):

- `EditorEngine` wraps ONE widget: `expose GUI.CodeEditor editor`
  (`viperide/src/editor/editor_engine.zia:36`), created in `Setup(parent)`
  (`:53-58`) into `shell.workbench.editorRow` (`main.zia:176`).
- Direct `engine.editor` consumers (grep `engine.editor` +
  `\.editor\b` across `viperide/src` — audit EVERY hit):
  minimap binding (`main.zia:211-213`), findBar binding (`:289`),
  completion setup (`:180`), gutter pump (`debug_commands.PumpGutter`,
  `:368`), status cursor counts (`:634`), `ClearCursors` (`:570`),
  and every controller that took `engine.editor` at Setup.
- Controllers hold the editor via setup-time references:
  `completer.Setup(engine.editor, shell.app)` (`main.zia:180`) — pattern
  repeats for others through `languageFrame` (`app/language_tool_frame.zia`).
- Layout: `editorRow` is an HBox holding editor + minimap
  (`ui/workbench_shell.zia:143-147`, `main.zia:211-213`).
- Tab switch path (`main.zia:562-582`) loads into "the" engine.
- Session (`core/session.zia`) stores tabs + ONE active tab; no pane concept.
- `SaveEditorState(docMgr, engine)` (`commands/file_commands.zia`) snapshots
  "the" editor into the active document.

## 3. Design

### 3.1 EditorEngine → EditorPanes

Refactor `EditorEngine` into a pane-aware engine, keeping its public surface so
most call sites survive:

```zia
class EditorPane {
    expose GUI.CodeEditor editor;
    expose Integer docIndex;        // docMgr index shown here, -1 = none
}

class EditorEngine {                 // name kept; semantics: "the focused pane"
    expose List panes;               // 1 or 2 EditorPane
    expose Integer focusedPane;      // 0/1
    expose GUI.CodeEditor editor;    // ALIAS kept in sync = panes[focused].editor
    ...existing methods operate on the focused pane...
    expose func SplitRight(parent: GUI.HBox);   // create pane 2 (same doc)
    expose func CloseSplit();                   // detach + destroy pane 2
    expose func FocusOtherPane();
    expose func PaneCount() -> Integer;
}
```

Keeping `engine.editor` as a synced alias is the compatibility crux: every
existing consumer keeps working against the focused pane, and the audit
(§2) becomes "which consumers must instead iterate panes or re-bind on focus
change" — a much smaller set:

- **Re-bind on focus change:** minimap (`minimap.BindEditor`), findBar
  (`findBar.BindEditor`), completion/hover/signature popup anchors (they took
  the widget at Setup — add a `Retarget(editor)` to each controller, called
  from a single `OnFocusedPaneChanged` hook in `main.zia`).
- **Iterate panes:** gutter breakpoint sync (`languageFrame.SyncBreakpoints` /
  `PumpGutter` — markers must show in both panes when both show the same file),
  diagnostics inline-highlight application (buffer-resident after plan 21 —
  automatic; verify), font/settings application
  (`view_commands.applyShellFont`/`applyPersistentSettings` — apply to all panes).
- **Focused-pane-only (no change):** status bar, breadcrumb, save flows,
  semantic queries, tab-switch loading.

### 3.2 Focus tracking

Each frame, if pane 2 exists: whichever editor widget has keyboard focus is the
focused pane (`Viper.GUI.Widget.IsFocused` exists, `runtime.def:2284`). On
change → `OnFocusedPaneChanged`: sync alias, re-bind minimap/findBar/popups,
update status/breadcrumb/tab highlight to the focused pane's document. Clicking
a pane focuses it (widget focus follows click already — verify for CodeEditor;
`codeeditor_can_focus`, `vg_codeeditor_input.inc:1966-1968`).

### 3.3 Document/tab semantics (shared strip)

- `docMgr.activeIndex` continues to mean "document of the focused pane".
- Tab click: load into focused pane (existing path, `main.zia:562-582`).
- `SplitRight`: pane 2 opens the focused pane's document (attach same buffer).
- Closing a document that is open in both panes: both detach; pane 2 falls back
  to the next open doc or empties (`docIndex = -1`, editor hidden/read-only
  placeholder). Close-flow audit in `file_commands` close/save-all paths:
  replace "the editor shows doc X" assumptions with pane queries.
- `SaveEditorState`: snapshot BOTH panes' cursor/scroll into their documents
  (buffer carries these post-plan-21; the call may become a no-op — verify and
  simplify).
- Session: persist `splitOpen` + pane-2 document path + focused pane (three
  keys in `session.zia`); restore after tabs load.

### 3.4 Layout + commands

- `SplitRight` adds a second `GUI.CodeEditor` to `editorRow` (both
  `SetFlex(1.0)`; a 1px separator via spacing is fine v1 — a draggable
  SplitPane inside editorRow is a nice-to-have; use `GUI.SplitPane` horizontal
  if trivial, else defer).
- Minimap stays single (bound to focused pane) — two minimaps is clutter; note
  in code.
- Commands (`command_catalog.zia` + `view_commands.zia` handlers):
  `spliteditorright` (Ctrl+\\), `focusotherpane` (Ctrl+K Ctrl+Arrow is
  chord-heavy — use a single available shortcut; check the catalog for
  conflicts), `closesplit`. Palette entries automatic via catalog.

## 4. Implementation steps

1. **Audit first:** `grep -rn "engine\.editor" viperide/src | wc -l` and
   classify every hit into the three buckets of §3.1. Write the bucket list
   into the PR/summary before coding.
2. `EditorPane` extraction + alias mechanics; single-pane mode must be
   byte-identical (all probes green before proceeding).
3. `SplitRight/CloseSplit/FocusOtherPane` + layout + focus tracking +
   `OnFocusedPaneChanged` re-binding hook + `Retarget` on
   completion/hover/signature controllers.
4. Tab/close/save-flow semantics (§3.3) + settings/font application to both
   panes + breakpoint gutter iteration.
5. Session persistence keys + restore.
6. Probe `viperide/src/probes/split_editor_probe.zia`: split; open different
   docs in each pane; assert focused-pane routing (typing goes to focused;
   status doc label follows focus); same doc in both panes → edit in one
   visible in other (revision equal, text equal); close split; close doc open
   in both. Register `LABELS "zia;viperide;editor;shell"`.
7. Manual sweep: split with large file; completion/hover/signature in each
   pane; find bar retargeting; breakpoint toggle visible in both panes (same
   file); debugger current-line marker (focused pane); save-all; session
   restore with split open; theme/zoom changes hit both panes.
8. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/editor/editor_engine.zia` — pane refactor (split into
  `editor_pane.zia` if >500 lines).
- `viperide/src/main.zia` — focus hook, split wiring, tab-switch path.
- `viperide/src/app/language_tool_frame.zia` — retarget plumbing, per-pane
  breakpoint sync.
- `viperide/src/editor/completion.zia`, `hover.zia`, `signature.zia` —
  `Retarget(editor)`.
- `viperide/src/commands/view_commands.zia`, `command_catalog.zia` — commands.
- `viperide/src/commands/file_commands.zia` — close/save-flow audit.
- `viperide/src/core/session.zia` — persistence.
- `viperide/src/ui/workbench_shell.zia` — editorRow hosting (if separator/split
  widget used).
- `viperide/src/probes/split_editor_probe.zia` — **new**; `src/tests/CMakeLists.txt`.
- `viperide/docs/status.md` + `architecture.md` — document panes.

## 6. Testing

Step-2 gate (single-pane byte-identical, full probe suite) is the safety net
for the refactor; the split probe covers new behavior; the manual sweep
(step 7) covers the long tail of consumers found in the step-1 audit.

## 7. Acceptance criteria

- Split/unsplit/focus-switch work via commands and palette; typing, IntelliSense,
  find, minimap, status all follow the focused pane correctly.
- Same-document-in-both-panes edits stay in sync (shared buffer); different
  documents are fully independent.
- Session restores the split. Single-pane behavior unchanged when never split.
- Every probe green.

## 8. Repo rules (read before starting)

- Zia-only plan (given plans 21/10 landed): rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; respect file-size budgets (extract
  `editor_pane.zia`).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
