# Zanna Studio Source Map

This document is a detailed tour of `zannastudio/src/`. It complements
[architecture.md](architecture.md) by naming the concrete files that own the
current behavior. Use it when deciding where a change belongs.

Zanna Studio is organized around long-lived state objects that are created by
`main.zia` and pumped every frame. The source tree is not a plugin system and it
is not yet a highly decoupled workbench framework. It is a practical IDE
application with clear subsystem boundaries and several large coordinator files
that still need pressure taken off them over time.

## Root Entry Point

### `main.zia`

`main.zia` is the application bootstrap and frame loop. It creates the shell,
document manager, project manager, settings manager, session manager, editor
engine, editor controllers, build system, debug session, terminal controller,
SCM view, and command registry.

It also performs the top-level polling work:

- Read editor input and synchronize editor text back to the active document.
- Pump semantic controllers.
- Pump file and workspace watchers.
- Pump command palette, menus, toolbar buttons, context menus, and shortcuts.
- Dispatch commands to `commands/`.
- Update build/run/debug/terminal/SCM state.
- Maintain status bar and panel state.
- Save session state and clean up processes on exit.

`main.zia` is already beyond the preferred file-size budget. New behavior should
usually be added to a focused module and called from `main.zia`, not implemented
inline. If a change requires adding another large block to the frame loop, first
look for a controller extraction point.

Common reasons to touch `main.zia`:

- Wiring a new subsystem object.
- Adding a new command dispatch branch after adding metadata to the command
  catalog.
- Adding a new per-frame controller pump.
- Adding startup/shutdown integration.

Avoid touching `main.zia` for:

- Source parsing rules.
- File mutation logic.
- UI widget construction details.
- Command metadata.
- Runtime binding policy.

## `app/`

The `app/` directory contains orchestration helpers that sit near the frame loop
but do not own the core document model or persistent widget tree.

### `app/build_info.zia`

Reads build metadata written beside the generated IDE binary. The Help/About
surface uses this so users can identify the build they are running. This file is
the right place for build-info parsing, not for general version policy.

### `app/context_menus.zia`

Builds and updates editor/explorer context menu state. It bridges the current
document, selection, language service, debug session, and explorer selection
into enabled/disabled menu entries.

Touch this file when adding right-click behavior. Do not put command
implementation here; dispatch should still route to `commands/` or a controller.

### `app/dispatch_helpers.zia`

Contains small helpers for command triggering and disabled-state checks shared
by the main dispatch loop. This keeps repetitive menu/toolbar/palette checks out
of individual command modules.

### `app/command_palette_controller.zia`

State machine for command-palette modes. It restores command mode, opens the
command palette, and tracks when the palette temporarily contains Quick Open
file rows.

### `app/explorer_action_runner.zia`

Coordinates explorer actions that need multiple subsystems: document manager,
tabs, project manager, session manager, and AppShell. It is a deliberate bridge
between UI action requests and safe document/project mutations.

This is the right place to keep explorer workflows visible and auditable. File
creation, rename, duplicate, delete, and "open and record" behavior should stay
centralized here instead of being spread across the frame loop.

### `app/file_watch_controller.zia`

Owns active-file watcher state and timestamp polling for inactive open tabs.
`main.zia` calls its `Pump` method instead of carrying watcher variables inline.

### `app/settings_applier.zia`

Applies persisted settings to the shell and editor. Settings parsing belongs in
`core/settings.zia`; settings application belongs here when it requires widgets
or editor state.

### `app/workspace_watcher.zia`

Tracks workspace-level change notifications. It complements document-level file
watching in the main loop and should remain focused on detecting project-tree
changes, not mutating documents directly.

## `build/`

The `build/` directory owns process-backed build/run/debug state and diagnostic
models.

### `build/build_system.zia`

Owns build and run child processes. It resolves the `zanna` executable, starts
`Zanna.System.Process` jobs with argv sequences, streams stdout/stderr, bounds
retained output, parses structured JSON diagnostics, and falls back to legacy
diagnostic text parsing.

Important behavior:

- The IDE never runs build/run commands through a shell.
- `ZANNA_BINARY`, colocated binaries, the build tree, `ZANNA_BUILD_DIR`, and
  `PATH` are searched for `zanna`.
