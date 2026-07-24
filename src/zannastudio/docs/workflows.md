# Zanna Studio Workflows

This document covers how to use and develop Zanna Studio as it exists today.

Zanna Studio is frame-driven and project-oriented. Most workflows begin with a
project folder, a set of open documents, and the active editor. Commands are
available through menus, toolbar buttons, the command palette, shortcuts, and
context menus. When a command is not valid for the active language or current
state, the IDE should either disable it or report an unavailable reason.

The workflows below are written from the user's point of view, but they also
name the implementation boundaries that matter when debugging a broken flow.

## Launching

From the repository root:

```sh
zanna run src/zannastudio/
```

From inside `src/zannastudio/`:

```sh
zanna run .
```

After building the native IDE binary:

```sh
./scripts/build_ide.sh
./src/zannastudio/bin/zannastudio
```

On Windows:

```powershell
.\scripts\build_ide_win.ps1
src\zannastudio\bin\zannastudio.exe
```

## Opening Projects

Use "Open Folder" to choose a project root. Use "Add Folder to Workspace" for
additional roots. The first opened folder is the primary project root used by
build/run defaults, session restore, Source Control, and workspace indexing.

The project explorer populates one directory level at a time. Quick Open and
workspace search build their full file cache cooperatively so opening a large
workspace does not require a full recursive tree build up front.

When a project opens, Zanna Studio does not immediately index every source file for
every semantic feature. It establishes the tree and caches enough state for
interactive work, then semantic indexing and Quick Open/search caches progress
cooperatively. This is why a freshly opened large workspace can show a tree
quickly while deeper semantic commands still warm up.

If the project has a `zanna.project` file, Zanna Studio reads project name, entry,
language, build/run overrides, working directory, problem matcher, and ignore
patterns. If there is no manifest, the folder still opens and defaults are used.

## Opening Files

Open files through:

- File > Open.
- Explorer selection.
- Quick Open.
- Search result navigation.
- Diagnostics/output/reference result navigation.
- Recent Files.
- Reopen Closed File.

Opening the same file twice activates the existing tab instead of creating a
duplicate document.

Zanna Studio tracks one `Document` object per open path. The editor widget displays
the active document, but the document manager owns the buffer state. Switching
tabs writes cursor/scroll/text state back to the outgoing document and loads the
incoming document into the editor. This is why file commands should go through
the document manager rather than setting editor text directly.

## File Kinds

| Extensions | IDE kind | Behavior |
| --- | --- | --- |
| `.zia` | code | Zia editor services. |
| `.bas`, `.vb` | code | BASIC editor services. |
| `.txt`, `.md`, `.json`, `.il` | text | Plain text editing. |
| `.scene`, `.level` | 2D scene | Built-in 2D hierarchy, viewport, properties, history, and import tools. |
| `.vscn` | 3D scene | Built-in 3D hierarchy, viewport, transform/material, history, and import tools. |
| image extensions | binary unsupported | Read-only preview placeholder. |
| unknown | text | Plain text editing. |

## Editing

Core editing behavior includes:

- Undo and redo.
- Cut, copy, paste, and select all for the active text or visual-scene surface.
- Multi-cursor commands.
- Add next occurrence, skip occurrence, select all occurrences.
- Clear extra cursors.
- Expand and shrink selection.
- Toggle line comment.
- Toggle block comment where the language supports it.
- Duplicate line.
- Move line up/down.
- Find/replace in the active file.
- Word wrap toggle.
- Line numbers toggle.
- Minimap toggle.
- Format document and format selection where a formatter exists.
- Trim trailing whitespace command and optional save-time trimming.

The editor keeps a revision number. Semantic controllers use that revision to
avoid applying results to the wrong buffer after the user types. If completion,
diagnostics, hover, or signature help appears stale, the likely bug is missing
path/revision/cursor validation around an async or delayed result.

## Zia Coding Workflow

Common Zia commands:

