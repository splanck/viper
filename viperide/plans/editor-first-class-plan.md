# ViperIDE Code Editor First-Class Plan

## 1. Reset

Status: active correction plan.

Scene editor work is blocked until this plan is complete. ViperIDE must first
become a fast, credible code editor and project IDE for ordinary Viper/Zia work.
Do not start Phase 4 or Phase 5 scene UI work while editor typing latency,
IntelliSense, refactoring, project navigation, console UX, and core shell polish
remain below product quality.

This plan supersedes the old interpretation that Phases 0 through 2 were
"done" in a user-facing sense. Some infrastructure landed and should be reused,
but the product bar was wrong: green probes and backend plumbing do not make a
first-class editor.

## 2. Current State

Useful pieces already exist:

- Multi-tab document model, file tree, tab bar, breadcrumb, find bar, minimap,
  settings overlay, status bar, command registry, and structured locations.
- Safer close/save/session paths, recent-project persistence, dirty document
  tracking, and external-change warnings.
- `Viper.GUI.CodeEditor` with syntax highlighting, line numbers, undo/redo,
  cursor APIs, gutter icons, selection APIs, and fold gutter support.
- Zia language APIs for completion, hover, symbols, diagnostics, signature help,
  and `Viper.Zia.ProjectIndex` definition/reference/rename queries.
- Build/run jobs using argument vectors, `Viper.System.Process`, streaming
  stdout/stderr, cancellation, run configs, and clickable diagnostic rows.
- Breakpoint persistence and gutter painting.

Product gaps:

- Editor responsiveness is not acceptable. Several controllers copy the whole
  editor buffer or run semantic work from the frame loop.
- Completion is basic, trigger-heavy, focus-stealing, and not reliably
  project-aware or incremental.
- Signature help does not reliably appear on `(` and is not structured enough
  for parameter names, overloads, active parameter highlighting, or docs.
- The project index exists, but completion/signature/diagnostics still behave
  mostly like source-driven single-file services.
- Refactor support is too narrow. Rename exists, but broader refactor UX,
  preview, conflicts, and undo grouping are not product-grade.
- The console is still a reused output list, not a developer console surface.
- The file tree has some context-menu wiring, but it needs a complete,
  discoverable, cross-platform project-file workflow.
- The UI reads like a demo: plain layout, weak information hierarchy, minimal
  editor chrome, shallow command discoverability, and little feedback about
  background work.
- BASIC support is honest but thin: syntax/build text editing without semantic
  language services in ViperIDE.
- The debugger UI is a placeholder against a non-executing protocol and must not
  be marketed as real debugging.

## 3. Definition of Done

ViperIDE reaches first-class code editor status when a developer can use it for a
small real Viper project without switching editors for everyday code work:

- Typing, cursor movement, selection, scrolling, and multi-cursor editing remain
  responsive in large files.
- Completion appears quickly, filters as the user types, ranks useful symbols
  first, and does not steal editor focus.
- Signature help appears after `(` and `,`, shows real parameter names/types,
  and tracks the active parameter through nested calls.
- Diagnostics run after idle, never while every keystroke is still in flight,
  and do not freeze the UI.
- Go to definition, find references, rename, and common refactors work through
  structured project state and offer preview/cancel paths.
- The file tree supports right-click project workflows expected in an IDE.
- Console, Problems, Search, Outline, and Refactor views are real tool surfaces,
  not ad hoc listbox dumps.
- Settings are easy to find, clearly laid out, persistent, and visually aligned
  with the rest of the IDE.
- The UI feels like an integrated work tool: denser, calmer, more legible, and
  less like a sample app.

## 4. Non-Negotiable Rules

- No scene editor work until this plan's release gate passes.
- No per-frame full-buffer text copies for routine editor state.
- No synchronous semantic work in the hot typing path.
- No feature may be marked complete without a manual UX checklist and at least
  one focused regression where practical.
- No overclaiming. Placeholder debugger, partial BASIC services, incomplete
  refactors, and deferred console features must be documented plainly.
- Runtime/API additions must include graphics and non-graphics build coverage
  where applicable, runtime binding updates, stubs, and completeness checks.

## 5. Workstreams

### E0 - Editor Performance Recovery

Goal: make the editor fast before adding more visible features.

Required changes:

- Add a `CodeEditor` document revision/version counter exposed to Zia.
- Expose cheap editor state APIs for:
  - current revision
  - changed revision since last poll
  - current line text
  - line count
  - cursor line/column
  - selected text/ranges
- Replace routine `engine.GetText()` polling in diagnostics, symbols, hover,
  signature help, completion, index sync, and status updates with revision-based
  invalidation.
- Cache active-document text in the document model only when a save, semantic
  job, refactor, search, or tab switch actually needs it.
- Move project-index dirty-buffer sync from "every frame" to:
  - active document revision change
  - save
  - tab switch
  - before semantic project query
  - explicit project refresh
- Add a single editor-work scheduler that can debounce, cancel, and coalesce:
  diagnostics, document symbols, completion refresh, hover, index update, and
  project search.
- Disable expensive live work while the user is continuously typing; run it
  after an idle threshold.

Acceptance:

- Typing in a large file does not call full-buffer `GetText()` every frame.
- Holding a key down does not trigger repeated semantic checks.
- Project index sync updates only changed open buffers.
- Scrolling does not run semantic language services.
- Editor typing remains visibly responsive with diagnostics, minimap, outline,
  and build output panes visible.

Tests:

- Runtime regression for editor revision increments on insert/delete/paste and
  remains unchanged on cursor-only movement.
- ViperIDE probe that simulates typing and asserts diagnostics/index update
  counters stay bounded.
- Large-file smoke with at least 5k and 20k-line buffers.

Current implementation status:

- `Viper.GUI.CodeEditor.Revision` is implemented and exposed through
  `runtime.def` with graphics and non-graphics runtime coverage.
- `EditorEngine.GetRevision()` gives IDE controllers a cheap content-change
  probe without reading the full buffer.
- Live diagnostics, signature help, and the symbol outline are now
  revision-gated. They read full source text only when a debounced/stale query
  is actually needed or when the user explicitly triggers a language action.
- Project-index sync is no longer a per-frame full open-buffer push. It now
  syncs the active Zia buffer after idle or immediately before semantic
  navigation/refactor actions.
- Added `zia_viperide_editor_hot_path` to verify revision behavior for set
  text, insert, undo, redo, cursor-only movement, idle index sync, and a
  20k-line cursor-movement smoke.
- Still open: a shared editor-work scheduler, changed open-buffer sync beyond
  the active editor, and manual latency profiling on real projects.

Manual checklist:

- Open a large `.zia` file, type continuously for 30 seconds, scroll, select,
  undo/redo, and verify no visible stalls.
- Repeat with diagnostics and symbol outline enabled.

### E1 - Completion and IntelliSense

Goal: completion should feel like a real editor feature, not a manual popup.

Required changes:

- Keep focus in the code editor while completion is open.
- Trigger completion on:
  - `.` immediately
  - identifier characters after a short debounce
  - Ctrl/Cmd+Space explicitly
  - `new`, type annotations, imports/binds, and member chains where applicable
- Filter and re-rank existing candidates as the prefix changes without
  reparsing when possible.
- Re-query semantically after debounce or when context changes meaningfully.
- Rank locals, parameters, fields, and receiver members above globals, runtime
  classes, keywords, and snippets.
- Include imported/project symbols in completion, not just current-file globals.
- Make completion item records structured before they reach the UI:
  label, insert text, kind, detail, documentation, source, replacement range,
  commit characters, and snippet flag.
- Add replacement-range handling so accepting a completion replaces only the
  typed prefix, not a guessed word under the cursor.
- Add optional function-call insertion:
  - inserting `foo` should not always add parentheses
  - accepting a callable via an explicit commit character may insert `foo()`
  - snippets should place the cursor inside expected locations
- Add graceful fallback results while semantic analysis is warming up.

Acceptance:

- `object.` shows member completions without freezing.
- Typing more characters while the popup is open narrows results and keeps focus
  in the editor.
- Completion works after imports/binds and across project files.
- Accepting a completion produces correct text and undo behavior.
- Completion does not appear in comments/strings unless explicitly requested.

Tests:

- Completion trigger tests for dot, identifier prefix, `new`, type annotation,
  and explicit shortcut.
- Project completion test with two files and unsaved dirty import content.
- Replacement-range tests for prefix, member access, and mid-token invocation.
- UI probe for keeping editor focus while popup is visible.

Manual checklist:

- Open a real ViperIDE source file and complete locals, methods, runtime classes,
  imports, and snippets.
- Type through the popup, accept with Tab/Enter, dismiss with Escape, undo the
  accepted completion, and verify focus never leaves the editor.

Current implementation status:

- Completion popup now keeps focus in `CodeEditor`; main input routing handles
  Tab/Enter/Escape/Up/Down while typed characters continue into the editor.
- Dot triggers query immediately; identifier characters schedule a debounced
  query; cached results filter in-place as the prefix changes.
- Completion rows keep separate label/display/insert text data so the UI does
  not parse decorated display strings on accept.
- Added `zia_viperide_intellisense` for focus-safe popup filtering, explicit
  trigger behavior, and signature active-parameter regression coverage.
- Still open: structured completion records with docs/source/replacement ranges,
  callable commit behavior, snippet cursor placement, and true project-symbol
  completion beyond what the current Zia completion API exposes.

### E2 - Signature Help and Hover

Goal: signature help must be useful at call sites and resilient to incomplete
code.

Required changes:

- Replace plain-text signature help with a structured signature API:
  function/method name, parameter names, parameter types, return type, active
  parameter, overload index/count, documentation, and source.
- Parser/sema recovery must handle incomplete calls such as:
  - `foo(`
  - `foo(a,`
  - `obj.method(`
  - nested calls
- Trigger signature help on `(`, `,`, explicit shortcut, and cursor movement
  within an active call.
- Highlight or otherwise visually distinguish the active parameter.
- Support overload navigation once overload data is available.
- Hover should use the same structured symbol data where possible and avoid
  whole-buffer work until hover dwell time is reached.

Acceptance:

- Typing `(` after a known function shows signature help immediately.
- Commas update the active parameter.
- Nested calls track the innermost call.
- Hover does not execute semantic work until dwell delay has elapsed.

Tests:

- Structured signature tests for normal functions, methods, runtime APIs, and
  overloaded functions.
- Incomplete-call parser recovery tests.
- Active-parameter tests for nested parentheses, brackets, and braces.

Manual checklist:

- Type calls in real source and verify signatures appear and update without
  editor lag.

### E3 - Diagnostics, Problems, and Code Actions

Goal: diagnostics should help without hurting responsiveness.

Required changes:

- Run live diagnostics only after editor idle and revision stability.
- Track diagnostic jobs with generation ids so stale results cannot overwrite
  newer editor state.
- Populate a real Problems panel with columns or structured rows:
  severity, file, line, code, message, source.
- Keep inline highlights and minimap markers in sync with diagnostic generation.
- Add basic code actions where safe:
  - quick fix placeholder state
  - organize imports/binds if supported
  - create missing import/bind only when engine can propose it safely
  - suppress or ignore actions only after language support exists
- Add a "Run Check Now" command that bypasses idle debounce intentionally.

Acceptance:

- Diagnostics never visibly pause typing.
- Problems panel rows remain clickable through `LocationStore`.
- Stale diagnostics from old revisions are dropped.
- Closing/switching documents clears or scopes diagnostics correctly.

Tests:

- Idle debounce and generation invalidation tests.
- Problems panel location tests with spaces, colons, and Windows-style paths.
- Manual code action tests start as disabled/unavailable until real actions land.

Current implementation status:

- Live diagnostics are revision/path-gated and debounced; stale revisions do not
  keep rechecking every frame.
- Diagnostics populate structured `LocationStore` ids for click navigation and
  mirror into minimap markers and editor highlights.
- Added `Run Check Now` (`Ctrl+Alt+C`) to intentionally bypass the idle debounce.
- Still open: dedicated Problems surface with columns, diagnostic generation ids
  for async jobs, and real code actions.

### E4 - Project Navigation and Refactoring

Goal: project intelligence should cover everyday refactor/navigation workflows.

Required changes:

- Keep Go to Definition, Find References, and Rename on structured project index
  APIs.
- Add a dedicated References/Results panel with preview text and grouping by
  file.
- Add refactor command framework:
  - command availability by language service capability
  - preview edits
  - apply/cancel
  - conflict display
  - undo grouping
- Harden Rename:
  - preview before apply
  - reject keyword names and invalid identifiers
  - reject collisions with clear messages
  - reject overlapping edits
  - abort on dirty closed-file or external timestamp conflict
  - apply all or nothing across open and closed files