- Output is bounded to avoid uncontrolled memory growth.
- Diagnostics are kept as structured `Diagnostic` objects for Problems/output
  navigation.

Touch this file for process lifecycle, compiler resolution, output retention, or
diagnostic parsing. Do not put UI row formatting here; that belongs in command
or shell code.

### `build/run_config.zia`

Builds a `RunConfig` from the active document and project manager. It parses
`zanna.project` build/run overrides into argv tokens and expands `{target}`.

This file is the authority for:

- Current-file versus project-entry build/run.
- Custom build/run program and args.
- Working-directory selection.
- Problem matcher selection.

### `build/diagnostic.zia`

Small diagnostic data model shared by build, Problems, and output navigation.
Use this instead of display strings when a feature needs severity, path, line,
column, code, and message.

### `build/breakpoints.zia`

Stores persisted breakpoint records: path, line, condition, and log message.
Also owns breakpoint gutter marker constants. Debug command code and the debug
session both depend on this store.

### `build/debug_session.zia`

Controls the external VM debug adapter process. It launches:

```text
zanna run --debug-adapter <file>
```

The IDE writes newline JSON commands to stdin and reads sentinel-prefixed JSON
events from stderr. Program stdout is kept as program output. The session tracks
stopped/running/terminated state, current stop path and line, locals, call
stack, evaluation results, persistent watches, program output, and debug console
text. Restart requests are queued and launched only after the old adapter
process terminates.

Touch this file for debug protocol transport and session state. UI presentation
belongs in `commands/debug_commands.zia`, `ui/app_shell.zia`, or debug-specific
overlays.

## `commands/`

The `commands/` directory is where user-triggered behavior lives.

### `commands/command_catalog.zia`

Declarative command metadata: id, label, description, shortcut, capability, and
palette visibility. Add command ids here first. Command ids are stable API
inside the app; changing them can break shortcuts, probes, and dispatch.

### `commands/command_registry.zia`

Builds runtime command entries from the catalog, installs shortcuts, populates
the command palette, detects duplicate shortcuts, and computes language-service
availability. It does not implement feature behavior.

### `commands/file_commands.zia`

File and tab workflows: new file, open file, open folder, add folder, save,
save as, save all, close, reload, recent files, reopen closed files, and
project/file selection flows.

Use this file for user-facing file commands. Keep low-level document save logic
in `core/document_manager.zia`.

### `commands/edit_commands.zia`

Editor and semantic editing commands: navigation, references, call hierarchy,
rename, diagnostics navigation, code actions, multi-cursor, comments, duplicate
line, and move line.

Transform-heavy edit flows delegate to `source_transform_commands.zia` so the
basic editor command surface does not keep growing.

### `commands/basic_workspace_commands.zia`

UI-side owner for project-wide BASIC definition, references, incoming/outgoing
calls, and rename. It snapshots roots and open BASIC buffers, schedules the
owned background job, rejects stale editor/workspace anchors, navigates or
applies edits on the UI thread, and publishes large result sets in bounded
per-frame batches. Keep GUI/document mutation here and scanner/file work in the
worker module below.

### `commands/zia_workspace_commands.zia`

UI-side owner for Zia definition, references, incoming/outgoing calls, and
rename. It waits for cooperative project indexing without blocking the event
loop, submits immutable editor/index anchors to the owned worker, rejects stale
or incomplete results, navigates and applies edits on the UI thread, and
publishes at most 100 result items per frame. Keep project parsing in
`editor/zia_workspace_query_job.zia` and GUI/document mutation here.

### `commands/source_transform_commands.zia`

User-facing orchestration for source transforms: organize binds, inline local,
extract local, extract function, trim trailing whitespace, format document, and
format selection. It collects document/editor context, calls pure transforms in
`zia/`, replaces the buffer, marks documents modified, and reports status.

### `commands/diagnostic_edit_commands.zia`

Diagnostic-driven source edits: suppress warning, apply diagnostic fix-it, and
create missing runtime/project binds. `DiagnosticActionController` starts the
compiler query without editing, validates the captured path/revision/caret on
later frames, and delegates project-wide candidate discovery to the bounded
worker in `editor/project_bind_query_job.zia`. Disk candidates are mtime-checked
and unsaved candidates are content-checked immediately before insertion.
`edit_commands.zia` keeps synchronous compatibility wrappers for probes and
older callers; the product dispatcher uses the controller.