- Trigger Completion: `Ctrl+Space`.
- Trigger Signature Help: `Ctrl+Shift+Space`.
- Go to Definition: `F12`.
- Find References: `Shift+F12`.
- Rename Symbol: `F2`.
- Workspace Symbols: `Ctrl+T`.
- Symbol Outline: `Ctrl+Shift+O`.
- Run Check Now: `Ctrl+Alt+C`.
- Apply Diagnostic Fix-It: `Ctrl+.`.
- Organize Binds: `Shift+Alt+O`.

Zia semantic results are revision-gated. Stale completion, diagnostics, hover,
signature, and symbol work is rejected when the buffer changes before a result
lands.

Suppress Warning, Apply Diagnostic Fix-It, and Create Missing Bind also return
to the event loop before compiler work finishes. Keep the originating tab and
caret stable while they run. Project-bind discovery scans captured workspace
roots on a bounded worker and inserts only when exactly one candidate remains;
ambiguous, unreadable, oversized, or subsequently changed candidates are left
untouched with a status explanation.

Zia workspace navigation and rename depend on `Zanna.Zia.ProjectIndex`, but the
commands return to the event loop before project parsing begins. While the lazy
index warms up, the status bar shows queued progress; the command starts only
after a complete index exists. Moving the caret, editing, switching tabs or
roots, or changing indexed content cancels the delayed result. References and
call rows render in batches of at most 100 per frame.

The Zia index accepts at most 20,000 files, 200 KB per file, and 64 MB of source.
Unreadable/oversized files or truncated discovery make it explicitly incomplete,
and project navigation/refactor commands refuse instead of returning partial
answers. Reference results cap at 2,000; rename refuses above that limit and
validates expected symbol text again when edits are applied.

The References toolbar filters by file/group heading, language, location,
marker, or preview text while Zia and BASIC results continue to arrive. A
matching heading shows all of that group; a matching row shows its heading and
that row. Filtered rows keep their source locations, so selecting one still
opens the original range. Copy All copies the complete unfiltered grouped
result, and Clear keeps References open with an explicit empty state.

BASIC workspace navigation uses Zanna Studio's in-process semantic scanner on
an owned background worker. It pages every workspace root, lets unsaved open
BASIC buffers override disk, and rejects results when the active path, revision,
caret, or root set changes before completion. File/source/declaration ceilings
bound the scan; reference and call results are limited to 1,000 rows and render
incrementally. Rename is canceled rather than applying a partial result set.

## BASIC Coding Workflow

BASIC files support completion, diagnostics, hover, document symbols, and
build/run. Semantic navigation commands such as definition, references, rename,
workspace symbols, call hierarchy, and signature help are available for BASIC.

## Search

Use project search for workspace-wide text queries. The command opens a docked
Search panel so the query and filters can stay visible while results update.
Supported options include:

- Literal or regex matching.
- Case-sensitive matching.
- Whole-word matching.
- Include path filter.
- Exclude path filter.
- Folder-scoped search from the explorer.

Search results are grouped by file and navigate through structured location ids
instead of parsing displayed `path:line` strings. Search processes at most 16
files and 2 MB of source per frame; the same 2 MB per-file ceiling applies to
unsaved open buffers as well as disk files.

Replace All is tied to the exact completed text-search location-id range and
the query/options that produced it. It is disabled while search is running,
after cancellation, when discovery/results were truncated, or after panel
options change without a new search. Starting replacement only snapshots file
paths and line numbers; subsequent frames apply at most two files each. Closed
files are reread under the search size ceiling and atomically replaced only if
their content and modification time remain stable. Open buffers remain unsaved,
receive a workspace-undo snapshot, and never overwrite their disk copies.
Stopping a running replacement preserves the explicitly reported files already
changed and leaves every later file untouched. Results are cleared afterward so
a stale row cannot be applied twice; run Search again to refresh them.

## Compare And Diff

Compare with Saved and Source Control history/worktree comparisons share one
side-by-side overlay. Opening it is non-blocking: a latest-wins worker computes
the alignment under 4 MB-per-side, 20,000-combined-line, edit-depth, and trace
allocation ceilings. The overlay then adds at most 100 of its maximum 4,000
retained rows per frame. Closing the overlay cancels unpublished work; opening a
new comparison supersedes the old request. Very large inputs show a concise
limit message instead of attempting a potentially unbounded diff.

## Workspace Layout

