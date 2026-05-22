# Phase 0 - Foundations and Data Safety

## 1. Summary and Objective

Make ViperIDE safe to extend. This phase fixes the architecture and data contracts that every later feature depends on: command dispatch, document kinds, close/save safety, cursor/session persistence, structured locations, project-search navigation, and file-kind recognition.

This phase should be implemented as several small increments. Do not wait until the whole phase is finished to land safe, tested pieces.

## 2. Scope

In:

- Command registry and command-id consistency.
- `DocumentKind` and a surface-selection hook.
- Close prevention and unsaved-document sweeps for app close paths.
- Correct active-editor state capture, cursor/scroll persistence, and dirty flags.
- Structured session restore, recent projects, and last project root.
- Structured result locations for search, diagnostics, build output, and future references.
- Clickable project-search results using structured locations.
- File open/save filters and kind/language detection for code, text, JSON, scene, and level files.

Out:

- Scene editor UI.
- Project-wide semantic indexing.
- Async process execution.
- Debugger UI.

## 3. Feature Toggles

No global feature toggle. Add user settings only for behavior users may want to disable:

- `restoreSession`: default `1`.
- `restoreLastProject`: default `1`.
- `confirmOnExitWithUnsavedChanges`: default `1`; this should not be disabled until autosave/recovery exists.

## 4. Configuration and Persistence

Do not persist semicolon-joined paths or `path:line:col` strings.

Use `~/.viperide/settings.ini` with separate sections:

```ini
[settings]
restoreSession=1
restoreLastProject=1

[recent]
count=2
path.0=/abs/project
path.1=/abs/other

[session]
projectRoot=/abs/project
activePath=/abs/project/src/main.zia
count=2

[session.file.0]
path=/abs/project/src/main.zia
kind=code
cursorLine=12
cursorCol=5
scrollLine=1

[session.file.1]
path=/abs/project/levels/level1.scene
kind=scene
cursorLine=1
cursorCol=1
scrollLine=1
```

Use the existing `Viper.Text.Ini` pattern from `core/settings.zia`. Session restore must skip malformed sections and missing files without aborting startup.

## 5. Technical Requirements

### 5.1 Command Registry

Current state:

- Shortcuts are registered in `main.zia:123-148`.
- Command-palette entries are registered in `main.zia:153-180`.
- Commands dispatch through a long if-chain in `main.zia:375-455`.
- There is already id drift: the palette registers `"sidebar"` while dispatch listens for `"togglesidebar"`.

Change:

- Add `commands/command_registry.zia`.
- A command record has `id`, display label, shortcut, menu item, toolbar item, command-palette visibility, and trigger mode.
- Startup registers each command once, then uses the registry to populate shortcuts and command-palette entries.
- Per frame, `Dispatch(cpSelected) -> String` returns one command id or `""`.
- Main-loop command handling remains explicit and frame-driven: dispatch returns an id, then `main.zia` calls the existing handler with current `shell`, `docMgr`, `engine`, and related state.

Acceptance:

- The sidebar command uses one id everywhere.
- No command is reachable from a shortcut but missing from the palette unless explicitly hidden.
- Unknown ids are ignored defensively and logged/toasted only in debug builds.

### 5.2 DocumentKind and Surface Selection

Current state:

- `Document` has text fields only (`core/document.zia`).
- `DocumentManager.OpenFile` reads all file content and sets only `language`.
- The shell has one code-editor area; there is no non-text surface.

Change:

- Add document kind constants:
  - `KIND_CODE`
  - `KIND_TEXT`
  - `KIND_SCENE`
  - `KIND_BINARY_UNSUPPORTED`
- Add `kind` to `Document`.
- Add `services/file_utils.zia` helpers:
  - `DetectLanguage(path) -> String`
  - `DetectKind(path) -> Integer`
  - case-insensitive extension matching.
- Treat `.zia` as code/Zia, `.bas` and `.vb` as code/BASIC, `.json` and `.md` as text, `.scene` and `.level` as scene.
- Update open/save dialogs. The current open filter only includes `*.zia;*.bas;*.txt`; scene and JSON files must be openable.
- Add `AppShell.SelectSurface(kind)` as the single hook later filled by the scene editor. Phase 0 may map `KIND_SCENE` to a read-only/source fallback until Phase 5 lands.

Acceptance:

- Opening `a.scene`, `b.LEVEL`, `c.zia`, and `d.bas` assigns the expected kinds.
- Switching tabs calls the surface hook exactly once and does not lose the previous editor state.

### 5.3 Close, Save, and Dirty-State Safety

Current state:

- `handleClose` prompts for one dirty tab.
- `handleExit` destroys the shell directly.
- The main loop exits on `shell.ShouldClose()` without an unsaved sweep.
- Runtime exposes `Viper.GUI.App.SetPreventClose` and `WasCloseRequested`.

