# ViperIDE Code Editor First-Class Plan

## 1. Reset

Status: active rebaseline. Performance remains the P0 blocker.

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
- The project index exists and open-project indexing is now cooperative, but
  completion/signature/diagnostics still need broader project-symbol awareness
  and a shared scheduler before they can be considered first-class.
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

## 2.1 Current Deep-Dive Findings

This section reflects the current codebase state after the first editor-first
correction pass. The earlier plan correctly identified the right product areas,
but it still underweighted native editor performance. The user-visible result is
clear: ViperIDE still feels slow while editing, so the next work must be
measurement and hot-path repair, not more features.

Implemented and useful:

- `CodeEditor.Revision` exists and lets IDE controllers avoid some routine
  full-buffer polling.
- Diagnostics, symbols, signature help, completion, and project-index sync are
  partly revision/idle gated.
- Completion no longer steals editor focus and filters cached results while the
  user types.
- Rename preview, safer workspace edits, right-click file-tree targeting,
  folder search, Quick Open, and output append preservation are implemented.
- CTests cover the current regression probes: editor hot path, IntelliSense,
  file tree, console/search, and the older phase probes.

Performance risks still present:

- Native `CodeEditor` paint/layout still has O(total-lines) work in important
  paths. `codeeditor_total_content_height_for_width`, scroll clamping,
  scrollbar metrics, visual-row lookup, and cursor visual-row calculation scan
  line ranges; word-wrap makes this worse. This can happen while painting or
  interacting, independent of Zia semantic services.
- Syntax highlighting is called from the paint path for visible lines. It caches
  buffers but does not have a clear dirty-line/version model, so paint is doing
  tokenization work that should be precomputed or invalidated precisely.
- Highlight rendering scans every highlight span for every visible line. This is
  fine for a few diagnostics but not for many diagnostics/find results.
- Several UI controllers still perform synchronous full-buffer reads and
  semantic queries on the frame loop: completion, signature help, hover,
  symbols, diagnostics, and project-index active sync.
- Project opening now indexes `.zia` files cooperatively in frame-sized slices,
  and Quick Open uses the maintained project-tree file cache. Project search
  still enumerates paths synchronously at start, but content scanning is now
  frame-sliced and cancelable.
- Build/debug output still stores a whole accumulated output string for final
  diagnostics parsing, but active build/run streaming now exposes deltas to the
  UI instead of repainting from the full string every frame.
- There is no profiler, no frame-time overlay/log, no per-controller timing, no
  counter for full-buffer text copies, and no benchmark gate that fails when the
  editor regresses.

Code evidence from this audit:

- `src/lib/gui/src/widgets/vg_codeeditor.c` owns the biggest suspected native
  hot paths: total-height calculation, visual-row lookup, scroll metrics,
  cursor-row mapping, visible-line highlighting, and highlight-span painting.
- `viperide/src/completion.zia`, `signature.zia`, `diagnostics.zia`,
  `hover.zia`, and `symbols.zia` still create full-buffer snapshots before
  calling language APIs when their debounced work fires.
- `viperide/src/project_index.zia` now has cooperative workspace indexing, but
  semantic queries still force a full pending-index drain for correctness.
- `viperide/src/search_commands.zia` still enumerates file paths synchronously at
  search start, but it now scans file contents cooperatively; Quick Open uses
  the cached project file list when the project tree is populated.
- `viperide/src/build_system.zia` and `build_commands.zia` now stream active
  output as deltas, but final diagnostics still parse an accumulated output
  string instead of a bounded stream of records.

Immediate conclusion:

- The previous responsiveness milestone is not complete. The current automated
  hot-path probe proves only that a few revision counters and idle gates behave;
  it does not prove typing latency, paint cost, scroll cost, minimap cost, or
  native editor input cost.
- All non-performance feature expansion is paused until P0 performance
  instrumentation identifies the top offenders and the worst editor hot paths
  are fixed.

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

- Add performance instrumentation before further optimization:
  - per-frame total time
  - `CodeEditor` paint/layout/input time
  - semantic-controller time by subsystem
  - full-buffer `Text`/`GetText()` copy count and byte count
  - project-index update count and byte count
  - minimap render/update time
  - visible line count and document line count
  - optional status/debug overlay and log output
- Add repeatable editor performance harnesses:
  - 1k, 5k, 20k, and 50k-line files
  - typing burst
  - key repeat
  - cursor movement
  - selection drag
  - scroll wheel / trackpad scroll
  - diagnostics/minimap/output/outline on and off