The primary sidebar contains Explorer, Source Control, and Run and Debug.
Drag its `Sidebar - drag` header to the stationary Left or Right target, or use
the adjacent arrow buttons. `Move Primary Side Bar Left/Right` provide the same
actions through the Command Palette. The complete live view tree and activity
rail move together; selection and the logical sidebar width are retained, and
the mirrored splitter position is persisted across restarts. Collapsing the
sidebar still leaves its activity rail reachable on the configured edge.

Problems/Output/Search/References/Variables/Call Stack/Debug Console/Terminal
can occupy simultaneous left, bottom, and right groups plus one in-window
floating group. Each attached group header moves its selected tool directly,
including into the floating group; Command Palette actions provide the same
destinations. Use `Float Primary Panel Group` or drag the primary group onto
the central Float target to move the complete group without rebuilding its
tools.

The floating header moves the window and shows the same stationary targets.
Release over a target card or narrow workbench edge to dock the whole group;
ordinary movement elsewhere simply places it. The lower-right handle resizes
the window. Its bounded position and size recover after viewport/UI-scale
changes and persist across restarts. Arrow buttons move only the selected
floating tool back to an attached group.

Tabs reorder within their group, and membership, order, floating bounds, active
split sizes, and the primary host are durable. Typed drop targets ensure that a
target for the other surface does not alter layout. Focused sidebar trees and
terminal/search/filter inputs are restored after a move rather than handing
typing back to the hidden editor.

`Reset Workspace Layout` restores the primary sidebar to the left, merges every
tool into the default bottom group, restores split sizes/tab order and one
editor pane, and does not clear panel content or active documents. Tool groups
return to the attached bottom workbench. The floating group is an in-window
workspace surface; native secondary OS windows and multi-monitor detachment are
not yet available.

## Build And Run

Build and run use `Zanna.System.Process` and argument vectors.

Default project behavior:

```text
zanna build --diagnostic-format=json <project-root>
zanna run --diagnostic-format=json <project-root>
```

Default single-file behavior:

```text
zanna build --diagnostic-format=json <active-file>
zanna run --diagnostic-format=json <active-file>
```

The IDE resolves the `zanna` binary in this order:

1. `ZANNA_BINARY`.
2. Binary next to the IDE executable.
3. Developer build tree relative to the IDE executable.
4. `ZANNA_BUILD_DIR`.
5. `PATH`.

Build/run output is streamed to the Output panel and retained in a bounded
buffer. Structured JSON diagnostics are preferred. Legacy text diagnostics are
parsed as a fallback.

The Output toolbar keeps its common operations in the panel:

- Filter updates visible rows as text is entered.
- Wrap reflows long lines to the effective dock width.
- Follow controls whether incoming output scrolls to the bottom.
- Copy uses the complete unfiltered captured output when available and the
  current generated report rows otherwise.
- Clear empties the output but leaves the panel open when invoked there.

The Problems toolbar provides a message/file/source/code filter plus
Errors/Warnings/Info toggles and visible/total counts. Selecting a diagnostic
navigates to its source. When that row advertises a safe action, Quick Fix opens
the same durable location and starts the existing asynchronous Zia diagnostic
action. Filtering rebuilds presentation rows without losing navigation or
action metadata. Unsupported non-Zia edits are reported rather than applied.
Both toolbars wrap and compact when the shared panel is docked at a narrow side.

Before launching build/debug flows, Zanna Studio can save modified files according
to settings. This is intentional: the compiler and debugger run on disk state,
while the editor may have unsaved memory state. If a build appears not to see an
edit, first check `saveAllBeforeBuild`, active document modified state, and
whether the file is untitled or a read-only preview.

## Project Manifest Overrides

Inside `zanna.project`, use:

```text
build-program zanna
build-args build "{target}"
run-program zanna
run-args run "{target}"
working-directory .
problem-matcher zanna
ignore build-output/
ignore *.tmp
```

Rules:

- `build-program` and `run-program` select the executable.
- `build-args` and `run-args` are parsed into argv tokens.
- Single and double quotes group spans.
- Backslash escapes are supported.
- `{target}` expands to the project root or active file.
- `working-directory` is resolved relative to the project root.
- Commands are not shell-expanded.

