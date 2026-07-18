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
| `.scene`, `.level` | scene | Text editing today; scene kind tracked. |
| image extensions | binary unsupported | Read-only preview placeholder. |
| unknown | text | Plain text editing. |

## Editing

Core editing behavior includes:

- Undo and redo.
- Cut, copy, paste, select all.
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

Zia workspace navigation and rename depend on `Zanna.Zia.ProjectIndex`. The
index is built lazily and skips oversized source files. If a query happens while
the workspace index is warming up, results may be unavailable or incomplete.

BASIC workspace navigation uses Zanna Studio's in-process semantic scanner. It scans
project BASIC files and open BASIC buffers using the same workspace file-index
policy as search.

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
instead of parsing displayed `path:line` strings.

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

Debug output is split between:

- Program output.
- Debug console/control messages.
- Variables panel.
- Grouped watch and local rows in the Variables panel.
- Call Stack panel.
- Current-line gutter marker.

Limitations:

- Variables are grouped, but object values are not expandable.
- Watch add/remove/refresh/clear commands are command-palette based, not a
  dedicated watch panel.
- Debug session state is functional but still visually light.

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
- Stage file.
- Unstage file.
- Stage all.
- Commit staged changes.
- View diff for selected file.
- Push.
- Pull.
- Switch branch.

Limitations:

- Commands run asynchronously, but the view processes one active Git job at a
  time.
- Push and pull can be long-running and have no rich progress or credential UI.
- Common paths with spaces and staged renames are covered; exotic path bytes and
  complex conflict states need more coverage.
- Error reporting is basic.

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
signature help are scanner-backed. Results can be incomplete while cooperative
workspace scans are still warming up or when code needs compiler-only semantic
context.

### Scene file opens as text

This is expected. Zanna Studio tracks the scene document kind but does not currently
include a visual scene editor.

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

This is current behavior. The file kind is recognized so session/save/filter
paths are ready, but the scene editor surface has not been implemented.
