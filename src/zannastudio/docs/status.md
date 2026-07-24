# Zanna Studio Current Status

Last reviewed against source: 2026-07-23.

This file is the current-state reference for Zanna Studio. It intentionally avoids
future-phase language and records limitations in the same place as shipped
behavior.

## Summary

Zanna Studio is usable as a code editor and project workbench for Zia projects, with
partial BASIC support, build/run integration, a VM-backed debugger path, an
integrated terminal, and lightweight Git operations.

It is not yet a polished product-complete IDE. The largest current gaps are:

- Bottom tool surfaces now have dockable responsive shells and bounded
  stable-row models. Problems, Output, References, Debug Console, Variables,
  and Call Stack have contextual in-panel controls, while the concrete result
  surfaces remain listbox/output-pane based rather than fully virtualized
  workbench views.
- The primary Explorer/Source Control/Run-and-Debug sidebar now moves as one
  live, persisted left/right dock with its activity rail, direct drag targets,
  mirrored width, collapse recovery, and Command Palette actions.
- Source Control covers status, staging, commit, paged history, per-commit
  diffs, queued jobs, in-app credential prompts, and a guided edit-then-Stage
  conflict path, but is still not a full Git client (no merge/rebase
  orchestration or ours/theirs recovery tools).
- BASIC semantic navigation and rename are implemented by the IDE-side scanner,
  not by the Zia project index or external BASIC server.
- Routine command text entry and rename review are non-modal. Native file
  pickers and destructive/external-state confirmations remain modal.
- Reinvoking project search or build/run while work is active reveals its
  Search or Output panel and Stop control without interrupting the operation.
- Run and Debug is a persisted activity-sidebar view with truthful session
  controls, direct inspection-panel links, and filterable breakpoint actions.
- The application source still has several oversized coordinator modules.

## Current Product Narrative

The most accurate way to describe Zanna Studio today is "a functional Zanna
workbench whose strongest path is Zia code editing." The editor can open real
projects, keep multiple files alive across sessions, run semantic services, and
drive the compiler/debugger toolchain. A Zia developer can use it for daily
editing in a small or medium project, especially when the workflow is edit,
search, build, run, diagnose, and debug one active program.

The roughness shows up when the workflow starts to look like a mature IDE. Some
secondary panels are still row-oriented instead of rich work surfaces.
Problems, Output, References, Debug Console, and Call Stack now expose filtering
and relevant actions in context; common short inputs, project search, settings,
Quick Open, and multi-file rename review also use integrated workbench surfaces.
The debug substrate is real: a state-aware Run and Debug activity view,
persistent watches, state-aware call frames, a clearable/filterable console,
and structured expansion of collections and class instances (field-by-field,
via the compile-time layout sidecar of ADR 0138).
BASIC support is intentionally honest but incomplete.
Source Control covers daily local Git plus commit history, queued operations,
live push/pull output, in-app credential prompts, and safe basic conflict
resolution guidance, but it does not have the merge/rebase workflow depth or
conflict recovery tools of a full client. Scene
documents mount built-in 2D and 3D visual editors with hierarchy/layer,
viewport, property, history, and import workflows, but those tools remain
earlier and narrower than a mature game-engine editor.

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

For a game developer, `.scene`/`.level` and `.vscn` documents mount built-in
2D and 3D authoring surfaces. They cover a useful first hierarchy, viewport,
property, undo/redo, save, and import workflow. The 2D editor resolves layer
tileset images, renders their real frames in the canvas, and exposes a bounded
clickable palette. Both inspectors expose bounded, searchable project-asset
choosers backed by the existing multi-root workspace index. The 3D inspector
authors a compact PBR material component and
embeds common albedo, normal, metallic/roughness, ambient-occlusion, and
emissive maps without mutating shared imported materials. Both hierarchy lists
support Ctrl/Command-click and Shift-click multi-selection with stable,
non-display row identities. Batch drag/transform, duplicate, and delete actions
commit as one undoable transaction. The 2D inspector can set/remove one typed
property across the selection, and the 3D numeric inspector applies explicit
relative position/rotation/scale batches. The 2D Select tool also owns
focus-safe one-pixel or one-tile keyboard nudging plus primary-axis alignment
and stable distribution commands. The 3D Parent chooser reparents selected
top-level subtrees in one cycle-safe transaction while preserving their local
transforms and remapping selection after hierarchy order changes. A full asset
library with tagging/import settings and advanced tileset metadata/animation
tools is still missing. Persistent assigned-map thumbnails are present, but
shaded material previews, generalized components, and advanced cubemap/lightmap
workflows remain well short of a mature game-engine editor.