Change:

- Enable close prevention after shell creation.
- Add a single `ConfirmCloseDocuments(reason, target)` path used by tab close, File > Exit, OS close, project switch, and future close-all.
- Before any close/switch/save/build/run command, call the active surface's `SaveStateToDocument`.
- For code docs this wraps the existing editor-state save.
- For future scene docs this will flush scene state to the document's scene model, not raw text.
- Do not clear `isModified` until save succeeds and the document timestamp has been refreshed.
- If a document has changed on disk since it was loaded or saved, warn before overwriting.

Acceptance:

- Dirty documents block OS close and File > Exit.
- Save All saves every dirty non-untitled document.
- Untitled dirty documents route to Save As or remain open.
- Canceling any prompt leaves the app and all tabs unchanged.

### 5.4 Cursor, Scroll, and Active State

Current state:

- `EditorEngine.SaveToDocument` stores `lastCursorLine` and `lastCursorCol`.
- Those fields are initialized but not updated by the current ViperIDE code.

Change:

- Either update `lastCursorLine/lastCursorCol/scrollLine` every frame from the live editor, or make `SaveToDocument` read the live cursor/scroll directly.
- Track scroll position if a public editor API exists; otherwise add a small follow-up runtime binding before claiming scroll restore.
- Session restore and tab switching must use the same state path.

Acceptance:

- Move the cursor in tab A, switch to tab B, switch back: cursor returns to the same line/column.
- Relaunch restores the active file and cursor.

### 5.5 Structured Locations and Result Lists

Current state:

- Project search stores `filePath + ":" + lineNum` in list item data.
- Diagnostics and build output carry line numbers or display strings instead of file/line/column records.

Change:

- Add `services/locations.zia` with a `Location` record:
  - `id`
  - `filePath`
  - `line`
  - `column`
  - `endLine`
  - `endColumn`
  - `kind`
  - `message`
- Add `LocationStore`, an in-memory id-to-location table. ListBox item data stores only the numeric/string id.
- Use the same store for project search, diagnostics, build problems, references, and scene validation later.
- Add `OpenLocation(location)` to open the file, select the correct surface, and jump to the line/column if the surface supports it.

Acceptance:

- Project search click navigation works for paths containing colons, spaces, and punctuation.
- Diagnostic navigation can later open non-active files without changing the data contract again.

### 5.6 Project Search

Current state:

- Search scans `.zia` and `.bas` synchronously.
- It is substring-only and case-sensitive.
- Results are not clickable.

Change:

- Add a small search options dialog or command flow that captures query, case sensitivity, whole-word, and file kinds.
- For Phase 0 it may remain synchronous, but it must route results through `LocationStore`.
- Respect project excludes from `ProjectManager` as they exist today; richer ignore behavior is Phase 1.
- Add a click handler on `shell.outputListBox` that resolves a location id and calls `OpenLocation`.

Acceptance:

- Search results open the correct file and jump to the correct line.
- Whole-word and case-insensitive modes have unit tests on pure search logic.

### 5.7 Session Restore and Recent Projects

Change:

- Persist open non-untitled documents, active path, cursor/scroll, last project root, and recent project roots.
- Restore after settings load and before the welcome document becomes the active user-facing tab.
- If any session file is restored, close or suppress the default welcome document unless the user explicitly opened it.
- If the project root is missing, skip project restore and still restore standalone files that exist.

Acceptance:

- Relaunch after three open files restores those three files, active tab, cursor, and project tree.
- Deleted files are skipped with one warning summary, not one modal per file.

## 6. Error Handling

- Missing session file: skip and include in a single warning toast.
- Malformed session section: ignore that section.
- Unknown document kind: open as text.
- Unsupported binary file: show an unsupported-file surface; do not load huge/binary bytes into the code editor.
- Close prompt cancel: no state change.
- Location id missing: ignore click and clear stale selection.

## 7. Tests

- Command registry: shortcut, menu, toolbar, and palette all dispatch the same id exactly once.
- Command registry regression: sidebar command works from shortcut and palette.
- Document kind: case-insensitive `.LEVEL` and `.scene` map to scene.
- Cursor persistence: tab switch and relaunch restore cursor.
- Close safety: dirty two-tab session blocks File > Exit and OS close until confirmed or saved.
- Session restore: missing file is skipped while other files restore.
- Search options: case-insensitive and whole-word searches return expected lines.
- Location store: Windows-style path `C:\tmp\a.zia` does not break navigation.

## 8. Manual Verification

- Open/edit/save/close dirty tabs.
- Click OS window close with one dirty tab and with multiple dirty tabs.
- Search a project and click several results.
- Relaunch after opening a project, code file, and scene file.
- Confirm all new UI works in dark and light themes.