### `commands/editor_document_edit.zia`

Low-level editor replacement primitive shared by command modules that replace a
whole buffer. Use this instead of rebuilding the same full-document selection
range in multiple commands.

### `commands/search_commands.zia`

Project search, folder search, workspace symbols, and related result
navigation. This module coordinates runtime-paged file discovery, search
options, result rows, and location ids.

Search result display strings should never be the only source of navigation
truth. Use `services/locations.zia` for stable location ids.

The interactive path is the docked Search panel owned by `ui/app_shell.zia` and
started from `SearchController`. `commands/search_prompt.zia` remains as a
legacy request helper; direct, cached, and panel paths should converge on
`services/search_request.zia`.

Text search is bounded by both file count and aggregate source bytes per frame.
The controller records the first/last `LocationStore` ids published by each run;
Replace All must use that interval rather than every historical `KIND_SEARCH`
record in the shared location store.

### `commands/replace_commands.zia`

Pure line replacement helpers plus `ProjectReplaceController`, the cooperative
owner for Search-panel Replace All. It groups the current search generation in
one linear pass, performs no file I/O in the button callback, and applies no
more than two file plans per later frame. Closed-file reads are bounded and
revalidated before atomic writes; open buffers are modified in memory with an
inactive-tab workspace undo snapshot. Dense lines that exceed the match ceiling
cancel the entire file rather than writing a partial transform.

`commands/quick_open_commands.zia` owns Quick Open palette rows, command id
encoding, deterministic file scoring, and opening the selected file. Keep Quick
Open behavior there so text search remains focused on matching and navigation.

### `commands/quick_open_commands.zia`

Quick Open workflow for project files. It warms the project file cache, scores
relative paths against the query, populates command-palette rows, translates
palette ids back into paths, and opens selected files through the normal file
command path.

This module intentionally shares project file enumeration with search through
`core/project_manager.zia`, but it should not grow text-search options or result
formatting. Search and Quick Open are separate user workflows even though they
both consume workspace paths.

### `commands/build_commands.zia`

User-facing build/run command behavior. It performs save preflight, starts
build/run configs, updates running jobs, populates output rows, and publishes
diagnostics to the shell.

Process mechanics belong in `build/build_system.zia`; command workflow and
status messages belong here.

### `services/diff_engine.zia`

Pure line alignment and summary counting for side-by-side diffs. The Myers
search has explicit combined-line, edit-depth, and trace-cell ceilings;
`BuildRowsCapped` counts the complete aligned result while allocating only a
caller-sized row prefix. Keep GUI widgets and async ownership out of this
module.

### `services/diff_job.zia`

Latest-wins owned worker for bounded diff computation. Requests contain only
immutable title/text values, reject sources above 4 MB each or 20,000 combined
lines, retain at most 4,000 aligned rows, and support a bounded shutdown drain.

### `ui/diff_view.zia`

Reusable side-by-side overlay shared by Compare with Saved and Source Control.
It queues `DiffJob` in `Open`, shows pending state immediately, and turns worker
rows into at most 100 colored list items per frame. `Shutdown` is mandatory
before application teardown so no promise outlives the runtime.

### `commands/debug_commands.zia`

User-facing debug commands: start, continue, step, pause, stop, restart, run to
cursor, breakpoint toggles, breakpoint metadata, evaluation, and UI publishing
from `DebugSession` state.

### `commands/view_commands.zia`

Workbench display commands: sidebar/status bar/theme/fullscreen/minimap,
font zoom, UI zoom, settings, keybindings, tool-row copy, output copy/clear,
output filter/wrap/autoscroll, recent files, and About.

## `core/`

The `core/` directory owns persistent IDE domain state.

### `core/document.zia`

The per-document record. It stores file path, display name, language, document
kind, text content, modified/new/read-only flags, disk metadata, cursor/scroll
state, and external disk-state/conflict fields.

Do not add editor-widget references here. `Document` is model state, not UI.

### `core/document_manager.zia`

Owns the open document list and active document index. Responsibilities:

- Create untitled documents.
- Open files with deduplication.
- Detect language and document kind.
- Load text or read-only preview content.
- Save active documents.
- Save As.
- Save All.
- Close documents and maintain active index.
- Track recently closed paths.
- Classify external disk changes, including changed, deleted, renamed, and
  missing files.