The standard Edit menu and keybindings are surface-aware: Cut, Copy, Paste,
Select All, and Duplicate Selection operate on scene objects or hierarchy
nodes when a visual scene owns the active tab; scene-only Delete is available
as well. Duplicate/Delete keyboard dispatch requires hierarchy or viewport
focus, leaving inspector text editing native. Copy uses a typed text envelope
rather than guessing from scene syntax. Same-kind paste works across scene
tabs, preserves 2D typed properties
or complete selected 3D subtrees and resources, offsets the new content, and
commits exactly one undoable transaction. Wrong-kind, malformed, oversized, or
unreconstructable clipboard data leaves the destination byte-exact.

For a user expecting a polished workbench, the rough areas are mostly around
large-result panel density, advanced Source Control recovery workflows, deeper
scene authoring, and panel virtualization for very large result sets.

## Feature Matrix

| Area | Status | Notes |
| --- | --- | --- |
| Text editing | Implemented | Multi-tab CodeEditor, undo/redo, selections, comments, formatting, folding, minimap option. |
| Split editor | Implemented (v1) | Two side-by-side panes ("Split Editor Right", "Focus Other Editor Pane", "Close Editor Split", click-to-focus). Each pane owns a distinct document and the focused pane drives the active tab, typing, IntelliSense, find, minimap, status, save, and recovery state. Opening a document already visible in the other pane focuses its existing owner, preventing divergent buffers and stale overwrites. Split-active state is restored when at least two documents are open. v1 limits: exactly two panes; open a second document before splitting; same-document multi-view awaits shared-buffer runtime support. |
| Workspace layout | Implemented with limits | The primary Explorer/Source Control/Run-and-Debug sidebar has a draggable header, stationary left/right targets, explicit arrows and Command Palette actions; its activity rail, logical width, visibility, active view, and position persist together. Problems/Output/Search/References/Variables/Call Stack/Debug/Terminal can form simultaneous left/bottom/right groups plus one movable/resizable in-window floating group: each header exposes direct selected-tool movement, the Command Palette exposes the same actions, and the primary group can drag/merge/float as a unit. Floating bounds recover across viewport/UI-scale changes and membership/order/attached sizes/floating geometry replay through migration-safe settings. Explicit cards and narrow edge strips make redocking deliberate, while Reset Workspace Layout restores coherent attached defaults. Native secondary-window and multi-monitor detachment are not implemented. |
| Zia IntelliSense | Implemented with limits | Completion, diagnostics, hover, signature help, symbols, definition, references, rename, workspace symbols. |
| BASIC IntelliSense | Implemented with limits | Completion, diagnostics, hover, document symbols, scanner-backed definition, references, rename, workspace symbols, call hierarchy, and signature help. |
| Plain text | Implemented | Opens unknown/text-like files as text without semantic features. |
| Scene files | Implemented with limits | `.scene`/`.level` mount the 2D scene editor and `.vscn` mounts the 3D editor. Both retain per-document selection/camera/history state, participate in save/session flows, and provide hierarchy, viewport, properties, creation, deletion, undo/redo, and import-oriented tools. Their retained hierarchy rows expose stable item data for Ctrl/Command and Shift multi-selection; group drag/transform, duplicate, and delete actions are one-step history transactions. The 2D inspector batch-sets/removes typed properties, while its Select surface supports focus-scoped one-pixel/one-tile nudging, primary-axis alignment, and deterministic distribution; the 3D inspector batch-adds local position/rotation and multiplies local scale, and its Parent chooser reparents selected top-level subtrees without cycles or local-transform changes. Searchable project-asset choosers reuse Studio's bounded multi-root file index for Tiled maps, layer images, 3D models, and texture maps; native file pickers remain available. The 2D viewport supports grid object dragging plus real PNG/JPEG/BMP/GIF layer-atlas rendering and a clickable first-512-frame palette; relative image paths resolve beside a saved scene and externally changed images refresh quietly without dirtying history. The 3D viewport provides parent-aware Move, Rotate, and Scale axis tools with optional 0.5-unit, 15-degree, and 0.1-scale snapping. Its material component inspector edits base color, alpha, metallic, roughness, ambient occlusion, alpha mode, double-sided, and unlit state, and embeds selectable albedo, normal, metallic/roughness, ambient-occlusion, and emissive maps from PNG, JPEG, BMP, GIF, or strict KTX2 sources up to 16 MB; decoded raster maps are capped at 16,777,216 pixels. Scalar edits and map replacement/clearing are clone-safe one-step VSCN history transactions. Focus-scoped W/E/R and scene Duplicate/Delete shortcuts do not intercept typing elsewhere in the workbench or material controls. |
| Scene clipboard | Implemented with limits | Standard Cut/Copy/Paste/Select All commands follow the active visual editor. A versioned, typed text envelope supports same-kind cross-tab transfer of up to 1,024 selected identities and 64 MB total, preserving typed 2D properties or serializable 3D subtrees. Cut and paste are one-step history transactions with exact rollback. Mixed 2D/3D paste and interchange with other editors are intentionally rejected. |
| Project explorer | Implemented with limits | Demand-loaded, scrollable tree; multi-root support; Quick Open cache; file actions; ignores. Rename/move preserve live editor buffers and undo state, while delete releases any removed split-pane owner. |
| Search | Implemented | Docked project/folder search panel with a compact-window minimum results viewport, runtime-paged file discovery, per-frame file/byte budgets, literal/regex, case/word filters, include/exclude filters, grouped results, and generation-scoped frame-sliced Replace All with bounded atomic closed-file writes. Search/Replace completion remains in the panel and status bar instead of interrupting later work with a popup. |
| Build/run | Implemented | Argument-vector jobs, project manifest overrides, streamed bounded output, JSON diagnostics, and durable completion in Output/Problems/status without duplicate background toasts. |
| Tool panels | Implemented with limits | Problems/Output/Search/References/Variables/Call Stack/Debug/Terminal retain their live controllers while moving among independent left/bottom/right groups or the in-window floating group, support group-local movable tab order, and use responsive controls. Problems has live text/severity filters, counts, durable navigation metadata, and selection-aware Quick Fix. Output has live filter/wrap/follow/copy/clear controls. References has grouped live filtering, visible/total counts, durable click-to-open locations, Copy All, Clear, and contextual empty states. Debug Console has filter/wrap/copy/clear controls and Call Stack has filter/copy controls plus live debugger-state empty text. In-panel Clear retains the visible Output, References, or Debug Console surface. Result widgets remain bounded ListBox/OutputPane surfaces rather than fully virtualized views. |
| Debugging | Implemented with UX gaps | External VM debug adapter, breakpoints, stepping, pause, async restart, run to cursor, locals, call stack, evaluate, conditions, and logpoints. The persisted Run and Debug activity view exposes state-valid Start/Continue/Pause/Restart/Stop/step actions, links to Variables/Call Stack/Console, and durable filterable breakpoint Open/Remove/Condition/Logpoint actions. The docked Variables surface has inline Add/Remove/Refresh/Clear watch controls, Call Stack preserves frame identity through filtering, and Debug Console separates clearable program output from retained session status. Lists/seqs/maps plus class-instance fields expand with value previews. |
| Terminal | Implemented with limits | PTY-backed shell in OutputPane terminal mode: alternate screen, DECSTBM scroll regions, IL/DL/ICH/DCH/ECH, tab stops, cursor visibility, bracketed paste, application cursor keys, DSR/DA replies, SGR 16/256/truecolor + reverse. Coverage is pinned to the vim/less/htop sequence table, not full VT. |
| Source Control | Implemented with limits | Responsive, selection-aware Git controls for async status, stage/unstage, commit, per-path diff, worker-computed/incrementally rendered side-by-side diffs, paged commit history with per-commit files and diffs, queued serialized jobs, PTY-backed push/pull with live output and focused in-app credential prompts. Real unmerged rows show edit-then-Stage guidance and block Commit until resolved. No merge/rebase orchestration or ours/theirs recovery tools. |
| Settings | Implemented | Platform config path, theme, editor behavior, auto-save, save-before-build, session options, settings search, rebindable keyboard shortcuts, and debounced primary-sidebar/tool-dock position and split sizing. The body is vertically scrollable with a fixed action footer; compact windows give Preferences the full workbench lane and stack descriptions above controls without horizontal overflow. |
| Session restore | Implemented | Project, tabs, cursor/scroll, recent files/projects, bounded recovery text, and painted caller-budgeted startup restoration. |
| File watching | Implemented with limits | Active file watcher, inactive document polling, missing/deleted/moved-file conflict state, capped recursive workspace watcher set with fallback scans, and quiet metadata-polled refresh of external 2D layer images. |
| Visual polish | Implemented with limits | Zanna-brand palettes (WCAG-gated), scalable vector icons across toolbar/tree/tabs/status, smooth scrolling, gamma-correct text with ligatures, and viewport-bounded welcome/About/Preferences/diff, command, and semantic popup surfaces. Focus-taking Settings, About, explorer, breakpoint, command-input, and diff surfaces are mutually exclusive with popup menus, preventing stacked panels and ambiguous Enter/Escape routing. Build/Search notifications follow a contextual policy: their durable background results stay quiet, while immediate failures may warn; routine success remains status text. Chrome text, floating overlays, wrapped output, and responsive tool tabs share one effective-scale coordinate space without applying user zoom twice. Long list rows—including compact Recent paths—use explicit ellipsis and expose their complete unmodified text on hover instead of ending at a hard clip. The native workbench minimum starts at 720 by 520 and grows with whole-UI zoom, contracting against a desktop-chrome safety margin when the display cannot fit that floor. A requested minimap is temporarily suppressed below a useful editor-lane width and restored automatically when the lane expands, without overwriting the user's preference. Remaining density work is tracked per panel. |
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