## Debugging

Start Debugging launches:

```text
zanna run --debug-adapter <active-file>
```

Supported commands:

- Start Debugging.
- Continue.
- Pause.
- Step Over.
- Step Into.
- Step Out.
- Run to Cursor.
- Restart Debugging.
- Stop Debugging.
- Toggle Breakpoint.
- Conditional Breakpoint.
- Add Logpoint.
- Evaluate expression while stopped.
- Add the evaluated expression to the watch section.

The activity rail includes a persisted **Run and Debug** view. It remains
available for a standalone saved Zia source file when no project is open.
Its primary action follows the live session state: Start while idle, Pause while
running, Continue while paused, and a disabled progress label while pausing,
stopping, or restarting. Restart, Stop, and step controls are enabled only when
the adapter can accept them. Variables, Call Stack, and Console buttons open the
corresponding dockable tool surface without changing debugger state.

The same view lists persisted source breakpoints. The live filter matches file,
path, line, condition, and logpoint text. Visible rows retain their original
store identity, so Open, Remove, Condition, and Logpoint never infer a source
location from formatted row text or filtered position. The concrete list
realizes at most 1,000 matches and reports the complete count. At narrow widths
or high UI zoom, wrapped action rows live in a vertical scroll viewport.

Debug output is split between:

- Program output.
- Debug console/control messages.
- Variables panel.
- Grouped watch and local rows in the Variables panel.
- Call Stack panel.
- Current-line gutter marker.

Call Stack reports the current idle, paused, running, pausing, stopping,
restarting, or terminated state when frames are unavailable. Its toolbar
filters frames and copies the complete unfiltered stack. Filtering preserves
each frame's original adapter index, so opening a visible result navigates to
the correct source frame rather than the row's filtered position.

Debug Console exposes live text filtering, wrap control, complete-source copy,
and program-output Clear. Clear removes retained stdout/stderr from the debug
session and keeps the current debugger status visible, so cleared output does
not reappear when the next adapter event arrives. Filtering and wrapping do not
mutate session output.

Limitations:

- Lists, sequences, maps, and class instances expand asynchronously; boxed
  struct payloads and direct-IL/BASIC objects without a layout sidecar remain
  typed leaves.
- The Variables panel provides inline watch entry plus Add, Remove, Refresh,
  and Clear actions. Add Watch from the command palette focuses the same field.
- Breakpoint condition/logpoint values are edited in a focused single-field
  overlay rather than a full details inspector.

## Terminal

Toggle Terminal starts the integrated terminal panel. The first visible pump
starts the configured shell:

- Windows: `%COMSPEC%` when valid, otherwise `cmd.exe`.
- POSIX: `$SHELL` when valid, otherwise `/bin/sh`.

POSIX shells receive `-i`. The terminal working directory is the active project
root for newly started sessions. If the workspace changes while a terminal is
already running, the terminal keeps its current child process and shows a note
that restart is needed to use the new directory.

Supported terminal operations:

- Type directly in the terminal pane.
- Stop.
- Restart.
- Resize with the panel.
- Clear-screen shell redraws that use common `CSI J` sequences.
- Cursor-position shell redraws that use common `CSI H/f` sequences.

Limitations:

- Not a full terminal emulator.
- Full-screen TUIs that require complete alternate-screen or terminal-mode
  semantics are out of scope.
- Width/height are estimated from widget pixels, not font metrics.

## Source Control

The Source Control view operates on the primary project root when it is a Git
repository.

Supported operations:

- Refresh status.
- View current branch.
- Stage or unstage the selected file; Stage All only advertises itself while
  worktree changes remain.
- Commit staged changes after entering a message; focused Enter submits.
- View diff for selected file.
- View paged commit history, changed files, and parent-to-commit comparisons.
- Push.
- Pull.
- Switch branch.
- Answer detected username/password/passphrase prompts in the focused in-app
  credential row; focused Enter sends the response.
- Resolve a basic unmerged file by selecting it, editing its conflict markers,
  and staging it. The diff pane explains this path, and Commit remains disabled
  while any unmerged row remains.