- Add safe initial refactors only when engine support exists:
  - rename symbol
  - rename file/folder with import/bind update preview
  - organize imports/binds
  - extract local variable
  - extract function for selected statements
  - inline local variable
- Keep unimplemented refactors visible only as disabled commands or omit them
  entirely until support exists.

Acceptance:

- Rename preview shows all affected files before writing.
- Canceling preview applies nothing.
- Applying rename creates one undo step per affected open document.
- File rename can update project binds/imports when language support is present,
  or clearly state that references were not rewritten.

Tests:

- Rename all-or-nothing tests across open, closed, dirty, and externally changed
  files.
- Refactor preview data model tests.
- References panel grouping and navigation tests.
- File rename import/bind update tests once implemented.

Manual checklist:

- Refactor a small multi-file project, inspect preview, cancel, retry, apply, and
  undo.

Current implementation status:

- Definition, references, and rename use structured `ProjectIndexer` queries and
  force an active-buffer sync before querying.
- Rename validates identifiers/keywords, rejects overlapping edits, validates
  disk edits before mutating open buffers, and applies open-buffer changes only
  after disk edits succeed.
- Rename now shows a preview/cancel dialog listing affected file/line/column
  edits before applying.
- Still open: grouped References panel, undo grouping per affected document,
  file/folder rename import/bind rewrites, and extract/inline refactors.

### E5 - File Tree and Workspace UX

Goal: the project tree should behave like an IDE project explorer.

Required changes:

- Complete right-click file/folder context menu:
  - Open
  - Open With Text Editor
  - New File
  - New Folder
  - Rename
  - Delete
  - Duplicate
  - Copy Path
  - Copy Relative Path
  - Reveal in Finder/Explorer/File Manager
  - Refresh
  - Search in Folder
  - Run This File where supported
  - Set as Project Entry where supported by manifest
- Context menu must target the node under the mouse, not only the selected node.
- New file/folder uses the clicked folder or parent folder of the clicked file.
- Rename/delete/update open documents, tabs, diagnostics, locations, project
  index entries, and recent files.
- Add keyboard commands for tree rename, delete, refresh, and new file/folder.
- Add project Quick Open by filename/path.
- Add recent files and recently closed tabs.
- Add ignored/excluded file handling backed by a documented matcher.

Acceptance:

- Right-clicking a file without selecting it first operates on that file.
- Creating a file in a subfolder places it in that subfolder.
- Renaming/deleting an open file updates or closes the document safely.
- Copy path and reveal commands are cross-platform.

Tests:

- Context target tests for selected vs hovered/right-clicked node.
- Open-document path update tests for file and folder rename.
- Tree operation tests for paths with spaces and punctuation.

Manual checklist:

- Build a project tree entirely from the IDE: create folders, create files,
  rename, duplicate, delete, search, copy paths, and reveal files.

Current implementation status:

- Added `Viper.GUI.TreeView.GetNodeAt(x, y)` with graphics and non-graphics
  runtime coverage so right-click actions target the node under the mouse.
- File-tree context menu now includes Open, Open With Text Editor, Reveal,
  Copy Path, Copy Relative Path, Duplicate File, Rename, Delete, New File,
  New Folder, Search in Folder, Run This File, and Refresh.
- New file/folder actions use the clicked directory or the parent directory of
  the clicked file. Right-click selection is consumed so it does not also open
  the file as a left-click selection.
- Reveal is OS-aware: Finder on macOS, Explorer on Windows, `xdg-open` on Linux.
- Added `zia_viperide_file_tree` for the GUI hit-test binding and selection
  data path.
- Still open: Set as Project Entry, keyboard tree commands, recently closed
  tabs/recent files, richer ignored-file configuration, and import/bind rewrites
  for file/folder rename.

### E6 - Console, Search, and Tool Panels

Goal: replace demo-like listbox output with real IDE work surfaces.

Required changes:

- Replace or supplement `outputListBox` with a real console/output pane:
  - append streaming lines without clearing the whole widget every update
  - preserve ANSI/color segments where practical
  - severity markers
  - copy selection
  - clear
  - search/filter
  - word wrap toggle
  - auto-scroll lock
  - clickable diagnostics through structured locations
- Add tabbed bottom panel: Problems, Output, Search, Debug Console.
- Make project search asynchronous/cancellable for large workspaces.
- Search panel supports case-sensitive, whole-word, regex/literal, include/exclude
  globs, and folder scoping.