`.scene` and `.level` files open in the built-in 2D scene editor; `.vscn` files
open in the built-in 3D scene editor. Each open scene owns its selection,
viewport/camera, inspector, and undo/redo workspace state independently, while
document dirty/save/session behavior remains integrated with ordinary tabs.

The visual editors provide hierarchy/layer navigation, viewport selection and
camera controls, property editing, object creation/deletion/duplication,
history, and import/export-oriented file workflows. A 2D object drag and a 3D
transform drag each become one undo entry.

The object and node lists support Ctrl/Command-click for additive selection and
Shift-click for ranges. Selection is recovered from byte-exact row data rather
than display labels, so duplicate or renamed labels do not redirect a batch
operation. In 2D, dragging any selected object moves the group while preserving
offsets; duplicate and remove apply to the complete selection and preserve
typed properties. While the Select button owns keyboard focus, Arrow keys move
the selection by one pixel and Shift+Arrow moves it by one tile; inspector and
hierarchy controls keep their native arrow behavior. The layout row aligns X
or Y to the primary object and distributes three or more objects by stable
coordinate order, with fixed extrema and deterministic integer rounding.
In 3D, Move/Rotate/Scale apply the same axis delta to every
selected node's local transform, Frame Selected bounds the group, and
duplicate/delete collapse selected descendants beneath a selected ancestor so
a subtree is processed exactly once. Each accepted group action is one history
entry with exact rollback. The Parent row applies the same selected-root
collapse rule when moving existing nodes, filters cycle- and depth-invalid
destinations, preserves each moved root's local transform, and remaps the
complete selection after preorder indices change. Reserved 2D object
identity/draw-order and 3D name/visibility/material controls remain
single-selection; batch-capable property, transform, and parent controls state
their group semantics explicitly.