The action rows wrap inside narrow sidebars. Stage, Unstage, side-by-side diff,
Stage All, Commit, and Cancel update from live selection, index, message,
conflict, and job state rather than advertising operations that cannot run.

Limitations:

- Commands run asynchronously, but the view processes one active Git job at a
  time.
- Push and pull can be long-running. Output and detected credential prompts are
  visible, but prompt detection is heuristic rather than a Git protocol.
- Common paths with spaces, staged renames, and a real content-conflict
  edit/Stage/commit path are covered; exotic path bytes, rename conflicts, and
  multi-file recovery need more coverage.
- Merge/rebase orchestration, ours/theirs review, stash, and merge abort are not
  provided.

## Scene Authoring

Files ending in `.scene` or `.level` open in the 2D scene editor. Files ending
in `.vscn` open in the 3D scene editor. Both surfaces write every accepted edit
back to the active `Document.content`, so ordinary dirty-tab, Save, Save As,
session, recovery, and external-change rules remain authoritative.

The standard Edit menu and Ctrl/Command keybindings follow the active surface.
Select All selects all authored 2D objects or editable 3D hierarchy nodes.
Copy places a versioned `2d` or `3d` scene envelope on the text clipboard; Cut
copies first and removes the selection as one undoable transaction. Paste
accepts only the matching scene kind, works between same-kind tabs, selects the
new content, and creates one undo entry. The envelope is capped at 64 MB and
1,024 identities. Malformed, wrong-kind, stale, oversized, or partly
unreconstructable data is rejected without changing the destination.

The 2D toolbar switches among Select, Paint, Erase, and Object modes. Paint and
erase interpolate missed cells during a fast pointer stroke. Select mode drags
objects on the tile grid, and one complete stroke or object drag creates one
undo entry. After the canvas has been clicked in Select mode, Arrow keys nudge
the complete selection by one authored pixel and Shift+Arrow uses one tile on
the relevant axis. Inspector fields and the Objects list keep their native
arrow behavior. Middle-drag pans and the wheel zooms.

The Objects list supports Ctrl/Command-click to add or remove rows and
Shift-click to select a range. Dragging any selected object in Select mode moves
the complete group on the tile grid while preserving its offsets. Duplicate
copies every selected object, including typed properties, and Remove deletes
the selection; each batch is one undo entry and restores the exact prior
content and selection on undo. Reserved object fields and draw-order buttons
remain single-selection operations. With several objects selected, Object
properties becomes a deliberate batch editor: Set writes one typed key/value
to every row and Remove removes the entered key wherever present. The complete
batch is verified and committed once; invalid values or rejected writes restore
the prior content and selection.

The Object layout row aligns every selected X or Y coordinate to the primary
object. Distribute X/Y requires at least three objects, keeps the two extrema,
and places intermediate authored point positions at deterministic rounded
intervals. Each accepted arrangement is one undo entry; choosing a layout the
selection already satisfies is a history no-op.

Cross-document 2D paste assigns unique object IDs, offsets each object by one
destination tile, and preserves null, Boolean, integer, floating-point, and
string properties.

Each layer can reference a PNG, JPEG, BMP, or GIF tileset image. Relative paths
resolve beside a saved `.scene`; untitled-scene relative paths resolve from the
working directory until the scene has a path. The inspector shows a scrollable
eight-column palette for the first 512 atlas frames. Clicking a frame selects
tile ID `frame + 1`, switches to Paint, and the canvas composites real atlas
frames across visible layers. The numeric tile field remains available for
larger atlases.

Tileset references are external and are not embedded in `.scene`. Choose Image
and Clear Image each create one canonical undo entry when the reference changes;
choosing the same image is a no-op. Invalid candidates leave content and history
untouched. Project Layer Images opens a non-modal searchable view of supported
images from every open workspace root. It incrementally reuses the project file
index, realizes at most 512 matching rows, previews one bounded image, and only
changes the scene after Use Image is requested. A native picker remains
available for files outside the workspace.

