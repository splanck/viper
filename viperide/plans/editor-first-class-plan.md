# ViperIDE Code Editor First-Class Plan

## 1. Reset

Status: active correction pass. The main automated editor, IntelliSense,
project-tree, output, settings recovery, and active-editor background semantic
job work is implemented; the remaining release blockers are richer semantic
quality, rich tool-surface polish, visual/preferences polish, and manual
dogfood evidence.

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
- Completion is structured, focus-safe, incrementally filtered, and backed by
  background jobs, but it still needs richer project-symbol ranking before it is
  first-class.
- Signature help does not reliably appear on `(` and is not structured enough
  for parameter names, overloads, active parameter highlighting, or docs.
- The project index exists and open-project indexing is now cooperative, but
  completion/signature/diagnostics still need broader project-symbol awareness
  and a shared scheduler before they can be considered first-class.
- Refactor support is too narrow. Rename exists, but broader refactor UX,
  preview, conflicts, and undo grouping are not product-grade.
- The console has append preservation, filtering, wrapping, copy, clear, and
  separate bottom-panel tabs, but it is still list-backed rather than a rich
  developer console surface.
- The file tree now has broad context-menu and keyboard workflow coverage,
  `.gitignore`, project-specific ignore patterns, and safe quoted Zia file-bind
  rewrites during file/folder rename.
- The UI shell is more complete than earlier passes credited: menu bar, toolbar,
  status bar with cursor info, breadcrumb, command palette, tabs, minimap,
  diagnostics/output panels, a welcome/empty state, and a bottom tool tab strip
  all exist. The remaining gaps are specific, not wholesale: no explicit
  activity bar, and the Search/Debug tabs are still list-backed work surfaces
  rather than rich tool views.
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

- Native `CodeEditor` no-wrap/no-fold paths, folded/wrapped visual-row lookup,
  content-height, scroll-clamp, locate-row, content-width, syntax-state, and
  highlight-span hot paths now have targeted caches and regression tests. The
  remaining native risks are broader multi-cursor/gutter/fold auto-detection
  audits and manual latency validation against real editing sessions.
- Completion, signature help, hover, symbols, and diagnostics now queue native
  background semantic jobs and poll results on the UI thread. The native bridge
  caps concurrent semantic workers so cancelled/stale jobs cannot pile up during
  typing. They still take a revision-keyed source snapshot when a job starts, so
  snapshot cost and real latency still need dogfood validation.
- Project opening now avoids automatic workspace parsing on the UI hot path.
  Explicit Definition/References/Rename commands lazily start the project index,
  pump only bounded slices before querying, skip oversized sources, and sync
  dirty open Zia buffers first. Quick Open uses the maintained project-tree file
  cache, hidden workspace/cache directories are excluded from project walks,
  `.gitignore` patterns are cached per root instead of reread for every file,
  and project search discovers paths plus scans file contents in frame-sized
  slices with cancellation.
- Build/debug output still stores a whole accumulated output string for final
  diagnostics parsing, but active build/run streaming now exposes deltas to the
  UI instead of repainting from the full string every frame.
- `VIPERIDE_PERF` / `VIPERIDE_PERF_LOG` now records frame, controller,
  project-index update count/bytes, full-buffer copy, layout-scan, syntax, and
  highlight counters. Automated native and IDE probes guard the known hot paths,
  but the final release gate still needs a manual latency report from
  dogfooding.

Code evidence from this audit (paths verified against the current nested tree;
the IDE sources moved into `editor/`, `commands/`, `build/`, `core/`, `ui/`):

- `src/lib/gui/src/widgets/vg_codeeditor.c` owns the native layout hot paths.
  The no-wrap/no-fold common case is O(1), and folded/wrapped visual-row,
  content-height, scroll-clamp, locate-row, and content-width paths now use
  layout caches instead of repeated full-document scans. Syntax and highlight
  work also has dirty-line/state and line-range indexing.
- `viperide/src/editor/completion.zia`, `signature.zia`, `diagnostics.zia`,
  `hover.zia`, and `symbols.zia` use a shared `EditorScheduler`,
  revision-keyed snapshots, and `Viper.Zia.SemanticJob` handles so semantic
  analysis runs on worker threads and stale results are dropped by
  revision/path/cursor checks before touching UI state.
- `viperide/src/editor/project_index.zia` has lazy cooperative workspace
  indexing. Definition/References/Rename start indexing only when explicitly
  requested, pump only a bounded index slice before querying, and open dirty Zia
  documents sync before project queries without repeatedly pushing unchanged
  buffers.
- `viperide/src/commands/search_commands.zia` discovers direct/fallback search
  paths incrementally and scans contents cooperatively; cached project searches
  skip discovery by using the project-tree file cache.
- `viperide/src/build/build_system.zia` and `viperide/src/commands/build_commands.zia`
  now stream active output as deltas, but final diagnostics still parse an
  accumulated output string instead of a bounded stream of records.

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
- Explicitly out of scope for this plan (named to prevent creep): real debugger
  execution, BASIC semantic services (correctly gated off today via
  `editor/language_service.zia`), split/diff editors, and SCM/git integration.

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
  - DONE: no-wrap/no-fold paint, scroll clamp, scrollbar metrics, and cursor
    visual-row calculations are O(1) (early returns at vg_codeeditor.c:741, 787,
    859, 895).
  - DONE: folded/wrapped content-height, visual-row, scroll-clamp, and locate-row
    paths use a visual-row prefix cache keyed by layout generation and content
    width.
  - DONE: `codeeditor_content_draw_width` no longer performs three full document
    height passes in wrap mode.
  - add line/wrap prefix-sum or Fenwick-style indexing if word-wrap remains
    supported for large files
  - DONE: Zia code folding is now fed by document symbols instead of the
    indentation auto-detector in the active IDE path; text replacement clears
    stale native fold regions.
  - audit per-line gutter-icon and extra-cursor paint loops for large
    multi-cursor/folded buffers
  - move syntax highlighting out of paint into dirty-line/versioned caches (DONE:
    edited lines invalidate individually; block-comment state cached per line)
  - index highlight spans by line range instead of scanning every span for every
    visible line (DONE: paint looks up only spans intersecting the visible line)
  - make minimap rendering revision-based and incremental, or disable it by
    default until it is cheap (DONE: off by default; samples large files)
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
- Native `CodeEditor` performance counters track layout scans, syntax-highlight
  calls, highlight-span checks, full-buffer copies, and copied bytes. The
  project indexer tracks update count and source bytes pushed per perf window.
  Zia can now read the aggregate layout scan count plus the
  syntax/highlight/copy/index counters needed by ViperIDE probes and perf
  logging.
- The no-wrap/no-fold editor path avoids whole-document layout scans for content
  height, visual-row lookup, scroll-top lookup, cursor movement, and scroll-to
  operations.
- `EditorEngine.GetTextSnapshot()` is revision-keyed, so repeated semantic
  controllers share one full-buffer materialization per content revision.