Edit > Cut, Copy, Paste, and Select All and their standard keybindings target
the active scene instead of the hidden text editor. The scene clipboard is an
explicit `2d` or `3d` envelope capped at 64 MB and 1,024 selected identities.
Paste accepts only a matching kind. A 2D paste reconstructs null, Boolean,
integer, floating-point, and string object properties with a unique ID and
one-tile offset. A 3D paste reconstructs each selected top-level subtree as a
sibling of the primary destination node, preserves its serializable components
and shared resources, assigns unique root names, and offsets local X by one
unit. Cut and paste each create one history entry; Copy and Select All do not.
Every rejected paste preserves the prior canonical content and selection.

Each 2D layer can reference a PNG, JPEG, BMP, or GIF atlas. Relative references
resolve beside the saved scene, the first 512 frames appear in a clickable
palette, and visible layers render their real atlas frames in the canvas.
Replacing or clearing a reference is one undoable transaction; unchanged or
rejected references do not alter history. Studio quietly checks one referenced
image per polling interval and refreshes changed pixels without dirtying the
scene; Reload Image remains available for an explicit reread. References remain
external rather than being embedded in `.scene`. The layer inspector and Tiled
import section can search supported files already inside any open workspace
root, with at most 512 realized matches and bounded image previews. Studio
bounds each source to 16 MB, each
decoded atlas to 4,194,304 pixels, and aggregate decoded/cached scene imagery to
8,388,608 pixels. Missing, invalid, over-budget, or out-of-range frames retain a
deterministic placeholder and contextual inspector status.

