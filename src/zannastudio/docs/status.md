# Zanna Studio Current Status

Last reviewed against source: 2026-07-22.

This file is the current-state reference for Zanna Studio. It intentionally avoids
future-phase language and records limitations in the same place as shipped
behavior.

## Summary

Zanna Studio is usable as a code editor and project workbench for Zia projects, with
partial BASIC support, build/run integration, a VM-backed debugger path, an
integrated terminal, and lightweight Git operations.

It is not yet a polished product-complete IDE. The largest current gaps are:

- Scene documents are recognized but do not have a visual scene editor.
- Bottom tool surfaces now share bounded stable-row models, but they are still
  listbox/output-pane based rather than fully virtualized, dockable workbench
  views.
- Source Control covers status, staging, commit, paged history, per-commit
  diffs, queued jobs, and in-app credential prompts for push/pull, but is
  still not a full Git client (no merge/rebase/conflict workflows).
- BASIC semantic navigation and rename are implemented by the IDE-side scanner,
  not by the Zia project index or external BASIC server.
- Some workflow prompts and overlays are still modal or command-palette based,
  although project search now uses a docked non-modal panel.
- The application source still has several oversized coordinator modules.

## Current Product Narrative

The most accurate way to describe Zanna Studio today is "a functional Zanna
workbench whose strongest path is Zia code editing." The editor can open real
projects, keep multiple files alive across sessions, run semantic services, and
drive the compiler/debugger toolchain. A Zia developer can use it for daily
editing in a small or medium project, especially when the workflow is edit,
search, build, run, diagnose, and debug one active program.

The roughness shows up when the workflow starts to look like a mature IDE. Some
panels are still row dumps instead of rich work surfaces. Common short inputs
use integrated overlays, but some operations still rely on prompt-style input.
The debug substrate is real: persistent watches, and structured expansion of
collections and class instances (field-by-field, via the compile-time layout
sidecar of ADR 0138). BASIC support is intentionally honest but incomplete.
Source Control covers daily local Git plus commit history, queued operations,
live push/pull output, and in-app credential prompts, but it does not have the
merge/rebase workflow depth or conflict recovery of a full client. Scene
support exists in the runtime, and the IDE recognizes scene file extensions,
but no visual editor is mounted.

This distinction matters for documentation and release notes. Zanna Studio should
not be described as a complete scene editor, complete SCM client, or full
multi-language IDE. The terminal now emulates the sequences full-screen
programs (vim, less, htop) actually emit — alternate screen, scroll regions,
cursor modes, bracketed paste, and status replies — but VT coverage beyond
that pinned table is not claimed. It should be described as a growing IDE with
clear working slices and clearly documented gaps.

## What "Implemented" Means Here

In this document, "implemented" means that the feature has a user-visible path,
source ownership, and at least some regression coverage. It does not always mean
the feature is polished or complete by mature IDE standards.

"Partial" means that the feature has working pieces but should not be marketed
as complete. Partial features need explicit limitations in docs and UI.

"Not present" means that users may see related groundwork in code or runtime
APIs, but Zanna Studio does not provide the user-facing workflow yet.

## User Impact Summary

For Zia and BASIC developers, the biggest strengths are:

- The editor understands source well enough for completion, diagnostics, hover,
  signature help, symbols, semantic navigation, references, and rename.
- Project search, Quick Open, workspace symbols, and recent files make normal
  navigation practical.
- Build/run/debug are wired to the Zanna toolchain without leaving the IDE.
- Session restore and recovery reduce the risk of losing active work.

For a game developer expecting scene tooling, the runtime data foundation exists
below the IDE, but the IDE user experience is not there yet.

For a user expecting a polished workbench, the rough areas are mostly around
panel density, remaining prompt flows, Source Control conflict workflows, and
panel virtualization for very large result sets.

## Feature Matrix