- Use `Zanna.Workspace.Edit` for safe existing-file replacement.

If a feature needs to mutate open documents, prefer going through this module or
a service that clearly documents how document state is updated.

### `core/project.zia`

Parses `zanna.project` manifest data: project name, language, entry, build/run
programs, build/run args, working directory, problem matcher, and ignore rules.

Keep manifest parsing here. Runtime execution of those values belongs in
`build/run_config.zia`.

### `core/project_manager.zia`

Owns the primary project, additional workspace roots, explorer tree, tree-node
path data, Quick Open cache, incremental directory loading, ignore checks, and
project file cache refresh.

It uses `Zanna.Workspace.FileIndex` for exclusion rules and paged project-file
cache refresh. Immediate Explorer children use `Zanna.IO.Dir.Page`; collection,
natural sorting, and node publication remain separate cooperative stages. Do not
duplicate ignore logic or recursive enumeration elsewhere unless the runtime
surface cannot answer the question.

### `core/project_file_ops.zia`

Small filesystem policy helpers for project/explorer flows: safe child names,
project-root containment checks, `.zia` file classification, and reversible
delete trash destinations.

### `core/settings.zia`

Persistent settings manager. It handles defaults, platform-native settings
paths, legacy settings-path compatibility, validation on load, and read-modify-
write persistence so unrelated INI sections survive. Shared state reads are
size-bounded and successful writes use the atomic persistence helper in
`services/safe_io.zia`.

Settings are parsed here but applied elsewhere. Keep widget calls out of this
file except for theme routing that is explicitly part of settings behavior.

### `core/session.zia`

Session and recent history persistence. It stores project root, open files,
active file, active index, cursor/scroll, bounded recovery text for modified
text buffers, recent projects, and recent files. `BeginRestore` / `PumpRestore`
provide caller-budgeted root and tab restoration; main polls and repaints the
startup card between entries instead of opening an entire large session on one
unresponsive UI frame. `Restore` remains the blocking compatibility wrapper.
Save mirrors the restore record limits, caps aggregate embedded recovery, and
prioritizes the active document when a large tab set must be truncated.

Recovery text is intentionally capped and base64 encoded. This module should
remain conservative because it runs during startup/shutdown and protects user
work.

### `services/safe_io.zia`

Non-trapping filesystem boundary helpers. Shared Studio INI state has a global
size ceiling and is committed through a same-directory staging file plus atomic
replace, so settings, sessions, and breakpoints share one corruption-resistant
policy.

### `core/recovery_store.zia`

Continuous per-document crash swaps and the unclean-session lock. Interactive
requests contain immutable path/text snapshots and run through one owned async
worker plus one coalesced latest request. Large writes use ignored staging names;
only cancellation-serialized atomic renames publish canonical `.path`/`.swp`
pairs. Save, reload, close, rename, and delete reconciliation cancels older
commits, prunes stale pairs, and queues remaining dirty buffers.

Startup discovery uses `BeginRecoveryScan` / `PumpRecoveryScan` to page the flat
recovery directory, remove abandoned staging files, and retain recovered text
under fixed record and aggregate-byte caps. Main polls and paints between scan
entries and between recovered tabs.

## `editor/`

The `editor/` directory adapts `Zanna.GUI.CodeEditor` and language services into
IDE behavior.

### `editor/editor_engine.zia`

The adapter between `Document` and `CodeEditor`. It loads documents into the
editor, saves editor state back to documents, sets language/display options,
manages current file path/language, and caches full-text snapshots by editor
revision.

Use `GetTextSnapshot()` instead of reading full editor text repeatedly.

### `editor/editor_tabs.zia`

Owns tab widget behavior and tab metadata such as modified markers, tooltips,
active tab, and close signals. Document content belongs in `DocumentManager`;
visual tab state belongs here.

### `editor/language_service.zia`

Capability matrix for Zia, BASIC, text, and scene documents. The command
registry and semantic controllers use it to decide what should run.

This file is the first place to update when adding a language capability.

### `editor/scheduler.zia`

Priority and debounce model for editor background work. It names semantic job
kinds, default delays, and scheduling state. Use it to keep typing responsive
when adding more background work.

### `editor/basic_query_job.zia`