Studio checks one referenced layer image per 250 ms polling opportunity.
Metadata changes quietly reread the pixels and redraw the canvas without
dirtying the document or adding undo history. Reload Image remains available
for an explicit reread, including same-size changes that a coarse filesystem
timestamp may not distinguish. Studio accepts sources up to 16 MB, decodes at
most 4,194,304 pixels per image and 8,388,608 pixels across the scene, and
applies the same
8,388,608-pixel bound to scaled preview caches. Missing, invalid, over-budget,
or out-of-range frames use a deterministic placeholder and report their state
in the layer inspector.

The 3D hierarchy also supports Ctrl/Command-click and Shift-click
multi-selection. The primary row owns the visible Move, Rotate, or Scale
handles; they follow that node's parent-aware local transform projection.
Dragging applies the same axis delta to each selected node's local transform,
so relative local offsets are preserved for Move and each node rotates or
scales around its own local origin. The optional Snap toggle quantizes the
resulting primary value—not each pointer frame—to 0.5 units, 15 degrees, or 0.1
scale for the active tool. A drag captures the pointer, updates the inspector
live, commits one VSCN history snapshot on release, and restores every origin
when Escape cancels it. Tool and snap selection belong to the scene tab and
survive session restore. With the viewport or one of its transform-tool buttons
focused, W selects Move, E selects Rotate, and R selects Scale. Those keys
remain ordinary input while an inspector, editor, terminal, or other workbench
control owns focus. Middle- or right-drag orbits; the wheel zooms; Frame All and
Frame Selected reposition the view around the whole selection.

For a multi-node selection, the numeric inspector fields switch to neutral
relative values: position and rotation start at zero and are added to every
node's local transform; scale starts at one and multiplies every local scale.
Apply deltas commits the complete group once and resets the fields to neutral.

The Parent row reparents existing nodes rather than requiring hierarchy
recreation. Its chooser omits every selected root and descendant, rejects moves
that would exceed Studio's bounded hierarchy depth, and disables Reparent for
the unchanged current parent. With several rows selected, selected descendants
travel through their selected ancestor and independent top-level roots move
together in one VSCN transaction. Local transforms remain unchanged, selection
is remapped by live node identity after preorder changes, and undo restores the
exact prior hierarchy.

Duplicate preserves each selected top-level node's complete serializable
subtree, components, and original parent, gives the clone a unique Copy name,
and offsets its local X position. Delete and Duplicate ignore a selected
descendant when its ancestor is also selected, preventing double removal or
duplicate nested clones. Both are one history transaction. Name, visibility,
and material controls remain single-selection operations. Duplicate Selection
uses `Ctrl`/`Cmd`+`Shift`+`D`; Delete uses the platform Delete key. Both
keyboard routes require hierarchy or viewport/tool focus, so Delete remains
native in inspector text fields and neither command escapes material controls.

Cross-document 3D paste applies the same selected-ancestor collapse as
Duplicate, places the pasted roots beside the primary destination node, assigns
unique root names, offsets local X by one unit, and restores selection across
the complete pasted hierarchy.

The 3D inspector's Material component edits base RGB, alpha, metallic,
roughness, ambient occlusion, opaque/mask/blend mode, double-sided, and unlit
state. Create Material adds a PBR material to a node without one. When an
imported material is shared, the action becomes Apply as Unique: Studio clones
the material before changing exposed fields, preserving sibling appearance plus
unexposed texture maps and custom parameters. Applying, removing, undoing, and
redoing are canonical VSCN transactions; unchanged values do not create a
history entry. Keyboard input in the color picker or another inspector control
does not activate W/E/R transform shortcuts.

The Texture maps group selects Albedo, Normal, Metallic / Roughness, Ambient
Occlusion, or Emissive independently. Choose Map accepts PNG, JPEG, BMP, GIF,
or strictly validated KTX2 files up to 16 MB and embeds the selected source in
the VSCN document. Raster maps above 16,777,216 decoded pixels are rejected
before they can be retained by scene history. Project Texture Maps searches the
open multi-root workspace and previews supported images before Use Map; the
hierarchy's Project 3D Imports browser does the same bounded search for VSCN,
glTF, GLB, FBX, OBJ, and STL assets. Replacing or clearing a map first clones a
shared material and creates exactly one undo entry; clearing an already-empty
slot is a no-op. The map selector and buttons retain keyboard focus, so W/E/R
remain ordinary control input rather than changing the transform tool.