- Project indexing starts lazily for explicit project navigation/refactor
  commands and pumps frame-sized slices. Quick Open uses the maintained
  project-tree file cache instead of fresh enumeration.
- Project/folder search now scans file contents cooperatively over multiple
  frames and can be canceled, instead of reading every file in one command
  handler.
- Active build/run output now consumes incremental output deltas, so the UI no
  longer receives the full accumulated output string every frame while a job is
  running.
- Output auto-scroll now uses a non-selecting `ListBox.ScrollToBottom()` runtime
  helper instead of selecting `Count - 1` for every appended row, avoiding the
  O(n^2) UI-thread path that made large build/test output beachball the IDE.
- Syntax highlighting no longer bumps the global syntax generation for ordinary
  single-line edits; edited lines are invalidated directly, Zia block-comment
  state is cached per line, and highlight spans are sorted so paint can stop
  scanning once spans pass the visible line.
- The minimap now defaults off for new settings and samples large files when
  visible instead of drawing one bar for every line.
- Added `zia_viperide_editor_hot_path` to verify revision behavior for set
  text, insert, undo, redo, cursor-only movement, idle index sync, and a
  20k-line cursor-movement smoke. It also verifies project-index update
  counters/bytes increment only when content is actually pushed and reset cleanly
  between perf windows.
- Added native `test_vg_codeeditor_perf` for 50k-line no-wrap cursor/scroll,
  folded/wrapped layout-cache regressions, highlight span indexing, typing-burst
  and large-selection copy/layout guards, full-text copy counter behavior, and
  wall-clock 5k/20k typing-plus-paint, 50k scroll/paint, pointer selection-drag,
  and minimap paint budgets.
- This is not complete. Current coverage proves several hot-path invariants, not
  full user-perceived responsiveness.
- Still open: manual latency profiling on real projects, real-window/canvas paint
  timing beyond the headless native paint path, and dogfood validation with
  diagnostics, outline, minimap, and output panes visible.

Latest pass notes:

- Exposed the remaining low-level `CodeEditor` performance counters to Zia:
  syntax-highlight calls, syntax-state line scans, highlight-span checks, and
  full-text copied bytes, with graphics and non-graphics runtime coverage.
- Added a visual-row/content-height cache keyed by layout generation and content
  width so folded and wrapped large buffers reuse source-line to visual-row
  mappings instead of repeatedly scanning every line during navigation/scroll
  math. The wrapped content-width calculation no longer performs three document
  height passes.
- Added native large-file coverage for folded and wrapped layout-cache paths.
- Added an opt-in ViperIDE perf monitor (`VIPERIDE_PERF` or
  `VIPERIDE_PERF_LOG`) that records frame/render/controller timings plus editor
  and project-index update counters without recompiling.
- Added a shared `EditorScheduler` and wired completion, diagnostics, signature,
  hover, symbols, and active project-index sync through it. Completion,
  diagnostics, signature, hover, and symbols now start native background
  `Viper.Zia.SemanticJob` work when scheduled; UI code only polls completion
  state and materializes results on the main thread. Synchronous semantic calls
  remain only as weak-stub/null-job fallback paths.
- Definition, References, and Rename no longer synchronously drain the entire
  pending project index before querying; they pump a bounded slice and query the
  current partial index.
- Definition, References, and Rename now sync all open Zia documents before the
  project query so dirty non-active tabs are represented in the index.
- Highlight spans are line-indexed, project/folder search uses the maintained
  project file cache when available, unchanged open documents are not repeatedly
  pushed into the project index, and `zia_viperide_editor_hot_path` now includes
  a large-buffer cursor-movement timing/copy budget.
- Direct fallback search no longer calls `FileIndex.Enumerate` from the command
  path. It discovers directories incrementally and scans discovered files in
  frame-sized slices, while cached project searches still skip discovery.
- `EditorScheduler` now exposes per-kind generation/current/cancel helpers so
  future async work can reject stale completions, diagnostics, symbols, hover,
  signature, index, search, or quick-open jobs.
- Added native wall-clock gates for 5k/20k typing-plus-paint, 50k scroll/paint,
  pointer-driven selection drag, and minimap paint. These are headless native
  gates, not a substitute for dogfooding the real window.
- Semantic fold-region refresh now reuses a pre-split source line table and caps
  generated regions per pass so symbol-heavy files cannot spend unbounded time
  registering fold regions on the UI thread; `zia_viperide_editor_hot_path`
  includes a fold-generation budget and verifies hidden folding no longer
  recomputes semantic fold regions on every edit.
- Native semantic jobs now have a small worker cap so diagnostics, completion,
  signature, hover, and symbols cannot spawn unbounded detached compiler work
  when edits invalidate older jobs. Project indexing no longer auto-pumps from
  the frame loop; explicit project-index commands start indexing lazily, pump
  bounded slices, skip oversized source files, and `FileIndex` hard-excludes
  hidden workspace paths so project roots do not traverse local tool/cache
  worktrees.
- Signature-help refresh now debounces follow-up cursor/revision updates and
  does not cancel/restart a semantic signature job while one is still running.
  This prevents typing inside an argument list from saturating the semantic
  worker slots with stale parse jobs.
- Runtime extern documentation is no longer generated during generic semantic
  initialization. Diagnostics/type-check jobs keep the runtime symbol table
  lightweight, while completion/signature/hover still surface generated runtime
  metadata where it is actually displayed.
- The main loop now preserves the user's "Run diagnostics while typing" setting
  instead of re-enabling live diagnostics every frame from language capability
  alone.
- The GUI runtime now clears paint-dirty flags after full repaints and skips
  framebuffer present/repaint work on idle frames, pumping OS events plus a short
  sleep instead. Overlay font setters also avoid marking themselves dirty when
  the font is unchanged, so idle editor frames no longer force whole-window
  redraws.
- Still open after this pass: manual latency report, richer semantic quality,
  and longer manual stress coverage across large workspaces.

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
- Completion now consumes structured runtime records with label, insert text,
  kind, detail, documentation/source fields, replacement range, commit
  characters, and snippet flag. Accept uses the replacement range rather than a
  guessed word under the cursor.
- Completion rows now render compact documentation/source metadata when the
  structured record provides it.
- Native completion now populates documentation for current-file and bound-file
  exported declarations from adjacent `///` doc comments.
- Callable completions now commit on advertised commit characters such as `(`
  while preserving the typed character for signature-help triggers.
- Snippet completions now carry cursor-offset metadata through the runtime
  record and acceptance path so the cursor lands inside the inserted snippet.
- Completion context detection now recognizes spaced type annotations such as
  `var value: I` and `new` expressions, and the IntelliSense probe covers both
  type-name completion paths.
- Completion now executes through a background semantic job after trigger or
  debounce; stale results are dropped if the revision, path, or cursor changes
  before the job completes.