Serial latest-wins worker used by compiler-backed BASIC completion,
diagnostics, hover, symbols/folding, and signature queries. Requests retain an
immutable active-buffer snapshot and cancelled generations never publish.

### `editor/basic_workspace_query_job.zia`

Bounded project-wide BASIC semantic worker. It pages every captured workspace
root, lets captured open-buffer text override disk, limits files/per-file and
aggregate source/declarations/results, checks cancellation between units, and
returns plain navigation or rename records. It never borrows GUI,
`ProjectManager`, `DocumentManager`, or editor objects from its worker thread.

### `editor/project_bind_query_job.zia`

Serial latest-wins worker for Create Missing Bind. It pages every captured
workspace root, lets captured open Zia buffers shadow disk, and requires a
complete scan with exactly one defining file before publishing. File-count,
per-file size, line-count, and aggregate-source ceilings turn uncertain
uniqueness into an explicit refusal; the request retains the candidate mtime or
open-source snapshot for final UI-thread stale validation.

### `editor/zia_workspace_query_job.zia`

Serial latest-wins worker for Zia project-index navigation and rename. Requests
retain the index handle, active-source snapshot, caret, document revision,
workspace signature, and index content generation. Definition, references,
incoming calls, and rename use the runtime index snapshot; outgoing calls scan
a bounded token set and resolve each target on the worker. Results cap at 2,000
references, superseded generations cannot publish, and shutdown drains the
owned future before controller teardown.

### `editor/completion.zia`

Completion controller. It handles automatic and explicit triggers, query
snapshots, async result tracking, popup population, filtering, acceptance,
commit characters, snippet cursor placement, and workspace-symbol completion
cache updates. Workspace-symbol discovery consumes the project manager's
runtime-paged file cache incrementally; do not reset the symbol cache merely
because the known file count grew.

The file is large. Pure filtering helpers have already been split into
`completion_filter.zia`, typed rows into `completion_items.zia`, and workspace
source/detail policy into `completion_workspace_source.zia`. New reusable
completion-display logic should move into helpers instead of growing the
controller.

### `editor/completion_items.zia`

Typed completion candidate model and constructors. Use this when adding fields
that must travel together with a completion row, such as insert text, replacement
range, cursor offset, or commit characters.

### `editor/completion_filter.zia`

Pure completion filtering and display helpers. This module is small and easy to
probe. Prefer adding deterministic string/ranking helpers here rather than
inside `completion.zia`.

### `editor/completion_workspace_source.zia`

Leaf helpers for completion's workspace-symbol cache: project-open checks,
cache-reset decisions, open-document-first source loading, file-size caps, and
workspace detail text.

### `editor/diagnostics.zia`

Live diagnostics controller. It schedules structured toolchain checks, rejects
stale results, updates Problems, tracks active-file inline highlights, and
publishes diagnostic actions.

### `editor/quick_fixes.zia`

Helpers for interpreting diagnostic records into quick-fix actions, missing
runtime bind names, undefined identifier extraction, and known runtime bind
lines. This is the right place for deterministic diagnostic-to-action mapping.

### `editor/hover.zia`

Dwell-based hover controller. It schedules hover lookups, rejects stale results,
formats hover display text, and positions tooltips.

### `editor/signature.zia`

Signature-help controller. It detects call-site context, schedules structured
signature lookups, formats active overloads, and implements overload navigation.

### `editor/symbols.zia`

Document symbol outline and semantic fold-region support. It updates the
outline panel and derives fold regions for Zia symbols when appropriate.

### `editor/project_index.zia`

Lazy Zia project index integration. It owns the `Zanna.Zia.ProjectIndex` handle,
syncs open documents, indexes workspace files cooperatively, enforces size and
per-frame byte limits, deduplicates overlapping roots, and exposes an explicit
complete/incomplete state. Workspace queries are refused after unreadable or
oversized input, more than 20,000 Zia files, more than 64 MB of source, or a
truncated runtime file enumeration. The runtime handle supports concurrent
updates and immutable query snapshots.

Changes here can affect responsiveness and semantic correctness. Keep file-size
caps, stale-data handling, and dirty-open-document behavior explicit.

### `editor/semantic_tokens.zia`

Semantic highlighting controller. It maps runtime token kinds to CodeEditor
highlight categories and applies semantic spans after debounce.