- Set explicit performance budgets before claiming success:
  - no individual frame above 16 ms during simple typing in 5k-line files
  - no individual frame above 33 ms during simple typing in 20k-line files
  - no synchronous semantic query in the keypress-to-paint path
  - no full-buffer text copy on cursor-only movement or scroll
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
- Fix native `CodeEditor` hot paths:
  - cache total visual row count and total content height until text/wrap/fold
    state changes
  - avoid O(line_count) scan in paint, scroll clamp, scrollbar metrics, and
    cursor visual-row calculations for the no-wrap common case
  - add line/wrap prefix-sum or Fenwick-style indexing if word-wrap remains
    supported for large files
  - move syntax highlighting out of paint into dirty-line/versioned caches
  - index highlight spans by line range instead of scanning every span for every
    visible line
  - make minimap rendering revision-based and incremental, or disable it by
    default until it is cheap
- Fix UI/data-model hot paths:
  - replace whole-output-string polling with append-only output events
  - make project open/indexing frame-budgeted or background-cooperative
  - make project search cancellable/cooperative
  - keep Quick Open backed by a maintained file cache, not a fresh full
    enumeration on every command

Acceptance:

- Typing in a large file does not call full-buffer `GetText()` every frame.
- Holding a key down does not trigger repeated semantic checks.
- Project index sync updates only changed open buffers.
- Scrolling does not run semantic language services.
- Paint and scroll do not scan the entire document in the no-wrap common case.
- Syntax highlighting does not tokenize visible lines in paint unless a dirty
  line genuinely needs highlighting.
- Minimap disabled/enabled state has measured impact and cannot silently dominate
  typing latency.
- Editor typing remains visibly responsive with diagnostics, minimap, outline,
  and build output panes visible.

Tests:

- Native microbenchmarks for paint/layout/scroll/input on 1k, 5k, 20k, and
  50k-line buffers.
- Runtime regression for editor revision increments on insert/delete/paste and
  remains unchanged on cursor-only movement.
- ViperIDE probe that simulates typing and asserts diagnostics/index update
  counters stay bounded.
- Large-file smoke with at least 5k, 20k, and 50k-line buffers.
- Performance gate that records frame timings and fails on budget regressions.

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
- Native `CodeEditor` performance counters now track layout scans,
  syntax-highlight calls, highlight-span checks, full-buffer copies, and copied
  bytes; `Viper.GUI.CodeEditor` exposes reset/read methods for the high-value
  counters used by probes.
- The no-wrap/no-fold editor path avoids whole-document layout scans for content
  height, visual-row lookup, scroll-top lookup, cursor movement, and scroll-to
  operations.
- `EditorEngine.GetTextSnapshot()` is revision-keyed, so repeated semantic
  controllers share one full-buffer materialization per content revision.
- Project indexing opens cooperatively in frame-sized slices and Quick Open uses
  the maintained project-tree file cache instead of fresh enumeration.
- Project/folder search now scans file contents cooperatively over multiple
  frames and can be canceled, instead of reading every file in one command
  handler.
- Active build/run output now consumes incremental output deltas, so the UI no
  longer receives the full accumulated output string every frame while a job is
  running.
- Syntax highlighting no longer bumps the global syntax generation for ordinary
  single-line edits; edited lines are invalidated directly, Zia block-comment
  state is cached per line, and highlight spans are sorted so paint can stop
  scanning once spans pass the visible line.
- The minimap now defaults off for new settings and samples large files when
  visible instead of drawing one bar for every line.
- Added `zia_viperide_editor_hot_path` to verify revision behavior for set
  text, insert, undo, redo, cursor-only movement, idle index sync, and a
  20k-line cursor-movement smoke.
- Added native `test_vg_codeeditor_perf` for 50k-line no-wrap cursor/scroll
  layout-scan regressions and full-text copy counter behavior.
- This is not complete. Current coverage proves several hot-path invariants, not
  full user-perceived responsiveness.
- Still open: frame timing overlay/logging, editor paint/input timing,
  selection-drag and scroll benchmarks, a shared editor-work scheduler, changed
  open-buffer sync beyond the active editor, async/cached search enumeration,
  dedicated minimap timing, and manual latency profiling on real projects.

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
- Performance caveat: `TriggerCompletion()` still copies the full editor buffer
  and calls `CompleteForFile` synchronously. Completion must move behind the
  scheduler or use incremental snapshots before it can be called first-class.
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

Current implementation status:

- Signature help triggers on `(` and `,`, tracks nested active parameters, and is
  covered by the IntelliSense probe.
- Signature help has a current-file source fallback for incomplete calls such as
  `foo(`, so locally declared functions can show real parameter names before
  semantic analysis succeeds.
- The popup is taller/wider so two-line signatures are not immediately clipped.
- The returned UI is still plain text plus a numeric active-parameter marker.
- Performance caveat: trigger/update still call `SignatureHelpForFile`
  synchronously from the frame loop while visible, although unchanged revisions
  now reuse the cached editor snapshot.