- Bound file modules now participate in completion: Ctrl+Space can surface the
  module root, and `Module.` lists exported symbols from the bound file. Native
  `test_zia_completion_engine` covers module-name and exported-symbol
  completion.
- Native completion ranking now prefers visible locals and parameters over
  current-file globals.
- ViperIDE now maintains a frame-sliced workspace-symbol cache from the project
  file cache and merges matching project functions/types/modules into
  completion results after local semantic items, including dirty open documents.
- Automatic completion triggers are suppressed inside line comments and open
  string literals; explicit completion remains available.
- Runtime member completion now layers curated prose for common Terminal, File,
  Path, App, and CodeEditor APIs ahead of generated signature/target metadata.
- Still open: richer receiver/member ranking, broader project/imported
  signature-hover documentation, and deeper semantic scoring across workspace
  candidates.

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
  covered by the IntelliSense probe. It also has an explicit
  command-palette/shortcut trigger (`Trigger Signature Help`, `Ctrl+Shift+Space`)
  gated to languages with signature-help support.
- Signature help has a current-file source fallback for incomplete calls such as
  `foo(`, so locally declared functions can show real parameter names before
  semantic analysis succeeds.
- The popup is taller/wider so two-line signatures are not immediately clipped.
- Signature help now uses a structured runtime map with availability, display,
  active parameter, overload count, documentation/source, name, return type, and
  parameter records; hover uses structured maps for title/type/docs/source.
- The signature popup now renders the active parameter inline by bracketing the
  current structured parameter, and `zia_viperide_intellisense` asserts the
  active-parameter display.
- Signature and hover popups now render structured documentation/source metadata
  when present.
- Structured signature and hover maps now populate documentation from adjacent
  `///` comments on current-source declarations.
- Signature and hover semantic queries now run as background semantic jobs, with
  stale revision/path/cursor or mouse-position results ignored before display.
- Bound-file module-export signature fallback now preserves exported parameter
  names in native completion coverage.
- Bound-file module-export signature and hover results now propagate adjacent
  `///` declaration documentation through synchronous and async structured maps.
- Runtime API completion, signature help, and hover now surface generated
  documentation from runtime metadata: class/member/property names, generated
  parameter names, signatures, and canonical runtime targets. Common runtime
  APIs also carry curated prose before the generated metadata.
- Signature help now returns structured overload records and overload counts for
  current-source overloads, and the popup marks the active overload as `1/N`.
- Signature overloads can be cycled from the popup keyboard path and through
  command-palette/shortcut commands (`Signature: Next Overload` /
  `Signature: Previous Overload`); the command registry gates these commands to
  languages with signature-help support and the IntelliSense probe covers the
  controller navigation path.
- Still open: broader imported/project declaration fallback beyond bound-file
  exports.

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
- Diagnostics now start `BeginCheckForFile` background jobs after idle and apply
  records only when the result still matches the active revision/path.
- Diagnostic jobs now also carry the scheduler generation that queued the work,
  so canceled or superseded diagnostic generations are rejected before applying
  Problems rows, minimap markers, or inline highlights.
- Live diagnostics now remain disabled when the persisted auto-check setting is
  off; language capability no longer overrides the user's performance setting
  each frame.
- Problems rows now use shared fixed-column tool-row formatting for severity,
  source, location, code, message, and action, while retaining structured click
  locations and severity colors.
- Added a safe `Organize Binds` code action/command for Zia files; it sorts and
  deduplicates the leading bind block as a single editor edit and remains
  hidden/unsupported for BASIC/text files through language-service capabilities.
- Added a safe `Suppress Warning` command for Zia warnings; it inserts a
  `// @suppress(Wxxx)` comment above the warning at the cursor as one undoable
  editor edit.
- Suppressible Zia warning rows now surface `Suppress Warning` in the Problems
  action column instead of the generic no-fix placeholder; diagnostics without a
  safe implemented action still show `No quick fix`.
- Zia diagnostics now carry the first language-proposed fix-it through
  `Viper.Zia.Toolchain` records. Problems rows show the fix-it label when one is
  available, and `Apply Diagnostic Fix-It` (`Ctrl+.`) applies the first fix-it at
  the cursor as an undoable editor edit.
- `Create Missing Bind` recognizes undefined identifiers that exactly match
  known Viper runtime namespace aliases, inserts the corresponding `bind Alias =
  Viper.Namespace;` line into the top bind block, and leaves unrelated undefined
  identifiers without an unsafe quick fix.
- `Create Missing Bind` also scans the open project file cache and dirty open
  Zia documents for a missing top-level declaration or module name, inserting a
  relative `bind "./file";` only when exactly one project file candidate exists.
  Ambiguous matches intentionally leave the editor unchanged.
- Added a safe `Trim Trailing Whitespace` command; it removes trailing spaces
  and tabs as one undoable editor edit.
- Still open: broader language-proposed quick fixes beyond compiler-provided
  fix-its, known runtime namespace binds, and unambiguous project-file binds.

### E4 - Project Navigation and Refactoring

Goal: project intelligence should cover everyday refactor/navigation workflows.

Required changes:

- Keep Go to Definition, Find References, and Rename on structured project index
  APIs.
- Add a dedicated References/Results panel with preview text and grouping by
  file.
- Broaden navigation beyond document scope (currently document-symbols only):
  - workspace symbol search (Ctrl+T) over the project index
  - call hierarchy (incoming/outgoing)
  - symbol-driven (semantic) code folding — folding today is widget-level only
  - inlay hints (parameter-name and inferred-type) once the P1 structured records
    land
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
- File/folder rename import/bind update tests.

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
- Rename applies changes to the active editor tab through a single editor
  replacement so one Undo restores the active pre-rename buffer.
- Rename overlap validation now reports a multi-line conflict message with the
  canceled status, file path, both conflicting ranges, and both replacement
  names.
- Definition/References/Rename pump only a bounded indexing slice before the
  query, and changed open documents are synced without repeatedly updating
  unchanged buffers.
- Find References now emits grouped file headers and preview lines into a
  dedicated References bottom-panel tab, with structured location ids on
  clickable reference rows.
- Workspace symbol search (`Ctrl+T`) scans the maintained project file state,
  uses dirty open Zia documents, and emits structured result locations.
- Workspace-symbol completion no longer pumps project symbol parsing while the
  completion controller is idle; when completion is active, project-file symbol
  cache fills through async semantic jobs and a per-frame file budget.
- Automatic project indexing is throttled to one file every 250 ms instead of
  parsing multiple workspace files in every UI frame.
- Active dirty-buffer project-index sync no longer runs from the UI frame loop;
  navigation/refactor commands still force a current active-buffer sync before
  they query.
- Incoming Calls uses `ProjectIndexer.References` to list non-definition
  call-site rows in the References tab, grouped by enclosing Zia function and
  backed by structured location ids.