- Search results group by file with preview snippets.
- Add command palette and Quick Open polish:
  fuzzy ranking, keyboard-only navigation, recent commands/files, and disabled
  command explanation.

Acceptance:

- Long-running output streams without freezing or losing scroll position.
- User can copy a range from console output.
- Project search can be canceled and does not block typing.
- Search result clicks remain path-safe.

Tests:

- Streaming console append and auto-scroll lock tests.
- Search options tests and cancellation tests.
- Output diagnostic click tests with Windows and punctuation-heavy paths.

Current implementation status:

- Output updates now preserve appended complete lines instead of clearing and
  rebuilding the list on every frame. Partial-line output falls back to a safe
  rebuild to avoid corrupt rows.
- Existing Problems and Output list surfaces now sit behind a lightweight bottom
  tool tab strip instead of appearing as unrelated raw panels.
- Folder-scoped search is available from the file-tree context menu and reuses
  structured search result locations.
- Added Quick Open (`Ctrl+P`) for project file name/path fragments.
- Added `zia_viperide_console_search` for output append behavior, partial-line
  rebuild behavior, whole-word search, and Quick Open ranking.
- Still open: full bottom-panel implementation for Search and Debug Console,
  copyable console selection, search/filter inside output, word wrap, severity
  styling, async/cancellable project search, and grouped search results.

### E7 - Settings and Preferences

Goal: settings should be discoverable, readable, and reliable.

Required changes:

- Replace the current floating settings overlay with a polished preferences
  dialog or side panel using grouped sections:
  Editor, Appearance, Files, IntelliSense, Run/Debug, Keybindings.
- Fix editor defaults and migration:
  - default editor font size must be comfortable
  - existing tiny persisted sizes should be detected and offered a reset
  - restore defaults works per section
- Add settings for:
  - font family/path
  - font size
  - tab width
  - insert spaces
  - word wrap
  - minimap
  - line numbers
  - code folding
  - auto-save
  - diagnostics delay
  - completion delay
  - theme
  - session restore
  - confirm close
- Add keybindings view with current shortcuts and conflict detection.
- Validate settings before writing and preserve unknown INI sections.

Acceptance:

- Settings fit at common laptop resolutions.
- Applying settings updates the live editor predictably.
- Cancel applies nothing.
- Corrupt settings do not crash launch.

Tests:

- Settings load/save/migration tests.
- Keybinding conflict detection tests.
- Dialog layout smoke at small and large window sizes where automation allows.

Current implementation status:

- Default editor font size is 20, and persisted legacy sizes below 16 migrate to
  the readable default on load.
- Preferences font-size spinner now avoids the old tiny range.
- Existing settings overlay exposes font path, font size, tab width, theme,
  minimap, diagnostics, folding, session restore, project restore, and close
  confirmation.
- Added settings migration coverage to `zia_viperide_phase0_phase1`.
- Still open: full preferences dialog sections, keybindings view/conflict
  detection, per-section restore defaults, auto-save/word-wrap/line-number
  settings, and automated layout smoke.

### E8 - Visual Design and Interaction Polish

Goal: ViperIDE should feel like a deliberate IDE, not a widget demo.

Required changes:

- Establish a restrained IDE visual system:
  - clear activity/sidebar/bottom-panel/editor hierarchy
  - consistent spacing
  - legible editor typography
  - distinct active/inactive states
  - icons for common commands
  - calmer status and toast behavior
  - consistent hover/focus/selection colors
- Improve first launch:
  - recent/open project action
  - create/open file action
  - no oversized marketing page
  - show useful empty states for Explorer, Problems, Output, and Search
- Add a denser toolbar/status bar with build/run state, current branch/project
  name if available, active language service, diagnostics summary, and job state.
- Add editor title/chrome improvements:
  unsaved indicator, readonly indicator, external-change warning, breadcrumbs,
  symbol path where available.
- Add command discoverability:
  shortcut labels in menus
  command palette categories
  disabled command reasons
  tooltips on icon buttons
- Improve theme contrast and keyboard focus visibility.

Acceptance:

- The app communicates project, file, diagnostics, and job state at a glance.
- Common actions are discoverable from menus, toolbar, context menus, and command
  palette.
