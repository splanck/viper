# Phase 1 - Code Editor and Project Intelligence

## 1. Summary and Objective

Make ViperIDE a serious project code editor. The goal is project-aware navigation, safe rename, references, signature help, multi-cursor UX, and file-tree operations, all behind explicit language-service capabilities.

This phase must not pretend that a single-file completion engine is a project index. Reference collection, symbol identity, dirty-buffer handling, and workspace edits are the real work.

## 2. Scope

In:

- IDE-side language-service abstraction.
- Zia project index lifecycle with explicit root and source provider.
- Structured definition/reference results.
- Rename built on tested reference ranges and transactional workspace edits.
- Multi-cursor command UX using existing CodeEditor runtime methods.
- Structured signature help UI.
- File tree rename/delete/move and documented ignore behavior.

Out:

- Full BASIC semantic IntelliSense unless a BASIC language-service backend is added.
- Debugging.
- Terminal/process infrastructure beyond what Phase 2 owns.

## 3. Feature Toggles

No global toggle. Each language service reports capabilities:

- `canComplete`
- `canDiagnose`
- `canGoToDefinition`
- `canFindReferences`
- `canRename`
- `canSignatureHelp`

Commands are disabled or show a clear no-op toast when the active language lacks a capability.

## 4. Technical Requirements

### 4.1 Language Service Boundary

Current state:

- Completion, diagnostics, hover, and symbols call `Viper.Zia.Completion.*ForFile` directly.
- BASIC buffers are treated as editor text but do not have equivalent semantic services.

Change:

- Add `editor/language_service.zia`.
- Route active-document tooling through a language-service interface.
- Zia service wraps existing and new Zia APIs.
- BASIC service starts with honest capabilities: syntax/text editing, build/run if project config supports it, no semantic rename unless implemented.
- Text and scene-source services disable semantic commands.

Acceptance:

- Opening a BASIC file does not call Zia semantic APIs.
- Command palette can hide or disable commands based on active service capabilities.

### 4.2 Project Source Provider

Change:

- Add a source provider that enumerates project source files and returns current contents from open dirty documents before falling back to disk.
- Respect hardcoded excludes from Phase 0, then add ignore support in this phase.
- Indexing must have an explicit project root. Do not rely on a singleton cache that guesses the workspace from `filePath`.
- Large projects should index in frame-budgeted slices until Phase 2 provides a general background job system.

Acceptance:

- Editing an unsaved open file changes definition/reference results without requiring a disk save.
- Closing a project drops its index.

### 4.3 Zia Project Index

Current state:

- Existing `CompleteForFile`, `CheckForFile`, `HoverForFile`, and `SymbolsForFile` APIs are path-aware but still source-driven single-file queries.
- `Sema::findSymbolAtPosition` is not by itself a references engine.

Change:

- Add a Zia project-index runtime API with explicit index lifetime. Preferred shape:
  - `Viper.Zia.ProjectIndex.New(root) -> obj`
  - `UpdateFile(index, path, source)`
  - `RemoveFile(index, path)`
  - `Clear(index)`
  - `Definition(index, path, line, col) -> obj`
  - `References(index, path, line, col) -> obj`
  - `RenameEdits(index, path, line, col, newName) -> obj`
- If runtime object returns are not practical yet, use stable serialized records only at the bridge boundary, then immediately parse into IDE-side structured records. Do not use display strings as storage.
- The index stores declarations and reference ranges with enough identity to distinguish shadowed locals, members, imports, and globals.
- Reference collection must ignore comments and strings and must use parser/token/AST information rather than raw string search.

Acceptance:

- Cross-file definition works with at least two files.
- References exclude shadowed symbols with the same name.
- Index update of one file does not require reparsing every file unless imports force invalidation.

### 4.4 Definition and References UI

Change:

- Replace the current string-parsed go-to-definition command.
- Use Phase 0 `LocationStore` and `OpenLocation`.
- Find References populates a results panel with structured locations and preview text.
- Empty results show `No definition found` or `No references found` without moving the cursor.

Acceptance:

- Go-to-definition opens a different file when the definition is external.
- References result clicks are safe for paths containing spaces, colons, and punctuation.

### 4.5 Rename

Rename must not land until reference ranges are reliable.

Requirements:

- Validate the new identifier before asking the engine for edits.
- Engine returns a `WorkspaceEdit`: file path plus ordered non-overlapping ranges and replacement text.
- IDE applies edits transactionally:
  - Dirty open buffers are edited in memory.
  - Clean closed files may be written on disk.
  - Dirty closed/unloaded conflicts abort.
  - External file timestamp changes abort.
  - No partial edits remain after a failure.
- One undo group per open edited document.

Acceptance:

- Rename updates all references and the declaration.
- Rename to an invalid identifier aborts before edits.
- Rename collision in scope returns an error and applies nothing.
- Rename across an unsaved dirty file uses the dirty buffer content.

### 4.6 Multi-Cursor UX

Current state:

- Runtime class `Viper.GUI.CodeEditor` already exposes cursor methods such as `GetCursorCount`, `AddCursor`, `RemoveCursor`, `ClearCursors`, `GetCursorLineAt`, `GetCursorColAt`, `SetCursorPositionAt`, `SetCursorSelection`, and `CursorHasSelection`.

Change:

- Do not add duplicate public cursor APIs unless a missing operation is proven.
- Add editor commands:
  - Add next occurrence.
  - Skip occurrence.
  - Select all occurrences.
  - Clear extra cursors.
  - Add cursor at click position if the GUI input layer can expose the modifier safely.
- Add status-bar cursor count.

Acceptance:

- Typing with multiple cursors edits all cursors predictably.
- Undo returns the buffer to the pre-multi-edit state.
- Extra cursors are cleared on file switch unless persisted intentionally.

### 4.7 Signature Help

Change:

- Add `editor/signature.zia`.
- Prefer structured signature data from the language service:
  - function name
  - ordered parameters
  - active parameter index
  - overload index/count
  - documentation text if available
- The UI uses a floating panel positioned near the cursor.
- If the runtime still returns plain text, parse only as a temporary compatibility path and track a follow-up to remove it.

Acceptance:

- Nested calls choose the innermost active call.
- Commas inside nested calls do not increment the outer active parameter.
- Signature panel hides on cursor movement outside a call.

### 4.8 File Tree Operations and Ignore Rules

Change:

- Rename file/folder:
  - update open documents and tab titles
  - update diagnostics/search/reference locations
  - refresh tree
- Delete file/folder:
  - confirm
  - close or retarget open documents
  - handle permissions errors
- Move/drag can be separate if tree DnD is risky.
- `.gitignore` support must either:
  - implement a documented subset with tests, or
  - use a proper ignore matcher inside the runtime/project service.

Do not claim full `.gitignore` semantics if negation, anchored patterns, directory-only patterns, and nested ignore files are not implemented.

## 5. Error Handling

- Index unavailable: command reports that indexing is still warming up.
- Language lacks capability: command reports unsupported for this language.
- Rename edit conflict: abort all edits and show the first conflict.
- Tree rename/delete failure: keep documents open and show error.
- Ignore parser failure: fall back to hardcoded excludes and show one warning.

## 6. Tests

- Language service: BASIC file does not call Zia completion/diagnostics.
- Index lifecycle: open project, index two files, update one dirty file, query result changes.
- Definition: external function definition opens the defining file.
- References: shadowed local is excluded.
- Rename: valid rename applies across open and closed files; invalid/collision rename applies nothing.
- Workspace edit: overlapping edits are rejected.
- Multi-cursor: add two cursors, type, undo.
- Signature: nested call active-parameter detection.
- Tree rename: open file remains open under new path.
- Ignore rules: documented include/exclude cases.

## 7. Manual Verification

- Navigate definitions and references in a small multi-file Zia project.
- Rename a symbol and inspect the diff before saving.
- Exercise multi-cursor commands from keyboard and command palette.
- Rename/delete files from the tree while documents are open.
- Confirm unsupported semantic commands behave cleanly in BASIC and text files.