- Outgoing Calls scans the enclosing Zia function for call-like identifiers,
  resolves each through `ProjectIndexer.Definition`, and lists resolved targets
  in the References tab with structured definition locations.
- Zia code folding now derives fold regions from document symbols and brace
  ranges, so the active IDE path no longer depends on the native indentation
  auto-fold detector for semantic source files.
- Zia inlay hints now refresh on editor revision changes and render through
  native `CodeEditor.AddInlayHint` ghost text. The initial controller adds
  conservative end-of-line inferred type hints for simple `var` initializers and
  parameter-name summaries for same-file function calls.
- File/folder rename now previews the number of Zia bind paths that will change,
  allows cancel before moving the file/folder, updates quoted file-bind paths in
  open and closed project `.zia` files, and keeps `viper.project` entry paths in
  sync.
- Rename/workspace edits now store a pending undo snapshot for inactive affected
  open documents; when that tab is later active and the editor has no native
  undo entry, Undo restores the pre-rename content as the document's single
  workspace-edit undo step.
- Inline Local Variable is implemented for simple single-assignment Zia local
  declarations. It removes the declaration, replaces uses inside the enclosing
  function while skipping strings/comments, preserves undo via whole-document
  editor replacement, allows equality comparisons, and rejects later direct or
  compound mutations.
- Extract Local Variable is implemented for selected single-line Zia expressions
  inside functions. It prompts for a valid new local name, inserts a local
  declaration before the selection line, replaces the selected expression with
  the new identifier, and rejects multi-line selections or duplicate names.
- Extract Function is implemented for complete selected Zia statement lines
  inside a function. The initial safe path extracts to a sibling no-argument
  function, replaces the selected statements with a call, rejects duplicate
  function names, return/break/continue selections, unbalanced brace selections,
  captured locals declared before the selection, and locals declared inside the
  selection that are used afterward.
- Still open: richer parameterized/return-value extract refactors and AST-backed
  conflict analysis; Extract/Inline Local and Extract Function remain
  intentionally limited to simple safe forms.

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
  New Folder, Search in Folder, Run This File, Set as Project Entry, and Refresh.
- New file/folder actions use the clicked directory or the parent directory of
  the clicked file. Right-click selection is consumed so it does not also open
  the file as a left-click selection.
- Reveal is OS-aware: Finder on macOS, Explorer on Windows, `xdg-open` on Linux.
- Set as Project Entry updates or creates `viper.project` with a relative
  `entry` path and updates the in-memory project model.
- Keyboard/palette commands now cover file-tree new file, new folder, rename,
  delete, refresh, and project-entry workflows.
- Session persistence now records recent files, and closed named tabs are tracked
  for reopen-closed-file workflow.
- `viper.project` can provide `ignore`, `ignore-patterns`, or `exclude` entries
  for project-specific tree/search exclusions on top of hard excludes and
  `.gitignore`.
- Added `zia_viperide_file_tree` for the GUI hit-test binding, project-entry
  manifest update, selection data path, file/folder rename bind rewrite preview,
  open-document bind rewrites, closed-file bind rewrites, and project-entry
  rename follow-up.
- File/folder rename now retargets open documents and recently closed file
  history, while delete removes stale recently closed paths under the deleted
  file/folder. Tree operations also clear stale Problems/Search/References
  locations and rebuild the project index after moves.
- Still open: richer project-tree manual checklist evidence.

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
- Add tabbed bottom panel: Problems, Output, Search, References, Debug Console.
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
- Raw build/run output now renders through the native append-only
  `Viper.GUI.OutputPane` runtime widget, while filtered, wrapped, and clickable
  tool output keeps the existing `ListBox` row model.
- Existing Problems, Output, Search, References, and Debug Console list surfaces now sit
  behind a lightweight bottom tool tab strip instead of appearing as unrelated
  raw panels. Search, references, and debug output no longer reuse the build
  output list.
- Folder-scoped search is available from the file-tree context menu and reuses
  structured search result locations.
- Added Quick Open (`Ctrl+P`) for project file name/path fragments.
- Added `zia_viperide_console_search` for raw output pane append behavior,
  partial-line rebuild behavior, row-mode output helpers, whole-word search, and
  Quick Open ranking.
- Project/folder search uses cached project-tree file state when available and
  scans contents cooperatively with cancellation; direct fallback search now
  discovers directories incrementally instead of enumerating every path in the
  command handler. Build output still keeps a whole accumulated output string
  for final parsing.
- Search commands now support include/exclude path glob filters in addition to
  literal/regex content matching, case-sensitive, whole-word, extension,
  project-ignore, and folder-scope filtering. Both cached project search and
  direct fallback discovery honor the filters, and `zia_viperide_console_search`
  covers direct/cached filtered and regex searches.
- Output supports selected-row copy, clear, text filtering, and fixed-width wrap
  toggling through commands.
- Output/tool-panel listboxes now support multi-select range copy through a
  runtime `ListBox.GetSelectedText()` binding, and output auto-scroll can be
  toggled/locked without losing the user's selected row.
- Output auto-scroll is now selection-free through runtime
  `ListBox.ScrollToBottom()` / `ScrollToTop()` helpers, and large listbox layout
  skips full text-width scans once rows exceed the small-list threshold.
- Raw output streams avoid list row insertion entirely through
  `OutputPane.Append()`, so ordinary console updates no longer force listbox
  selection, measurement, or per-row layout work.
- Problems, Search, and References now use shared fixed-column tool-row helpers
  with muted group headers, stable kind/source/location/code columns, preview
  text, and structured location ids for path-safe navigation.
- The command palette now keeps unsupported language-service commands visible with
  an unavailable marker; selecting one reports a status/toast reason instead of
  dispatching a Zia-only semantic command for BASIC/text/scene files.
- Runtime `ListBox.ItemSetTextColor()` now lets list-backed tool rows carry
  severity colors. Problems rows color error/warning/info severity, and build
  output colors parsed diagnostics plus common success/failure/warning lines.
- Runtime `StatusBarItem.SetTextColor()` lets the shell color the diagnostics
  summary by worst active severity while preserving the compact status-bar text.
- Still open: richer output-console rendering beyond listbox rows, such as
  segmented log text and column-native result widgets.

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

- Default editor font size is 15, and persisted legacy sizes below 12 migrate to
  the readable default on load.
- Preferences font-size spinner now avoids the old tiny range.
- Docked Preferences side panel exposes font path, font size, tab width, insert
  spaces, word wrap, minimap, line numbers, diagnostics, folding, diagnostics
  delay, completion delay, auto-save, save-time whitespace/final-newline
  behavior, session restore, project restore, and close confirmation.
- Added settings migration and behavior coverage to `zia_viperide_phase0_phase1`.
- DONE: unknown INI sections are preserved. `SettingsManager.Save()` read-modify-
  writes the file (`Ini.Parse` -> set only `[settings]` keys -> `Ini.Format`), so
  `[session]`/`[recent]` and any unknown sections survive; the phase0_phase1 probe
  asserts this. Drop it from the open list.