- No text overlaps, clipped labels, or tiny editor defaults at supported window
  sizes.

Manual checklist:

- Fresh launch, open project, edit, search, build/run, inspect diagnostics,
  change settings, and close/reopen without needing external explanation.

### E9 - BASIC and Multi-Language Honesty

Goal: support BASIC honestly without pretending Zia services work there.

Required changes:

- Keep BASIC syntax/build support enabled.
- Add an explicit BASIC language-service adapter only when ViperIDE can call a
  real backend.
- Until then, disable semantic commands and show a concise unsupported message.
- Consider integrating `vbasic-server` only after the code editor scheduler can
  support async language backends without blocking.

Acceptance:

- Opening BASIC never invokes Zia semantic APIs.
- Command palette clearly reflects supported/unsupported BASIC commands.

## 6. Release Milestones

### Milestone A - Responsiveness Hotfix

Ship first. No UX expansion until this is green.

- E0 revision tracking and removal of per-frame full-buffer copies.
- Debounced diagnostics and stale-generation drop.
- Dirty project-index sync only on revisions/events.
- Large-file typing smoke.

Gate:

- `zia_viperide_editor_hot_path`
- `zia_smoke_viperide_project_compile`
- manual large-file typing checklist

Current status: partially implemented. Automated hot-path coverage is green;
manual latency profiling is still required before calling the milestone done.

### Milestone B - IntelliSense Recovery

- E1 completion popup/focus/filter/replacement-range fixes.
- E2 structured signature help and incomplete-call recovery.
- Hover delayed until dwell.

Gate:

- `zia_viperide_intellisense`
- native completion/signature runtime tests
- manual IntelliSense checklist on real ViperIDE source

Current status: completion focus/filter/trigger recovery is implemented.
Structured signature records, replacement ranges, callable snippets, and manual
source-file dogfood are still required.

### Milestone C - Refactor and Project Explorer

- E4 rename preview/all-or-nothing hardening.
- E5 complete file-tree context menu and project operations.
- References panel grouped by file.

Gate:

- `zia_viperide_phase0_phase1`
- `zia_viperide_file_tree`
- manual refactor/project-tree checklist

Current status: rename preview/all-or-nothing and context-menu project explorer
work are implemented. Grouped references and broader refactors are still open.

### Milestone D - Console and Work Surfaces

- E6 real console/output pane.
- Problems/Search/Output bottom panel.
- Async/cancellable project search.

Gate:

- `zia_viperide_console_search`
- process streaming/cancel tests
- manual build/run/search checklist

Current status: append-preserving output updates, folder search, Quick Open, and
a lightweight Problems/Output tab strip are implemented. The full
console/search/debug-console surface is still open.

### Milestone E - Preferences and Visual Polish

- E7 preferences dialog/keybindings/settings migration.
- E8 visual polish pass.
- Accessibility, keyboard navigation, and contrast audit.

Gate:

- `zia_viperide_phase0_phase1`
- `zia_viperide_ux_smoke`
- screenshot/manual checklist across compact and wide window sizes

Current status: font defaults/migration and the existing settings overlay are
improved. Keybindings, visual polish, and full UX smoke coverage remain open.

### Milestone F - Dogfood

- Use ViperIDE for a small real Viper project without another editor for normal
  code editing, navigation, refactoring, build/run, search, settings, and file
  operations.
- Fix all P0/P1 dogfood issues before scene editor work resumes.

Gate:

- full relevant CTest subset plus full `ctest --test-dir build --output-on-failure`
- `./scripts/build_ide.sh`
- manual dogfood report

## 7. Documentation Updates Required Per Milestone

- Update `viperide/README.md` feature list only for features that are genuinely
  usable in the app.
- Update this plan with completed milestone notes and known deferrals.
- Update old phase files only to point at this correction plan where they would
  otherwise overclaim.
- Add short manual checklists beside each CTest gate.

## 8. Scene Editor Resume Criteria

Scene editor work may resume only when:

- Milestones A through F are complete.
- The editor no longer has known typing-latency regressions.
- Completion/signature help are reliable enough for daily coding.
- Project explorer/refactor workflows are safe.
- Console/search/problems surfaces are real app features.
- Documentation no longer claims placeholder or partial features as complete.

After that, start a new scene-editor plan from the corrected baseline. Do not
reuse the old Phase 4/5 plan without re-review.