- Still open: structured signature result API, active parameter highlighting,
  overload navigation, docs/source display, imported/project declaration
  parameter-name fallback, and scheduler-backed execution.

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
- Performance caveat: diagnostics still copy the full buffer and call
  `CheckForFile` synchronously when the debounce expires. They are less frequent,
  but still run on the UI frame.
- Still open: scheduler/background execution, dedicated Problems surface with
  columns, diagnostic generation ids for async jobs, and real code actions.

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
- Performance caveat: project search still enumerates file paths synchronously
  at search start, and Quick Open falls back to synchronous enumeration when no
  project-tree cache is available. Build output still keeps a whole accumulated
  output string for final parsing.
- Still open: full bottom-panel implementation for Search and Debug Console,
  copyable console selection, search/filter inside output, word wrap, severity
  styling, async/cached search enumeration, and grouped search results.

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

## 6. Revised Recovery Milestones

### Milestone P0A - Measure the Editor

Goal: stop guessing. Before any more product feature work, add enough
instrumentation to identify where editor latency is actually spent.

Work:

- Add frame timing, editor paint/layout/input timing, semantic-controller
  timing, minimap timing, output-panel timing, full-buffer copy counters, and
  project-index update counters.
- Add a visible debug overlay or developer log that can be enabled without
  recompiling.
- Add repeatable large-file and project-size fixtures.
- Add an automated performance probe that runs typing, cursor movement,
  selection, and scroll loops on large documents.

Gate:

- `zia_viperide_editor_hot_path`
- new editor performance probe with recorded budgets
- native `CodeEditor` microbenchmarks
- manual latency report listing the top five measured offenders

Current status: partially implemented. `CodeEditor` now exposes native/runtime
hot-path counters, the editor hot-path probe asserts no full-buffer copies for
cursor-only movement/snapshot reuse, and `test_vg_codeeditor_perf` covers a
50k-line no-wrap cursor/scroll path. Still missing: real frame-time logging,
paint/input/minimap timing, selection/typing burst benchmarks, explicit budget
recording, and a manual top-five latency report.

### Milestone P0B - Fix Native CodeEditor Hot Paths

Goal: make raw text editing fast with semantic services disabled.

Work:

- Remove O(total-lines) work from no-wrap paint, scroll clamp, scrollbar
  metrics, and cursor visual-row paths.
- Cache visual row counts and content height by revision/wrap/fold state.
- Move syntax highlighting out of routine paint and into dirty-line caches.
- Index diagnostics/find/highlight spans by line range.
- Make minimap incremental and revision-based, or keep it disabled by default
  until measured cheap.
- Add native tests that compare large-file edit/scroll timings before and after
  the cache changes.

Gate:

- no-wrap paint/scroll native microbenchmarks pass on 5k, 20k, and 50k-line
  buffers
- cursor movement and selection do not scan the whole document
- syntax highlighting is not routinely tokenized in paint
- manual large-file editing feels responsive with all language services off

Current status: partially implemented. The no-wrap/no-fold path now avoids
whole-document scans for common layout/scroll/cursor operations, ordinary
single-line edits invalidate only the edited line's syntax cache, Zia
block-comment state is cached per line, highlight spans are sorted before paint,
and the minimap is off by default plus sampled when visible. Still missing:
word-wrap prefix indexing, full paint/input benchmarks, line-range indexed
highlight spans, and measured minimap budgets.

### Milestone P0C - Semantic Work Scheduler

Goal: make language services cooperate with editing instead of competing with
it.

Work:

- Add one scheduler for diagnostics, completion refresh, signature help, hover,
  symbols, project-index updates, search, and Quick Open refreshes.
- Coalesce work by document revision and cancel stale jobs.
- Move completion, signature help, hover, diagnostics, symbols, and active-index
  sync away from synchronous frame-loop execution.
- Replace routine whole-buffer reads with cached snapshots created only for jobs
  that need them.
- Make project open/index/search cooperative and frame-budgeted.

Gate:

- no synchronous semantic query in the keypress-to-paint path
- no full-buffer copy on cursor-only movement, scroll, or popup navigation
- diagnostics and symbols cannot overwrite newer revisions
- project search and indexing can be canceled or paused without blocking typing

Current status: partially implemented. Diagnostics, symbols, completion,
signature help, hover, active-buffer index sync, project indexing, and Quick
Open now avoid routine full-buffer copies or full project enumeration in the
common frame loop. Project indexing is cooperative. Still missing: one shared
scheduler abstraction, async/cached search-path enumeration, async/background
semantic queries, cancellable/pausable long index drains, and generation-based
stale-result rejection for future async jobs.