- Added a keyboard-shortcuts command/list view with conflict detection.
- Auto-save is settings-backed and saves modified named files after editor idle;
  it pauses rather than overwriting if the file changed on disk.
- Preferences now live in the workbench right-side dock instead of a floating
  settings overlay, expose grouped defaults reset helpers for Appearance,
  Editing, IntelliSense, and Startup, and the console/search probe covers
  compact and wide side-panel layout smoke plus close behavior.
- E7-specific automated blockers are closed. Remaining visual/manual polish is
  tracked by E8/P5.

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

Current implementation status:

- Command palette discoverability is improved for capability-gated commands:
  unsupported semantic commands remain searchable with an unavailable marker, and
  selecting them reports the exact missing language-service capability in the
  status bar and toast.
- Command palette entries now use concise categories such as File, Edit, Search,
  Navigate, Refactor, Build, Debug, View, Settings, Output, and Explorer instead
  of leaking long command descriptions into the category prefix.
- Toolbar and status chrome now expose active build/run job state plus compact
  project, language-service, and diagnostics state.
- File/Search/Navigate menu entries now expose the same shortcut labels as the
  command registry for Quick Open, Search in Project, Go To Line/Definition,
  References, call hierarchy, workspace symbols, symbol outline, diagnostics
  navigation, and signature help. Save As and Save All now have distinct
  shortcuts, and Explorer context-menu rows show their keyboard commands.
- Still open: the broader visual system pass, icon polish, and manual
  compact/wide layout evidence.

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

### E10 - Core Text-Editing UX

Goal: deliver the everyday editing ergonomics that define a real editor. These
are felt on every keystroke and are currently absent; they matter more to
"first-class feel" than further console/settings polish.

Required changes (none of these exist today):

- Auto-indent on newline (language-aware: continue indent, indent after block
  open).
- Bracket/paren/quote auto-close, skip-over closing char, and matching-pair
  highlight.
- Comment toggle (line and block) command.
- Format-on-type and explicit Format Document / Format Selection commands.
- Duplicate line, move line up/down, smart home/end, expand/shrink selection.
- Trim trailing whitespace and ensure final newline on save (settings-gated).

Acceptance:

- Typing in real Zia source auto-indents and auto-closes pairs without fighting
  the user, and respects the tab-width / insert-spaces settings.
- Comment toggle and move/duplicate line work with single-step undo.
- Format commands are no-ops with a clear message until a formatter backend
  exists, rather than silently doing nothing.

Tests:

- Native or runtime tests for auto-indent, pair auto-close/skip, and line
  move/duplicate undo behavior.
- A probe asserting comment toggle round-trips and respects selection.

Sequencing: starts after P1 (fast editor + structured IntelliSense), per §6.

Current implementation status:

- Native `CodeEditor` now supports auto-indent on newline, bracket/paren/quote
  auto-close with skip-over, matching-pair highlight, smart Home/End, and
  pointer-driven selection drag. `test_vg_codeeditor_perf` covers auto-indent
  undo grouping, pair auto-close/skip undo behavior, same-line and multi-line
  pair highlight resolution, and pointer selection-drag performance.
- ViperIDE edit commands cover line comment toggle for single lines and selected
  line ranges, duplicate line, move line up/down, block comment toggle for
  Zia/text selections, and save-time trailing-whitespace/final-newline behavior
  behind settings.
- Expand Selection now provides a predictable word -> line -> enclosing brace
  block -> document ladder, with shrink reversing simple word/line/document
  selections back toward the originating word.
- Expand Selection now recognizes inline `(...)` and `[...]` delimiter
  expressions, so word selections inside calls/indexers expand to the enclosing
  expression before the whole line.
- `zia_viperide_intellisense` now audits single-step undo for block-comment
  toggles, selected-line comment toggles, duplicate line, and move-line
  commands.
- Format Document / Format Selection now use an initial formatter backend:
  Zia gets brace-aware indentation that respects the editor tab-size /
  insert-spaces settings and ignores braces in strings/comments, text selections
  get trailing-whitespace cleanup, and BASIC remains an explicit unsupported
  path until a BASIC formatter exists.
- `zia_viperide_intellisense` now audits Zia document formatting, selection
  formatting, text selection whitespace cleanup, unsupported BASIC formatting,
  and formatter undo.
- Still open: full AST-aware expansion and a full AST-backed formatter.

## 6. Revised Recovery Milestones

Order (preserves the perf-first gating in §4):

1. P0A/P0B/P0C - instrumentation, fold-path + counter exposure, and the shared
   `EditorScheduler`. Hard gate before any feature work.
2. P1 - structured language-service records and the project-index latency fix.
3. E10 - core editing UX (new; table-stakes, so it precedes further polish).
4. P2/P3 - refactor breadth, code actions, and finishing console/search surfaces.
5. P4/P5 - preferences/visual polish and the dogfood gate.

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
hot-path counters to Zia, `ProjectIndexer` records update count and source bytes
pushed into the semantic index, the editor hot-path probe asserts no full-buffer
copies for cursor-only movement/snapshot reuse and no index counter churn for
unchanged open buffers, and `test_vg_codeeditor_perf` covers a 50k-line no-wrap
cursor/scroll path, folded/wrapped large-file cache paths, typing bursts, large
selections, explicit 5k/20k typing-plus-paint wall-clock budgets, 50k
scroll/paint budgets, pointer selection-drag budgets, and a dedicated minimap
paint budget. An opt-in `PerfMonitor` logs frame/render/controller timings with
editor counters plus `projectIndexUpdates` and `projectIndexBytes` when
`VIPERIDE_PERF` or `VIPERIDE_PERF_LOG` is set. Still missing: manual top-five
latency report and real-window dogfood timing across large projects.

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

Current status: substantially implemented for the known large native hot paths.
The no-wrap/no-fold path avoids whole-document scans for common
layout/scroll/cursor operations, ordinary single-line edits invalidate only the
edited line's syntax cache, Zia block-comment state is cached per line,
highlight spans are indexed by line, and the minimap is off by default plus
sampled when visible. The folded/wrapped layout paths cache visual-row prefix
data by layout generation and content width, and wrap-mode content width no
longer runs three full height passes. Text replacement clears stale fold regions,
and ViperIDE's Zia fold regions come from document symbols rather than the
native indentation auto-detector. Native headless wall-clock gates now cover
typing-plus-paint, scroll/paint, pointer selection drag, and minimap paint. Still
missing: word-wrap edit-local prefix maintenance instead of full cache rebuild
on layout invalidations and real-window/canvas paint validation.

### Milestone P0C - Semantic Work Scheduler

Goal: make language services cooperate with editing instead of competing with
it.