| Area | Status | Notes |
| --- | --- | --- |
| Text editing | Implemented | Multi-tab CodeEditor, undo/redo, selections, comments, formatting, folding, minimap option. |
| Split editor | Implemented (v1) | Two side-by-side panes ("Split Editor Right", "Focus Other Editor Pane", "Close Editor Split", click-to-focus). Each pane owns a distinct document and the focused pane drives the active tab, typing, IntelliSense, find, minimap, status, save, and recovery state. Opening a document already visible in the other pane focuses its existing owner, preventing divergent buffers and stale overwrites. Split-active state is restored when at least two documents are open. v1 limits: exactly two panes; open a second document before splitting; same-document multi-view awaits shared-buffer runtime support. |
| Zia IntelliSense | Implemented with limits | Completion, diagnostics, hover, signature help, symbols, definition, references, rename, workspace symbols. |
| BASIC IntelliSense | Implemented with limits | Completion, diagnostics, hover, document symbols, scanner-backed definition, references, rename, workspace symbols, call hierarchy, and signature help. |
| Plain text | Implemented | Opens unknown/text-like files as text without semantic features. |
| Scene files | Partial | `.scene` and `.level` are detected, saved, restored, and filtered, but display as text. |
| Project explorer | Implemented with limits | Demand-loaded, scrollable tree; multi-root support; Quick Open cache; file actions; ignores. Rename/move preserve live editor buffers and undo state, while delete releases any removed split-pane owner. |
| Search | Implemented | Docked project/folder search panel with a compact-window minimum results viewport, runtime-paged file discovery, per-frame file/byte budgets, literal/regex, case/word filters, include/exclude filters, grouped results, and generation-scoped frame-sliced Replace All with bounded atomic closed-file writes. |
| Build/run | Implemented | Argument-vector jobs, project manifest overrides, streamed bounded output, JSON diagnostics. |
| Debugging | Implemented with UX gaps | External VM debug adapter, breakpoints, stepping, pause, async restart, run to cursor, locals, call stack, evaluate, watches, conditions, logpoints; structured expansion of lists/seqs/maps and class-instance fields with value previews. |
| Terminal | Implemented with limits | PTY-backed shell in OutputPane terminal mode: alternate screen, DECSTBM scroll regions, IL/DL/ICH/DCH/ECH, tab stops, cursor visibility, bracketed paste, application cursor keys, DSR/DA replies, SGR 16/256/truecolor + reverse. Coverage is pinned to the vim/less/htop sequence table, not full VT. |
| Source Control | Implemented with limits | Async Git status, stage/unstage, commit, per-path diff, worker-computed/incrementally rendered side-by-side diffs, paged commit history with per-commit files and diffs, queued serialized jobs, PTY-backed push/pull with live output and in-app credential prompts. No merge/rebase/conflict workflows. |
| Settings | Implemented | Platform config path, theme, editor behavior, auto-save, save-before-build, session options, settings search, rebindable keyboard shortcuts, and debounced bottom-panel drag sizing. The body is vertically scrollable with a fixed action footer; compact windows give Preferences the full workbench lane and stack descriptions above controls without horizontal overflow. |
| Session restore | Implemented | Project, tabs, cursor/scroll, recent files/projects, bounded recovery text, and painted caller-budgeted startup restoration. |
| File watching | Implemented with limits | Active file watcher, inactive document polling, missing/deleted/moved-file conflict state, and capped recursive workspace watcher set with fallback scans. |
| Visual polish | Implemented with limits | Zanna-brand palettes (WCAG-gated), scalable vector icons across toolbar/tree/tabs/status, smooth scrolling, gamma-correct text with ligatures, and viewport-bounded welcome/About/Preferences/diff, command, and semantic popup surfaces. Focus-taking Settings, About, explorer, breakpoint, command-input, and diff surfaces are mutually exclusive with popup menus, preventing stacked panels and ambiguous Enter/Escape routing. Chrome text, floating overlays, wrapped output, and responsive tool tabs share one effective-scale coordinate space without applying user zoom twice. Long list rows—including compact Recent paths—use explicit ellipsis and expose their complete unmodified text on hover instead of ending at a hard clip. The native workbench minimum starts at 720 by 520 and grows with whole-UI zoom, contracting against a desktop-chrome safety margin when the display cannot fit that floor. A requested minimap is temporarily suppressed below a useful editor-lane width and restored automatically when the lane expands, without overwriting the user's preference. Remaining density work is tracked per panel. |
| Cross-platform | Intended | Runtime adapters exist for process, PTY, GUI; display/runtime behavior still needs regular platform smoke. |

## Language Support

### Zia

Zia is the primary supported language. Current Zia features include:

- Syntax highlighting and semantic tokens.
- Completion with popup filtering, commit behavior, snippets, docs, runtime
  metadata, workspace-symbol completions, and stale-result rejection.