### `editor/inlay_hints.zia`

Conservative inlay hints. The current implementation uses IDE-side heuristics
for parameter names and inferred local types. It deliberately caps scanned lines
and hint count.

### `editor/zia_query.zia`

Helpers for wrapping source snippets before sending them to Zia runtime queries.
Use this when a semantic feature needs a parseable module wrapper around partial
editor text.

### `editor/perf_monitor.zia`

Optional perf logging for dogfood and probes. It records frame/controller/editor
counters when enabled by environment configuration.

### `editor/workbench_state.zia`

Small active-workbench-state model. Use it when command enablement or context
needs a structured snapshot rather than reading widgets ad hoc.

## `services/`

Services are leaf modules with shared rules and no ownership of AppShell.

### `services/file_utils.zia`

Extension-based language and document-kind detection plus open/save dialog
filters. This is the source of truth for `.zia`, `.bas`, `.vb`, `.scene`,
`.level`, text, IL, and unsupported binary detection.

### `services/locations.zia`

Structured location id storage for search, diagnostics, build output,
definitions, references, scene locations, and symbols. This prevents UI rows
from becoming the navigation contract.

When adding a clickable result type, add a structured location kind here rather
than parsing display text.

### `services/text_utils.zia`

Small deterministic text helpers: trailing CR stripping, leading whitespace,
right-trim, string-list joining, and string-list membership.

### `services/search_matcher.zia`

Pure project-search line matching. It owns literal matching, whole-word checks,
and the compact editor-search regex subset used by Search in Project.

### `services/search_paths.zia`

Search path normalization and include/exclude/ignore matching. Use it when a
feature needs to decide whether a project path belongs in search results.

### `services/workspace_file_index.zia`

Shared `Zanna.Workspace.FileIndex` boundary. Use it for workspace containment,
default excludes, ignore checks, and file enumeration instead of duplicating
recursive directory or map-unpacking rules in command/core modules.

### `services/workspace_edits.zia`

Preview, validation, conflict detection, application, and diagnostics formatting
for workspace edit sequences. Rename and refactor commands depend on this to
avoid partial or conflicting application. Scanner-backed edits may attach the
source text and disk mtime observed during the background scan; validation
rejects changed buffers/files before applying any edit.

## `basic/`

### `basic/semantic_scan.zia`

Lightweight IDE-only BASIC declaration/reference/call/signature scanner. The
capped declaration, reference, and call-token entry points are the worker-safe
boundary for large projects. This scanner is deliberately not presented as the
compiler's full semantic model.

## `terminal/`

### `terminal/terminal_session.zia`

Thin wrapper around `Zanna.System.Pty.PtySession`. It starts a child, drains
bounded output through `ReadResult()`, writes input, resizes, tracks exit,
stores the latest startup error, reports runtime/frame truncation in-band, and
destroys the PTY on stop.

### `terminal/terminal_controller.zia`

UI-side integrated terminal controller. It wires the shell-owned OutputPane,
sets terminal mode, lazily starts a shell when visible, resolves the platform
shell, derives rows/columns from OutputPane cell metrics, appends output,
forwards captured input, drains already-running hidden sessions into a bounded
replay buffer, handles Stop and Restart, and updates the working directory for
future sessions.

Do not treat this as a full terminal emulator. It is a PTY-backed shell surface
for line-oriented interactive work, not a full-screen TUI host.

## `scm/`

### `scm/scm_git.zia`

Async Git command layer over `Zanna.System.Process`. It resolves `git`, starts
argv-sequence jobs in the repository root, captures stdout/stderr/exit code,
parses porcelain v2 status, stages/unstages files, commits, diffs, pushes,
pulls, lists branches, and switches branches.

Known constraints are documented in [status.md](status.md) and
[runtime-integration.md](runtime-integration.md). Blocking wrapper methods are
kept for probes and older call sites, but UI code should start and pump
`GitJob` objects.

### `scm/scm_view.zia`

Source Control view model and UI action state. It owns the current Git snapshot,
selected path, diff text, commit message, active job, and refresh/operation
behavior needed by AppShell.

### `scm/scm_gutter_controller.zia`