The 3D editor has distinct Move, Rotate, and Scale handles, mode-aware snapping,
pointer capture, Escape cancelation, per-scene tool persistence, group framing,
and W/E/R tool shortcuts that only activate while the viewport/tool owns
keyboard focus. The primary selected row owns the visible gizmo; dragging it
applies the resulting local-axis delta to every selected node and commits once.
The Parent chooser moves selected top-level roots under Scene Root or one valid
existing node. Destinations inside any moved subtree and destinations that
would exceed the scene depth limit are omitted; accepted moves preserve local
transforms, serialize once, and restore the same node selection at its new
preorder rows. It is an explicit chooser rather than drag-to-reparent and does
not yet offer sibling ordering or preserve-world conversion.
Its Material component edits base RGB, alpha, metallic, roughness, ambient
occlusion, opaque/mask/blend mode, double-sided, and unlit state. Applying an
edit clones the selected material first, so imported nodes that share a material
remain independent and unexposed maps/custom data are retained. The map
controls assign or clear albedo, normal, metallic/roughness, ambient-occlusion,
and emissive slots from PNG, JPEG, BMP, GIF, or strictly validated KTX2 sources
up to 16 MB. Decoded raster maps are capped at 16,777,216 pixels. The hierarchy
import and texture-map sections can search supported files from the same
bounded multi-root project index and preview supported images before use.
Map replacement also clones shared materials, and the image data is embedded in
VSCN. The selected assigned slot shows a bounded thumbnail from the canonical
live material, so load, history, import, clone, and round trips cannot leave a
stale picker-path preview behind. Scalar apply/remove and map replace/clear each
create one history transaction and round-trip through VSCN. They are
intentionally v1: a full tagged asset library/import-settings workflow,
generalized component composition, advanced Tiled atlas
metadata/image-collection editing, tile
animation/collision/metadata editing, cubemap/lightmap authoring,
mixed-value scalar/material editing, batch component composition,
local/world switching, hierarchy drag-to-reparent, explicit sibling ordering,
preserve-world reparenting, and
production-grade ring/plane gizmos still need depth.

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

Problems retains structured severity, source, code, action, and location data
independently of its realized rows. Its toolbar filters by text and severity,
reports visible/total counts, and enables Quick Fix only for an actionable
selection. Filtering and side-dock movement do not discard navigation or
action metadata. Diagnostic source edits currently use the Zia action
controller; unsupported language/action combinations report that limit without
editing.

Output exposes its text filter, wrapping, follow state, complete-text copy, and
clear action directly above the log. The controls wrap in side docks. Clearing
from the toolbar empties the log without collapsing the panel, and generated
reports replace stale captured-output copy state.

References retains group, row text, color, and location identity independently
of realized ListBox items. Its live filter includes every child when a group
heading matches, or only matching children with their heading otherwise.
Incremental Zia and BASIC workspace results honor an active filter without
repainting the whole result set. Copy All uses the complete unfiltered grouped
model, while Clear leaves an explicit empty References surface open.

Call Stack reports idle, paused, running, pausing, stopping, restarting, and
terminated session states instead of presenting stale frames or a generic empty
list. Its live filter rebuilds visible rows from durable frames while retaining
each adapter frame index, so selecting a filtered row still opens the correct
frame. Copy Stack copies the complete unfiltered stack.

Debug Console keeps program output separate from debugger status text. Its
responsive toolbar provides live filtering, wrapping, complete-text copy, and
Clear. Clear removes session-owned stdout/stderr so output does not reappear on
the next debugger event, but retains the current running, stopped, or terminated
status. Filtering and wrapping affect presentation only.

### Debugger

The debugger uses the external VM debug adapter:

```text
zanna run --debug-adapter <file>
```

Supported behavior:

- Launch active file.
- Open a persisted Run and Debug activity-sidebar view even without a project.
- Present Idle, Running, Paused, Pausing, Stopping, Restarting, and Terminated
  state with only valid primary, restart, stop, and step controls enabled.