This is the highest-leverage architectural item: today there is **no** shared
scheduler. Each controller self-times against `Viper.Time.GetTickCount()` with its
own constant (`COMPLETION_DEBOUNCE_MS=180`, diagnostics `DEBOUNCE_MS=500`,
`HOVER_DELAY_MS=300`, `INDEX_IDLE_MS=350`). A single scheduler is the gate for
"no synchronous semantic work in the hot path."

Work:

- Add one `EditorScheduler` (new file `viperide/src/editor/scheduler.zia`) that
  owns debounce/coalesce/cancel for all semantic work. Concrete contract:
  - a job-kind enum: diagnostics, completion, signature, hover, symbols,
    index-sync, search, quick-open-refresh
  - each job carries the document revision it was queued against
  - a per-kind generation/cancellation token so a newer revision cancels or
    supersedes an in-flight job and stale results are dropped on arrival
  - per-kind debounce/idle thresholds owned by the scheduler, not the controllers
- Register all six controllers (completion, signature, hover, diagnostics,
  symbols, active-index sync) through the scheduler instead of self-timing.
- Move those controllers off synchronous frame-loop execution.
- Replace routine whole-buffer reads with `GetTextSnapshot()` cached snapshots
  created only for jobs that need them (revision-keyed; already exists).
- Make project open/index/search cooperative and frame-budgeted.

Gate:

- no synchronous semantic query in the keypress-to-paint path
- no full-buffer copy on cursor-only movement, scroll, or popup navigation
- diagnostics and symbols cannot overwrite newer revisions
- project search and indexing can be canceled or paused without blocking typing

Current status: mostly implemented for active editor language services.
Diagnostics, symbols, completion, signature help, hover, active-buffer index
sync, project indexing, and Quick Open avoid routine full-buffer copies or full
project enumeration in the common frame loop. Project indexing is cooperative. A
shared `EditorScheduler` owns debounce/coalescing state, and completion,
diagnostics, signature, hover, and symbols now use native `SemanticJob`
background workers with stale-result rejection. Search path discovery is
cooperative for both cached and direct fallback searches. Still missing:
cancellable/pausable long index drains beyond the current frame-sliced pump and
manual proof that this meets real typing-latency budgets.

### Milestone P1 - IntelliSense Reliability

Goal: make completion, signature help, and hover useful enough for daily coding
after the editor is fast.

The concrete blocker is the wire protocol, not the UI. Today completion returns a
positional `label\tinsertText\tkind\tdetail` string (`completion.zia` parses it by
splitting on tabs), signature help is plain text plus a numeric active-parameter
marker, and hover is a bare string. First-class IntelliSense requires the Zia
language APIs (`src/frontends/zia/ZiaCompletion.cpp` + the runtime bridge) to emit
structured records the UI consumes directly — never re-parsing decorated display
strings.

Work:

- Define and emit structured records:
  - `CompletionItem`: label, insertText, kind, detail, documentation, source,
    replacementRange, commitCharacters, isSnippet.
  - `SignatureHelp`: name, params[{name,type,doc}], returnType, activeParameter,
    activeSignature, overloads[], documentation.
  - `Hover`: title/signature, type, documentation, source range.
- Completion: replacement ranges, commit characters, snippet cursor placement,
  ranking (locals/params/fields/receiver members above globals/runtime/keywords),
  and project-symbol inclusion.
- Signature help: active-parameter highlight, overload navigation, docs, and
  incomplete-call recovery (`foo(`, `foo(a,`, `obj.method(`, nested calls).
- Hover backed by structured symbol data and dwell scheduling.

Gate:

- `zia_viperide_intellisense`
- project completion and signature tests using unsaved cross-file content
- manual dogfood on ViperIDE source for locals, members, imports, runtime APIs,
  signatures, hover, accept, dismiss, and undo

Current status: partial but no longer stringly typed or UI-thread semantic
bound. Focus-safe completion filtering, cached snapshot use, structured
completion/signature/hover runtime records, replacement-range acceptance,
snippet cursor placement metadata, basic callable commit-character acceptance,
signature triggering, nested active-parameter tracking, and current-file
incomplete-call signature fallback with real parameter names exist. Signature
help now visually marks the active parameter in the popup text, and completion,
signature, and hover display structured documentation/source metadata when it is
provided. Completion documentation is now populated from adjacent `///` comments
for current-file declarations and bound-file exports. Signature/hover maps
populate documentation for current-source declarations, bound-file module
exports, and runtime APIs from generated metadata docs. Completion, signature,
hover, diagnostics, and symbols run semantic analysis on native background jobs
and poll results on the UI thread.
Definition, References, and Rename no longer block to drain the full pending
index before querying. Bound file module roots and exported members now have
native completion coverage, visible locals/parameters rank above globals,
workspace-symbol completions are merged from a frame-sliced project cache after
local semantic results, and bound-file exported signature fallback preserves
parameter names. Signature overload navigation now has keyboard/controller and
command-palette coverage. The remaining gaps are semantic quality: richer
receiver/member ranking, authored runtime API docs beyond generated metadata,
broader imported/project signature-hover docs, and deeper workspace scoring.

### Milestone P2 - Refactor and Project Explorer

Goal: make project navigation and file operations safe and discoverable.

Work:

- Grouped References panel with preview snippets.
- Rename preview polish, undo grouping, and broader conflict diagnostics.
- File/folder rename with import/bind rewrite only when language support can do
  it safely.
- Keyboard commands and full context-menu workflows for the file tree.
- Recent files, recently closed tabs, and project-specific ignored-file
  configuration.

Gate:

- `zia_viperide_phase0_phase1`
- `zia_viperide_file_tree`
- rename/refactor all-or-nothing tests
- manual refactor and project-tree checklist

Current status: partial. Rename hardening, a dedicated grouped References tab,
Set as Project Entry, recent/reopen file workflows, keyboard tree commands, and
many context-menu actions exist. `.gitignore` and project-specific ignore
patterns are respected by the tree/search paths. File/folder rename now has a
cancelable bind-rewrite preview and safely rewrites quoted Zia file binds in open
and closed files.

### Milestone P3 - Console, Problems, Search, and Quick Open

Goal: replace listbox-style output with real work surfaces.

Work:

- Bottom panel with Problems, Output, Search, References, and Debug Console tabs.
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
workspace symbols, cooperative direct/cached file discovery and content search,
delta-based active job output, auto-scroll lock, range copy, and lightweight
Problems/Output/Search/References/Debug Console tab surfaces exist. Output
selected-row/range copy, clear, filter, and wrap toggles are implemented, and
search results have file grouping plus include/exclude path filters and regex
content matching. Console/search surfaces are still list-backed and not
first-class rich panes.

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

Current status: partial. Font defaults/migration, editor behavior preferences,
the docked Preferences side panel, per-section default reset helpers,
compact/wide layout smoke, auto-save, save-time whitespace preferences,
scheduler delay preferences, a keybindings/conflict view, live build/run
toolbar/menu state, categorized command palette entries, disabled command
explanations, shortcut-labeled File/Search/Navigate/Explorer menu commands, and
compact status-bar project, language-service, job, and diagnostics state exist;
the broader visual system and manual compact/wide evidence are still below the
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