Owns the editor's asynchronous Git diff job and change-bar projection. Requests
coalesce across edits/tab switches, resolve the repository from the document's
owning workspace root, reject stale completions, time out slow children, and
bound marker publication.

## `ui/`

The `ui/` directory owns persistent widgets, overlays, and display helpers.

### `ui/app_shell.zia`

Constructs the workbench: window, menu bar, toolbar, sidebar, explorer,
activity bar, editor container, tab bar, breadcrumb, find bar, bottom panels,
debug panels, terminal, Source Control widgets, preferences, overlays, status
bar, and helper methods for showing/hiding/updating those widgets. It arbitrates
the single focus-taking transient-surface slot used by Settings, About, explorer
actions, breakpoint input, command input, and diff views. Frame rendering also
reconciles the requested minimap with the measured editor-lane width so compact
windows reclaim its space without losing the persisted preference.

This file is also too large. Add new UI state here only when it truly belongs to
the persistent shell. For complex new surfaces, prefer a dedicated controller or
view module that AppShell wires.

### `ui/activity_bar.zia`

Activity bar state and view ids. Currently covers Explorer, Search, Source
Control, Run/Debug, Extensions placeholder, Problems, Outline, and related view
selection behavior.

### `ui/debug_breakpoint_overlay.zia`

Overlay for breakpoint metadata entry: conditions and logpoints. It owns the
temporary input UI and action result consumed by debug command code.

### `ui/explorer_actions.zia`

Overlay for explorer create/rename/duplicate/delete workflows. It collects user
input and reports action intent; actual filesystem/document mutation happens in
`app/explorer_action_runner.zia`.

### `ui/command_input.zia`

Reusable non-modal single-value command input overlay. It is used by Go To Line,
Add Watch, output filtering, Workspace Symbols, Rename Symbol, Extract Local,
and Extract Function; command modules consume the completed value and perform
validation/effects.

### `ui/ide_overlays.zia`

Shared overlay helpers used by command palette or workbench popups.

### `ui/tool_panel_text.zia`

Formatting constants and helpers for Problems/Output/Search/References rows:
colors, column widths, severity labels, truncation, diagnostics summary, and
language display names.

### `ui/tool_panel_model.zia`

Named ids and tab indexes for the bottom tool-panel strip. Use this instead of
magic integers when adding bottom-panel behavior. It also owns the bounded
stable-row model used by ListBox-backed Problems, Search, References, Debug
Console, Variables, and Call Stack panels.

### `ui/output_cache.zia`

Pure output-cache policy for AppShell's build output: retained character
budget, truncation marker, raw-pane versus row-list decision, and wrap width
heuristic.

## `zia/`

Pure Zia source-analysis and rewrite helpers live here. These modules should
not own GUI widgets or document-manager state.

### `zia/identifier_utils.zia`

Identifier and keyword rules used by refactors and source scanning. Unicode
identifier support should start here if added later.

### `zia/source_scan.zia`

Compatibility facade for source scanners. Existing callers can keep importing
`source_scan`, while new code should prefer the focused modules below.

### `zia/function_scan.zia`

Function lookup helpers: source-line access, function-name parsing, enclosing
function detection, and function end-line matching.

### `zia/trivia_scan.zia`

Trivia-aware line scanning for comment depth, brace/paren depth, continuation
indent, whitespace skipping, and identifier replacement outside strings/comments.

### `zia/call_scan.zia`

Outgoing call-token collection for simple call hierarchy support.

### `zia/delimiter_scan.zia`

Inline delimiter and block brace matching used by selection expansion.

### `zia/occurrence_scan.zia`

Whole-word next-occurrence search for multi-cursor commands.

### `zia/formatters.zia`

Zia and BASIC document/range formatting, whitespace trimming, indentation rules,
and line formatting helpers.

### `zia/bind_utils.zia`

Bind normalization and rewrite helpers. Explorer rename and missing-bind
commands use this kind of logic to keep source updates deterministic.

### `zia/refactors.zia`

Pure refactor transformations for inline local, extract local, and extract
function. The transforms are deliberately conservative and return unchanged
source when unsafe or unsupported.

## `probes/`

Probe files are focused executable checks. They are not examples for end-user
application structure; they are regression harnesses for IDE behavior.

Important probe groups:

- `smoke_probe.zia`: basic app smoke.
- `phase0_phase1_probe.zia`: core document/project/command/session behavior.
- `phase2_phase3_probe.zia`: build/run/debug boundary and scene data contracts.
- `editor_hot_path_probe.zia`: editor performance and copy/layout/index guards.
- `intellisense_probe.zia`: completion, diagnostics, hover, signature behavior.
- `console_search_probe.zia`: output/search/Quick Open helpers.
- `debug_probe.zia`: VM debug adapter integration.
- `terminal_*`: PTY terminal behavior and rendering.
- `scm_probe.zia`: Git Source Control behavior.
- `scm_gutter_probe.zia`: bounded rendering plus asynchronous/stale-safe gutter
  diffs in a real temporary multi-root Git workspace.

When adding a feature, add or extend a focused probe near the smallest surface
that can verify it. Do not rely only on a full IDE smoke test.

## `tests/`

`zannastudio/src/tests/` contains local GUI/runtime probe sources and small
experiments. CTest registration for IDE probes lives in the repository-level
`src/tests/CMakeLists.txt`.

## Where To Put New Code

Use this practical decision table:

| Change | Preferred location |
| --- | --- |
| New command id or shortcut | `commands/command_catalog.zia` |
| Command availability | `commands/command_registry.zia`, `editor/language_service.zia` |
| File open/save behavior | `core/document_manager.zia` |
| New document metadata | `core/document.zia` |
| Project manifest parsing | `core/project.zia` |
| Explorer tree/cache behavior | `core/project_manager.zia` |
| Editor widget synchronization | `editor/editor_engine.zia` |
| Zia semantic query scheduling | appropriate `editor/*` controller |
| Zia project navigation/refactor commands | `commands/zia_workspace_commands.zia` + `editor/zia_workspace_query_job.zia` |
| BASIC project semantic commands | `commands/basic_workspace_commands.zia` + `editor/basic_workspace_query_job.zia` |
| Pure source scanning | focused `zia/*_scan.zia` module; keep `zia/source_scan.zia` as facade |
| Search matching/path/file discovery rules | `services/search_matcher.zia`, `services/search_paths.zia`, `services/workspace_file_index.zia` |
| Quick Open palette/scoring | `commands/quick_open_commands.zia` |
| Completion row data | `editor/completion_items.zia` |
| Completion workspace source loading | `editor/completion_workspace_source.zia` |
| Pure source rewrite | `zia/refactors.zia` or `zia/bind_utils.zia` |
| Source-transform command flow | `commands/source_transform_commands.zia` |
| Diagnostic quick-fix command flow | `commands/diagnostic_edit_commands.zia` |
| Text formatting | `zia/formatters.zia` |
| Build/run process mechanics | `build/build_system.zia` |
| Build/run user command flow | `commands/build_commands.zia` |
| Debug transport/session state | `build/debug_session.zia` |
| Debug command behavior | `commands/debug_commands.zia` |
| Terminal PTY wrapper | `terminal/terminal_session.zia` |
| Terminal UI behavior | `terminal/terminal_controller.zia` |
| Git command execution | `scm/scm_git.zia` |
| Source Control view state | `scm/scm_view.zia` |
| Persistent shell widgets | `ui/app_shell.zia` |
| Build-output cache/wrap policy | `ui/output_cache.zia` |
| Shared display row formatting | `ui/tool_panel_text.zia` |
| Bottom panel ids/tab indexes | `ui/tool_panel_model.zia` |
| Structured clickable locations | `services/locations.zia` |
| Transactional workspace edits | `services/workspace_edits.zia` |
| Runtime API change | C runtime + `runtime.def` + docs/probes |

## Current Pressure Points

The source map should also make current weak spots explicit:

- `main.zia` and `ui/app_shell.zia` are oversized coordinator files.
- `commands/edit_commands.zia`, `commands/search_commands.zia`, and
  `editor/completion.zia` are also large and should be split when adding
  cohesive new behavior.
- Bottom panels are functional but not true virtualized workbench surfaces.
- Scene document kind exists without a scene editor subsystem.
- Source Control is async and marks basic conflicts but still workflow-light.
- Terminal behavior depends on OutputPane terminal mode, not a complete terminal
  emulator.
- BASIC support is intentionally narrower than Zia.

New code should reduce these pressure points when it naturally can, but should
not perform unrelated refactors that make feature verification harder.