- Open Variables, Call Stack, or Debug Console directly from the activity view.
- Set and persist breakpoints.
- Conditional breakpoints and logpoints.
- Filter breakpoints by file, path, line, condition, or log text while retaining
  exact store identity; Open, Remove, Condition, and Logpoint act on the selected
  source record. Concrete rows are capped at 1,000 while counts remain complete.
- Continue, pause, step over, step in, step out.
- Run to cursor.
- Stop and restart.
- Restart waits for the previous adapter process to terminate before launching
  a replacement.
- Current-line gutter marker.
- Locals and call stack at stop points.
- State-aware Call Stack filtering and complete-stack copy. Filtered rows retain
  their original adapter frame index for correct source navigation.
- Expression evaluation while stopped.
- Persistent watch expressions, shown in the Variables panel above locals.
- Inline watch entry plus Add, Remove selected, Refresh, and Clear controls in
  the docked Variables panel; Enter submits without leaving the workbench.
- Command-palette Add Watch routes to and focuses that same inline field.
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
- Debug console output with live filtering, wrapping, complete-text copy, and
  program-output Clear that retains debugger status.

Known debugger UX gaps:

- Boxed struct-typed fields display as typed leaves (struct payload expansion
  is a recorded follow-up); direct-IL and BASIC debug sessions have no layout
  sidecar and keep `<TypeName>` leaves for objects.
- Breakpoint condition/logpoint editing still uses a focused single-field
  overlay rather than a richer breakpoint-details inspector.

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
- Responsive wrapped action rows whose enabled states follow the repository,
  selected path, staged index, authored message, active job, and conflict state.
- Focused Enter submission for commit messages and Git credential responses.
- Basic conflict recovery: real porcelain-v2 unmerged rows are highlighted,
  explain "edit conflict markers, then Stage", and keep Commit disabled until
  every conflict is staged as resolved.
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
- Status parsing uses porcelain v2 and handles common spaces, renames, and real
  unmerged rows, but exotic path bytes and multi-file/rename conflict recovery
  still need more coverage.
- No merge, rebase, stash, ours/theirs selection, or merge-abort workflow.

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

- Recovery applies only to editable text buffers; a hard crash can still lose
  edits made after the most recent two-second debounce snapshot.
- Source Control write operations depend on Git command success and basic
  stderr reporting.

## Product Polish Gaps

These gaps are current documentation, not a plan commitment:

- Extend named vector icons when new workbench actions are added.
- Continue replacing routine confirmation-style workflows with contextual
  workbench surfaces while retaining explicit destructive safeguards.
- Bottom-group panels share one vertical splitter, while independent left and
  right groups and one in-window floating group can remain open simultaneously.
  Each attached boundary is drag-resizable; the floating group moves/resizes and
  redocks through explicit cards or narrow edge strips. Search temporarily
  reserves a useful compact-window result viewport without overwriting the
  bottom preference, side widths stay mirrored, and empty groups collapse
  independently. Group membership/order, floating bounds, and the primary
  draggable group's host persist alongside the independently movable primary
  sidebar. Remaining: native secondary-window/multi-monitor detachment and
  fully virtualized panel content beyond the bounded stable-row model.
- Add boxed struct-payload expansion in the debugger.
- Deepen Source Control with merge/rebase orchestration, ours/theirs review,
  merge-abort, and multi-file conflict recovery.
- Deepen the 2D/3D scene editors with richer asset/component workflows,
  asset tagging/import settings, advanced tileset animation/collision/metadata
  editing, shaded material previews, cubemap/lightmap authoring,
  mixed-value scalar/material editing, batch component composition,
  local/world switching, hierarchy drag-to-reparent, sibling ordering,
  preserve-world reparenting, and
  ring/plane transform handles.
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
- Say "built-in v1 2D/3D scene editors" while naming their advanced tileset,
  animation/collision/metadata, asset-library/material-preview/advanced-map,
  component, and gizmo limits.
- Say "debug adapter supports stepping, breakpoints, locals, call stack,
  evaluate, inline watch management, and structured expansion of collections
  and class-instance fields" while still mentioning the struct-payload leaf
  and direct-IL/BASIC layout-sidecar gaps.

The goal is to make the app feel more trustworthy by making the docs less
optimistic than the code.