### Milestone P1 - IntelliSense Reliability

Goal: make completion, signature help, and hover useful enough for daily coding
after the editor is fast.

Work:

- Structured completion records, replacement ranges, commit characters, snippets,
  ranking, docs/details, and project-symbol inclusion.
- Structured signature help with parameter names/types, active parameter display,
  overload metadata, docs, and incomplete-call recovery.
- Hover backed by structured symbol data and dwell scheduling.

Gate:

- `zia_viperide_intellisense`
- project completion and signature tests using unsaved cross-file content
- manual dogfood on ViperIDE source for locals, members, imports, runtime APIs,
  signatures, hover, accept, dismiss, and undo

Current status: partial. Focus-safe completion filtering, cached snapshot use,
basic signature triggering, nested active-parameter tracking, and current-file
incomplete-call signature fallback with real parameter names exist. The data
model and semantic quality are still not first-class: completion records lack
replacement ranges/docs/source/commit characters, signature help is still
plain text, and project/imported parameter-name fallback remains open.

### Milestone P2 - Refactor and Project Explorer

Goal: make project navigation and file operations safe and discoverable.

Work:

- Grouped References panel with preview snippets.
- Rename preview polish, undo grouping, and broader conflict diagnostics.
- File/folder rename with import/bind rewrite only when language support can do
  it safely.
- Keyboard commands and full context-menu workflows for the file tree.
- Recent files, recently closed tabs, and ignored-file configuration.

Gate:

- `zia_viperide_phase0_phase1`
- `zia_viperide_file_tree`
- rename/refactor all-or-nothing tests
- manual refactor and project-tree checklist

Current status: partial. Rename hardening and many context-menu actions exist;
grouped references, import/bind rewrites, recent-file workflows, and keyboard
tree polish remain open.

### Milestone P3 - Console, Problems, Search, and Quick Open

Goal: replace listbox-style output with real work surfaces.

Work:

- Bottom panel with Problems, Output, Search, and Debug Console tabs.
- Append-only output records with copy, clear, search/filter, wrap toggle,
  severity styling, auto-scroll lock, and clickable diagnostics.
- Async/cancellable project search with grouped results and include/exclude
  filters.
- Quick Open backed by maintained project file state, fuzzy ranking, and recents.

Gate:

- `zia_viperide_console_search`
- process streaming and cancellation tests
- search cancellation and option tests
- manual build/run/search/output checklist

Current status: partial. Output append preservation, folder search, Quick Open,
cooperative file-content search, delta-based active job output, and a
lightweight Problems/Output tab strip exist; the console/search surfaces are not
first-class.

### Milestone P4 - Preferences, Visual System, and Shell UX

Goal: make the IDE feel intentional rather than like a demo.

Work:

- Replace the settings overlay with a real preferences surface.
- Add keybindings view, conflict detection, restore defaults, word-wrap,
  line-number, auto-save, diagnostics delay, completion delay, and session
  settings.
- Establish a consistent IDE visual hierarchy, toolbar/status information,
  command discoverability, focus states, icon buttons, and tooltips.
- Add empty states and first-launch/open-project affordances.

Gate:

- settings load/save/migration tests
- keybinding conflict tests
- `zia_viperide_ux_smoke` or equivalent layout smoke
- screenshot/manual checklist at compact and wide sizes

Current status: partial. Font defaults/migration and the current settings
overlay are improved; the visual system and preferences UX are still below the
target bar.

### Milestone P5 - Dogfood Release Gate

Goal: use ViperIDE as the primary editor for a small real Viper project.

Gate:

- full relevant CTest subset plus full `ctest --test-dir build --output-on-failure`
- `./scripts/build_ide.sh`
- manual dogfood report covering large-file editing, IntelliSense, diagnostics,
  refactor, project tree, search, build/run, settings, session restore, and
  crash-free launch
- all P0/P1 dogfood defects fixed before scene editor planning resumes

## 7. Documentation Updates Required Per Milestone

- Update `viperide/README.md` feature list only for features that are genuinely
  usable in the app.
- Update this plan with completed milestone notes and known deferrals.
- Update old phase files only to point at this correction plan where they would
  otherwise overclaim.
- Add short manual checklists beside each CTest gate.

## 8. Scene Editor Resume Criteria

Scene editor work may resume only when:

- Milestones P0A through P5 are complete.
- The editor no longer has known typing-latency regressions.
- Completion/signature help are reliable enough for daily coding.
- Project explorer/refactor workflows are safe.
- Console/search/problems surfaces are real app features.
- Documentation no longer claims placeholder or partial features as complete.

After that, start a new scene-editor plan from the corrected baseline. Do not
reuse the old Phase 4/5 plan without re-review.