- Hover, signature help, and overload navigation.
- Live diagnostics and explicit "Run Check Now".
- Problems panel integration with diagnostic navigation.
- Non-blocking, revision/caret-gated fix-it application for supported
  structured diagnostics.
- Create Missing Bind for known runtime aliases and unambiguous project-file
  binds. Project discovery is bounded and asynchronous, and refuses ambiguous,
  incomplete, or changed candidate snapshots.
- Non-blocking Suppress Warning insertion for supported warnings.
- Definition, references, incoming calls, outgoing calls, and rename through
  `Zanna.Zia.ProjectIndex`. Project queries run on an owned background worker;
  delayed results require the same tab, revision, caret, workspace, and index
  generation, and large result sets render over multiple frames.
- Organize Binds.
- Extract Local Variable, Extract Function, and Inline Local Variable for
  deliberately conservative cases.
- Document formatting and selection formatting.

Known Zia limits:

- Workspace indexing is lazy and cooperative. A semantic command waits without
  blocking while the index warms up, then refuses the query if any source was
  unreadable/oversized or the 20,000-file/64 MB workspace ceilings were reached.
- Reference and call publication retains at most 2,000 results; rename refuses
  to apply when the reference ceiling is exceeded. Closed-file edits carry the
  expected symbol text so delayed content changes cancel the refactor.
- Refactors are intentionally conservative and reject many legal programs.
- Some UI panels use string display rows even when the underlying location data
  is structured.

### BASIC

BASIC support is implemented through a mixed runtime/IDE path:

- Completion.
- Diagnostics.
- Hover.
- Document symbols.
- Scanner-backed Go to Definition.
- Scanner-backed Find References.
- Scanner-backed Rename Symbol.
- Scanner-backed Workspace Symbols.
- Scanner-backed Signature Help.
- Scanner-backed incoming/outgoing call hierarchy.
- Project-wide definition, references, call hierarchy, and rename scans run on
  an owned background worker; unsaved open BASIC buffers override disk.
- Delayed results are rejected after a tab, caret, revision, or workspace-root
  change, and large References rows are painted over multiple frames.
- Formatting for supported line forms.
- Build/run through the same `zanna` toolchain path.

BASIC still has important limits:

- Semantic results come from `src/basic/semantic_scan.zia`, a lightweight
  scanner, rather than the compiler's full semantic model.
- Workspace scans are asynchronous and bounded, but are not backed by the Zia
  project index data structure. File/source/declaration ceilings can limit a
  navigation result; reference/call results cap at 1,000 rows.
- Rename refuses to apply when any workspace or reference limit was reached,
  and validates the scanned token text plus closed-file mtime before mutation.
- Ambiguous BASIC syntax and dynamic dispatch can still produce conservative or
  incomplete navigation results.

The command registry marks unavailable commands with language-specific reasons.

### Text And IL

Plain text, Markdown, JSON, and IL open as text buffers. The IDE provides core
editing, search, save, session, and file-watcher behavior, but no semantic
language service for these file kinds.

### Scene Files

`.scene` and `.level` files are recognized as scene documents. They participate
in open/save/session/file-filter flows, but there is no visual scene document
surface. Today they open as text.

Runtime scene support exists in `Zanna.Game2D.SceneDocument` and is covered by probes, but
Zanna Studio does not yet keep a scene handle per open document, does not have
scene-specific dirty/save/reload logic, and does not provide tile/object tools.

## Workbench Status

### Explorer

The explorer supports:

- Open folder.
- Add folder to workspace.
- Demand-loaded folder expansion with bounded directory pages, incremental
  natural sorting, and bounded tree-row publication.
- Open selected files.
- Create file/folder.
- Rename.
- Delete/move-to-trash flow.
- Duplicate.
- Copy path and relative path.
- Search in folder.
- Run file.
- Set project entry.
- Refresh.
- Project-specific and runtime workspace ignore rules.

Known limits:

- Tree operations use integrated overlays for create/rename/delete/duplicate;
  confirmation-heavy workflows still use dialogs.
- Ignore behavior is whatever `Zanna.Workspace.FileIndex` supports; do not
  assume full Git ignore semantics beyond what the runtime implements.
- Very large workspaces depend on cooperative tree/cache/index pumping; slow
  network filesystems can still delay an individual native directory operation.
- Quick Open and completion file discovery use runtime `FileIndex.Page` instead
  of treating the visible tree as a complete workspace snapshot.

### Bottom Panels