Latest automated evidence (2026-05-23):

- Passed focused editor/tool gates:
  `ctest --test-dir build -R '^(test_vg_codeeditor_perf|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed adjacent runtime/project gates:
  `ctest --test-dir build -R '^(test_rt_gui_runtime|test_rt_gui_ide|zia_viperide_file_tree|zia_viperide_phase2_phase3)$' --output-on-failure`.
- Passed focused regression after editor/tester feedback:
  `ctest --test-dir build -R '^(test_rt_gui_runtime|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed semantic folding/editor regression:
  `ctest --test-dir build -R '^(test_vg_codeeditor_perf|zia_viperide_intellisense)$' --output-on-failure`.
- Passed workspace-symbol completion regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed focused semantic/editor/tool regression:
  `ctest --test-dir build -R '^(test_vg_codeeditor_perf|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed organize-binds code-action regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed incoming-calls navigation regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed workspace-cache stall regression:
  `ctest --test-dir build -R '^(zia_viperide_editor_hot_path|zia_viperide_intellisense)$' --output-on-failure`.
- Passed call-hierarchy/perf regression:
  `ctest --test-dir build -R '^(test_vg_codeeditor_perf|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed active rename undo regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed richer rename conflict regression:
  `ctest --test-dir build -R '^(zia_viperide_phase0_phase1|zia_viperide_intellisense)$' --output-on-failure`.
- Passed trim-whitespace code-action regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed brace-block expand-selection regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed IntelliSense metadata display regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed IntelliSense metadata/editor hot-path focused regression:
  `ctest --test-dir build -R '^(test_vg_codeeditor_perf|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed declaration documentation completion regression:
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed signature/hover declaration documentation regression:
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed combined completion-docs/editor focused regression:
  `ctest --test-dir build -R '^(test_zia_completion_engine|test_vg_codeeditor_perf|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed edit-command undo audit regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed inline expand-selection regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed suppress-warning code-action regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed search include/exclude filter regression:
  `ctest --test-dir build -R '^zia_viperide_console_search$' --output-on-failure`.
- Passed disabled command explanation and focused editor/search regressions:
  `ctest --test-dir build -R '^(zia_viperide_phase0_phase1|zia_viperide_console_search|zia_viperide_editor_hot_path)$' --output-on-failure`.
- Passed literal/regex search mode regression:
  `ctest --test-dir build -R '^zia_viperide_console_search$' --output-on-failure`.
- Passed command palette category regression:
  `ctest --test-dir build -R '^zia_viperide_phase0_phase1$' --output-on-failure`.
- Passed project-index perf counter regression:
  `ctest --test-dir build -R '^zia_viperide_editor_hot_path$' --output-on-failure`.
- Passed perf-log smoke with `VIPERIDE_PERF_LOG=/tmp/viperide_perf_index.log`,
  confirming `projectIndexUpdates` and `projectIndexBytes` appear in real
  ViperIDE frame logs.
- Passed interactive perf smoke after the hidden-folding fix with
  `VIPERIDE_PERF_LOG=/tmp/viperide_interactive_perf_after.log`; the saved-session
  text-input window dropped `symbolsUs` from about 40 ms before the fix to
  sub-0.1 ms after the fix.
- Passed focused ViperIDE and Zia semantic regressions:
  `ctest --test-dir build -R '^(zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`
  and
  `ctest --test-dir build -R '^(test_zia_completion|test_zia_completion_engine|test_rt_zia_completion_stub|native_smoke_viperide_completion_arm64)$' --output-on-failure`.
- Passed full release-gate CTest:
  `ctest --test-dir build --output-on-failure` with 1695/1695 tests passing
  and only the expected skipped tests (`macos_toolchain_installer_smoke`,
  `test_rt_audio_unavailable`).
- Passed responsiveness regression gates after the semantic-worker/index-idle
  fix:
  `ctest --test-dir build -R '^(test_rt_ide_workspace|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search)$' --output-on-failure`.
- Passed post-fix perf smoke with
  `VIPERIDE_PERF_LOG=/tmp/viperide_perf_fix.log`; the restored 427-line session
  stayed near frame cadence with no full-text copies and project-index work
  draining to zero after startup.
- Passed Problems action/source-column regression:
  `ctest --test-dir build -R '^(zia_viperide_console_search|zia_viperide_intellisense)$' --output-on-failure`.
- Passed live build/run toolbar/menu state regression:
  `ctest --test-dir build -R '^zia_viperide_console_search$' --output-on-failure`.
- Passed hidden-worktree/file-index regression and post-cache startup smoke:
  `ctest --test-dir build -R '^(test_rt_ide_workspace|zia_viperide_editor_hot_path|zia_viperide_console_search|zia_viperide_intellisense|test_rt_gui_ide|test_rt_gui_runtime)$' --output-on-failure`;
  `VIPERIDE_PERF_LOG=/tmp/viperide_perf_after_ignore_cache.log` stayed at
  frame cadence with no full-text copies while automatic index work remained
  bounded.
- Passed severity-colored diagnostics status summary regression through
  `zia_viperide_console_search` plus graphics/non-graphics GUI runtime tests.
- Passed tree move/delete stale-state regression:
  `ctest --test-dir build -R '^(zia_viperide_phase0_phase1|zia_viperide_file_tree|zia_viperide_console_search|zia_viperide_editor_hot_path)$' --output-on-failure`.
- Passed project/language/job status chrome regression:
  `ctest --test-dir build -R '^(zia_viperide_console_search|zia_viperide_phase0_phase1|zia_viperide_file_tree)$' --output-on-failure`.
- Passed formatter command regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed diagnostic fix-it regression:
  `cmake --build build --target zia -j 8` followed by
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed missing-runtime-bind quick-fix regression:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed missing-project-bind quick-fix regression:
  `ctest --test-dir build -R '^(zia_viperide_intellisense|zia_viperide_editor_hot_path|zia_viperide_console_search)$' --output-on-failure`.
- Passed `./scripts/build_ide.sh`, producing `viperide/bin/viperide`.
- Passed responsiveness regression after removing automatic frame-loop project
  indexing and making controller enabled-state setters idempotent:
  `ctest --test-dir build -R '^(test_vg_tier2_fixes|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path|zia_viperide_intellisense)$' --output-on-failure`.
- Passed interactive perf smoke with
  `VIPERIDE_PERF_LOG=/tmp/viperide_perf_responsive.log`; the restored 427-line
  session stayed at frame cadence after simulated typing, with project-index
  updates dropping to zero after startup/open-doc sync.
- Passed completion Tab-acceptance regression, including the native-editor
  consumed-Tab fallback and held-key latch:
  `ctest --test-dir build -R '^(zia_viperide_intellisense|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path)$' --output-on-failure`.