The selected assigned slot shows a persistent bounded thumbnail and its source
dimensions. The preview is derived from the live material's decoded pixels, not
the picker path, so it follows VSCN load, undo/redo, import, clone, and
cross-document scene operations. Switching slots reuses the thumbnail while the
material and decoded source identity are unchanged.

Both editors retain invalid source bytes unchanged and explain the parse/load
failure instead of replacing the document. Current authoring limits include no
tagged asset library/import-settings pipeline, cubemap/lightmap picker,
generalized component composition surface,
mixed-value scalar/material editing and batch component composition, advanced
Tiled margin/spacing or
image-collection editing, tile animation/collision/metadata editing, or 3D
local/world and plane/ring gizmo controls. Hierarchy reparenting currently uses
the explicit Parent chooser and preserves local transforms; drag-to-reparent,
sibling reordering, and an optional preserve-world mode are not yet present.

## Settings

Settings are stored in `settings.ini` in the platform config directory. Current
settings include:

- Editor font size and optional font path.
- Theme.
- Minimap visibility.
- Live diagnostics.
- Code folding.
- Status bar visibility.
- Fullscreen.
- Word wrap.
- Line numbers.
- Insert spaces.
- Trim trailing whitespace.
- Ensure final newline.
- Auto-save.
- Save all before build/debug.
- Restore session.
- Restore last project.
- Confirm on exit with unsaved changes.
- Diagnostics delay.
- Completion delay.
- Tab width.
- UI zoom.

Unknown INI sections are preserved when settings are saved.

The settings file is deliberately human-editable. If the UI gets into a bad
state, users can close the IDE and edit or remove the relevant key. New settings
should keep this property: simple values, clear defaults, validation on load,
and compatibility with older files.

## Session Restore And Recovery

On shutdown, Zanna Studio stores:

- Last project root.
- Open tabs.
- Active tab.
- Cursor and scroll state.
- Recent projects.
- Recent files.
- Bounded recovery content for modified editable text buffers.

Recovery content is capped at 200000 characters per document. Read-only previews
and oversized modified buffers are not persisted as recovery text.

## Troubleshooting

### Build says it cannot find `zanna`

Set `ZANNA_BINARY` to the full path of the desired `zanna` executable or add the
binary to `PATH`.

### Completion or diagnostics feel stale

Save or wait for the editor debounce interval, then run "Run Check Now". For
project navigation, allow workspace indexing to finish or narrow the workspace.

### BASIC semantic commands feel incomplete

BASIC definition, references, rename, workspace symbol, call hierarchy, and
signature help are scanner-backed. Project navigation now runs in the background,
but ambiguous/dynamic code can still need compiler-only semantic context. A
status ending in `(limited)` means the explicit workspace/result safety budget
was reached; narrow the workspace. Rename never applies in that state.

### A scene opens in the wrong surface

Use `.scene` or `.level` for the 2D editor and `.vscn` for the 3D editor.
Unknown scene-like extensions intentionally fall back to the text editor because
Studio cannot safely infer a serialized format from file contents alone.

### Terminal is unavailable

The runtime PTY layer may be unavailable on the platform or unable to launch the
configured shell. The terminal panel reports `Zanna.System.Pty.LastError()` when
startup fails.

### Source Control actions appear to hang

Source Control commands run as background `Zanna.System.Process` jobs, but the
view only pumps one active Git job at a time. Local status, stage, unstage,
commit, and diff are expected to be quick. Push and pull can still wait on
network, authentication, or remote state and currently expose only basic
status/error text.

### A command is visible but unavailable

Some commands stay visible in the command palette so users can discover that the
feature exists for another language or state. The command registry should report
a status/toast reason. For example, Rename Symbol is available for Zia but not
for BASIC.

### A scene file does not show visual tools

Confirm the extension is `.scene`, `.level`, or `.vscn`. Unknown scene-like
extensions intentionally remain text documents because Studio cannot safely
infer their serialization format. If a recognized file opens the visual
surface but shows a repair message, the original bytes failed bounded scene
parsing and remain unchanged; repair the source or create a new scene.