Current bottom panel tabs:

- Problems.
- Output.
- Search.
- References.
- Debug Console.
- Variables.
- Call Stack.
- Debug.
- Terminal.

Output rows are bounded by an OutputPane ring buffer plus a bounded row model.
Problems, Search, References, Debug Console, Variables, and Call Stack share a
bounded stable-row model. This prevents runaway UI memory in normal cases and
gives panel rows stable identities, but it is not the same as a fully
virtualized dockable workbench surface for very large logs/search results.

### Debugger

The debugger uses the external VM debug adapter:

```text
zanna run --debug-adapter <file>
```

Supported behavior:

- Launch active file.
- Set and persist breakpoints.
- Conditional breakpoints and logpoints.
- Continue, pause, step over, step in, step out.
- Run to cursor.
- Stop and restart.
- Restart waits for the previous adapter process to terminate before launching
  a replacement.
- Current-line gutter marker.
- Locals and call stack at stop points.
- Expression evaluation while stopped.
- Persistent watch expressions, shown in the Variables panel above locals.
- Command-palette watch management for add, remove selected, refresh, and clear.
- Variables panel rows are grouped through a `VirtualTree` model for Watches and
  Locals before being rendered into the current ListBox UI.
- Composite locals (lists, seqs, maps) are expandable: clicking a `▸` row loads
  its children asynchronously through the adapter's `variables` request, shows
  an immediate loading row, and publishes the reply on a later frame; nested
  containers expand one level at a time. Timed-out rows remain retryable.
  Expansion state is kept by variable name-path, so stepping re-opens the same
  nodes automatically.
- Class instances expand field-by-field with `{field=value}` previews on the
  locals row. Field layouts come from the module's own compile (the ADR 0138
  class-layout sidecar), so display types are the semantic Zia types; objects
  nest with collections in both directions.
- Debug console output.

Known debugger UX gaps:

- Boxed struct-typed fields display as typed leaves (struct payload expansion
  is a recorded follow-up); direct-IL and BASIC debug sessions have no layout
  sidecar and keep `<TypeName>` leaves for objects.
- Watch management is command-palette based; a dedicated in-panel watch toolbar
  is planned alongside the bottom-panel layout work.
- Breakpoint metadata editing exists, but the UX is still lightweight.
- Session state could still be clearer while a long graceful stop/restart is
  waiting for process exit.

### Terminal

The integrated terminal starts a platform shell in a PTY when the Terminal panel
is shown. It supports prompt output, raw typed input, line editing delegated to
the shell, resize, Stop, Restart, and workspace-root working directory selection
for new sessions.

Emulation coverage (pinned to the vim/less/htop sequence table, exercised by
`test_vg_outputpane_term.c` and `terminal_altscreen_probe.zia`):

- Cursor addressing, save/restore, line/display erase, insert/delete lines and
  characters, tab stops (HT/HTS/TBC).
- Alternate screen (47/1047/1049) with primary scrollback preserved.
- DECSTBM scroll regions with region-aware LF/IND/RI and SU/SD, so status
  rows stay pinned during full-screen redraws.
- DEC private modes 25 (cursor visibility), 2004 (bracketed paste), 1
  (application cursor keys); DSR and DA replies on the input stream.
- SGR 16/256/truecolor plus bold and reverse video.
- Clipboard chords: Cmd+V / Ctrl+Shift+V paste (bracketed when armed),
  Cmd+C / Ctrl+Shift+C copy a selection; plain Ctrl+C/Ctrl+V still reach the
  child process.

Current limitations:

- VT features outside the pinned table (e.g. underline rendering, Sixel,
  mouse reporting, OSC beyond swallowing) are not claimed.
- Terminal dimensions come from OutputPane cell metrics via `ColumnsForWidth()`
  and `RowsForHeight()`.
- Hidden panels do not auto-start shells, but already-running sessions are pumped
  into a bounded replay buffer so PTY output does not back up while another panel
  is selected.

### Source Control

The Source Control view is a Git integration, not a general SCM abstraction.
It supports:

- Detecting whether the project root is a Git repository.
- Current branch display.
- Status entries.
- Stage one file.
- Unstage one file.
- Stage all.
- Commit staged changes with a message.
- Diff selected path (unified in the panel, or side-by-side via the diff view).
- Editor gutter change bars are produced by cancellable, frame-pumped Git jobs.
  Tab switches coalesce to the newest path, secondary workspace folders use
  their owning repository, configured external diff/textconv commands are
  disabled for this passive decoration, and a five-second/4,096-marker safety
  budget prevents a slow child or pathological hunk from freezing the editor.