- Verified the editor font default remains 15 points through
  `zia_viperide_phase0_phase1`.
- Passed signature-help controller regression for popup visibility after `(`
  and `,`, plus comma active-parameter tracking:
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`.
- Passed automatic-completion context regression for line comments and string
  literals:
  `ctest --test-dir build -R '^(zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`.
- Passed spaced type-annotation and `new` expression completion regression:
  `cmake --build build --target zia -j 8`, `./scripts/build_ide.sh`, and
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed bound-file signature/hover declaration documentation regression:
  `cmake --build build --target zia -j 8` and
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed runtime metadata documentation regression for completion, signature
  help, synchronous hover, and async signature/hover:
  `cmake --build build --target test_zia_completion_engine zia -j 8` and
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed post-runtime-docs editor hot-path regression and standalone rebuild:
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`
  and `./scripts/build_ide.sh`.
- Passed signature-overload structured-map regression:
  `cmake --build build --target test_zia_completion_engine zia -j 8` and
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`.
- Passed post-overload hot-path regression and standalone rebuild:
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`
  and `./scripts/build_ide.sh`.
- Passed post-responsiveness regression and standalone rebuild after removing
  eager runtime-doc generation from semantic initialization and preserving the
  auto-check setting:
  `cmake --build build --target zia test_zia_completion_engine -j 8`,
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_phase0_phase1|zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed idle-render responsiveness regression after adding dirty-frame skipping
  to the GUI runtime:
  `cmake --build build --target viper test_rt_gui_runtime test_vg_tier2_fixes test_vg_widgets_new -j 8`,
  `ctest --test-dir build -R '^(test_rt_gui_runtime|test_vg_tier2_fixes|test_vg_widgets_new|test_zia_completion_engine|zia_viperide_phase0_phase1|zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`. A production-mode idle run
  with `VIPERIDE_PERF_LOG=/tmp/viperide_perf_final.log viperide/bin/viperide`
  measured about 4.5% CPU via `ps`, down from about 40% before the runtime
  relink.
- Passed docked Preferences side-panel regression:
  `cmake --build build --target zia -j 8`,
  `../../build/src/tools/zia/zia console_search_probe.zia` from
  `viperide/src`,
  `ctest --test-dir build -R '^(zia_viperide_console_search|zia_viperide_phase0_phase1)$' --output-on-failure`,
  and `git diff --check`.
- Passed structured tool-row regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^(zia_viperide_console_search|zia_viperide_intellisense)$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed authored runtime API documentation regression:
  `cmake --build build --target test_zia_completion_engine zia -j 8`,
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense)$' --output-on-failure`,
  `git diff --check`, `cmake --build build --target viper -j 8`, and
  `./scripts/build_ide.sh`.
- Passed selected-line comment regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed inactive rename undo regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed inline local variable refactor regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`,
  `ctest --test-dir build -R '^zia_viperide_phase0_phase1$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed extract local variable refactor regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^zia_viperide_intellisense$' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed post-documentation hot-path focused regression and standalone rebuild:
  `ctest --test-dir build -R '^(zia_viperide_editor_hot_path|zia_viperide_intellisense)$' --output-on-failure`
  and `./scripts/build_ide.sh`.
- Passed post-invalidation-churn regression after making repeated widget/layout
  setters no-op when values are unchanged:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R 'test_vg|test_rt_gui_runtime|test_rt_gui_ide|zia_viperide_editor_hot_path|zia_viperide_intellisense|zia_viperide_console_search' --output-on-failure`,
  `git diff --check`, and `./scripts/build_ide.sh`.
- Passed a short live-window perf smoke with
  `VIPERIDE_PERF_LOG=/tmp/viperide-perf.log ./viperide/bin/viperide`; the
  restored 427-line session held about 56 frames/second, with controller work
  near zero after startup and no full-text copies/layout scans.
- Passed output auto-scroll responsiveness regression after replacing
  append-time last-row selection with non-selecting scroll helpers:
  `ctest --test-dir build -R '^(test_vg_tier2_fixes|zia_viperide_console_search|test_rt_gui_runtime)$' --output-on-failure`,
  `cmake --build build --target viper -j 8`, `git diff --check`, and
  `./scripts/build_ide.sh`.
- Passed a short post-fix live-window typing smoke with
  `VIPERIDE_PERF_LOG=/tmp/viperide-perf-fixed.log ./viperide/bin/viperide`;
  the restored 427-line session stayed near 58-60 frames/second after typing,
  with controller work near zero and no layout scans.
- Passed native `OutputPane` runtime + ViperIDE output integration:
  `cmake --build build --target test_vg_tier2_fixes -j 8`,
  `cmake --build build --target test_vg_audit_fixes -j 8`,
  `cmake --build build --target zia -j 8`,
  `cmake --build build --target viper -j 8`,
  `ctest --test-dir build -R '^(test_vg_tier2_fixes|test_vg_audit_fixes|test_rt_gui_runtime|zia_viperide_console_search)$' --output-on-failure`,
  `ctest --test-dir build -R '^(test_runtime_classes_catalog|test_runtime_surface_audit|test_linker_runtime_import_audit)$' --output-on-failure`,
  `ctest --test-dir build -R '^(test_zia_completion_engine|zia_viperide_intellisense|zia_viperide_phase0_phase1|zia_viperide_editor_hot_path)$' --output-on-failure`,
  and `./scripts/build_ide.sh`.
- Passed a short live-window output-pane perf smoke with
  `VIPERIDE_PERF_LOG=/tmp/viperide-outputpane-perf.log ./viperide/bin/viperide`;
  the restored 427-line session held about 56-60 frames/second after typing,
  with controller work near zero, no layout scans, and no repeated full-text
  copies.
- Passed Zia inlay-hint controller/runtime integration:
  `cmake --build build --target test_vg_tier2_fixes -j 8`,
  `cmake --build build --target zia -j 8`,
  `cmake --build build --target viper -j 8`,
  `ctest --test-dir build -R '^(test_vg_tier2_fixes|test_rt_gui_runtime|test_runtime_classes_catalog|test_runtime_surface_audit|zia_viperide_intellisense|zia_viperide_editor_hot_path)$' --output-on-failure`,
  and `./scripts/build_ide.sh`.
- Passed menu shortcut discoverability regression after adding File/Search/
  Navigate/Explorer shortcut labels and resolving the Save As/Save All shortcut
  conflict:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^(zia_viperide_phase0_phase1|zia_viperide_console_search)$' --output-on-failure`,
  and `git diff --check`.
- Passed Extract Function safe-form regression:
  `cmake --build build --target zia -j 8`,
  `ctest --test-dir build -R '^(zia_viperide_intellisense|zia_viperide_phase0_phase1)$' --output-on-failure`,
  and `git diff --check`.
- Still missing for this release gate: the manual dogfood report.

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