- Commit history: lazily paged log, per-commit file lists, and side-by-side
  parent-vs-commit diffs for any file in a commit.
- Push and pull on a PTY with live output streaming into the panel; detected
  Username/Password/passphrase/host-key prompts surface an in-app credential
  row (masked input for secrets) — no external askpass helper.
- Queued operations: actions requested while a job runs wait in a bounded,
  visible queue and run in order; Cancel clears the active job and the queue.
- Switch branch basics.

Known limits:

- Operations remain serialized by design (git mutates shared repository
  state); the queue makes waiting visible rather than adding parallelism.
- Credential prompt detection is a heuristic over PTY output and fails open:
  unrecognized prompts simply stream into the panel.
- Status parsing uses porcelain v2 and handles common spaces, renames, and basic
  unmerged conflict rows, but exotic path bytes and complex conflict recovery
  still need more coverage.
- No merge, rebase, stash, or conflict-resolution workflows.

## Data Safety

Implemented protections:

- Modified tabs get close prompts through the document close flow.
- Save All skips untitled and read-only preview buffers.
- Existing-file saves use `Zanna.Workspace.Edit.ApplyInRoot`.
- Save As uses same-directory temporary writes for new files.
- File watchers detect external changes, deletions, and missing/moved files;
  saves after a missing-file conflict require confirmation before recreating the
  original path.
- Session restore persists unsaved small text buffers as bounded base64 recovery
  data. Session/settings reads reject oversized state before parsing; writers
  cap tabs, roots, breakpoints, and aggregate embedded recovery, retain the
  active tab when truncating, and atomically replace the shared INI file.
- Continuous crash swaps snapshot modified editable buffers after a two-second
  debounce and perform large writes on a coalescing background worker. Atomic
  staged commits are cancellation-safe across save, close, reload, rename, and
  delete transitions, so an old worker cannot resurrect discarded text.
- Explorer path-only rename and drag-move operations retain the live editor
  buffer, including undo, selection, folds, and scroll. Moving an open path to
  project trash closes its documents and releases either split-pane owner before
  the surviving tab is activated.
- Build/debug preflight can save all modified files before launching.

Known data-safety gaps:

- Scene files do not have a scene-specific document model yet.
- Recovery applies only to editable text buffers; a hard crash can still lose
  edits made after the most recent two-second debounce snapshot.
- Source Control write operations depend on Git command success and basic
  stderr reporting.

## Product Polish Gaps

These gaps are current documentation, not a plan commitment:

- Extend named vector icons when new workbench actions are added.
- Move remaining prompt-style workflows into non-modal workbench overlays.
- The editor and bottom tool panels share one vertical splitter: every panel tab
  has the same height (no editor jump on switch), the boundary is drag-resizable
  with a debounced persisted height, Search temporarily reserves a useful result
  viewport in compact windows without overwriting that preference, and the bottom
  area collapses when no panel is open. Remaining: fully virtualize panel content
  beyond the bounded stable-row model.
- Add a dedicated in-panel watch toolbar and struct-payload expansion in the
  debugger.
- Add Source Control merge/rebase/conflict workflows.
- Add real scene editing before advertising scene editor functionality.
- Split oversized coordinator modules.
- Expand platform and display test coverage.

## Documentation Honesty Rules

Use these phrasing rules when updating user-facing docs:

- Say "Zia and BASIC semantic navigation" only for definition, references,
  rename, call hierarchy, workspace symbols, and signature help.
- Say "BASIC compiler-backed completion, diagnostics, hover, and symbols" when
  discussing the `Zanna.Basic.LanguageService` runtime bridge specifically.
- Say "integrated PTY terminal covering the vim/less/htop sequence table"
  instead of "full terminal emulator".
- Say "Git Source Control view" instead of "SCM platform".
- Say "scene files are recognized and open as text" instead of "scene editor".
- Say "debug adapter supports stepping, breakpoints, locals, call stack,
  evaluate, watches, watch commands, and structured expansion of collections
  and class-instance fields" while still mentioning the struct-payload leaf
  and dedicated watch panel UX as gaps.

The goal is to make the app feel more trustworthy by making the docs less
optimistic than the code.
