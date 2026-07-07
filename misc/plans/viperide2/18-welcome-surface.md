# Plan 18 — Real Welcome surface + panel empty states

## 1. Objective & scope

Replace the fake "welcome document" (an editable Zia buffer synthesized at
startup) with a proper start view — recent projects, recent files, Open
Folder / New File actions, version info — and give the empty bottom panels
one-line empty states instead of blank listboxes.

**In scope:** a Welcome view widget surface shown when no session restores,
recents wiring, actions, dismissal/lifecycle rules, "Show Welcome" command,
empty-state rows for Problems/Search/References/Variables/Call Stack, probe.

**Out of scope:** onboarding tours, news/changelog feeds, template galleries,
scene-editor anything.

## 2. Current state (verified anchors)

- Startup synthesizes a welcome *code buffer* when nothing restored:
  `welcomeText = "module Welcome;\n"...` + `docMgr.NewDocument()` + tab +
  `engine.LoadDocument(welcomeDoc)` (`viperide/src/main.zia:254-271`). It is a
  real untitled document — it participates in save prompts and session recovery.
- Recents already exist and persist: `core/session.zia` stores "recent
  projects, recent files" (`viperide/docs/architecture.md` Sessions section);
  recent files are surfaced via `file:` data rows in the output listbox path
  (`main.zia:448-453` — `explorer_action_runner.OpenFileAndRecord`).
- Open-folder flow exists: explorer "Open folder" + project manager
  (`docs/status.md` Explorer list); New File flow exists in `file_commands.zia`.
- Version/build info: `app/build_info.zia` (`BuildSummary()`, `main.zia:123`),
  `VIPERIDE_VERSION` (`main.zia:109`).
- Surface switching precedent: `shell.SelectSurface(kind)` shows the editor row
  per document kind (`ui/workbench_shell.zia:165-171`,
  `activeSurfaceKind`); the preferences panel shows how an alternate surface
  is hosted beside/instead of the editor (`preferencesPanel`,
  `workbench_shell.zia:127-132` — hidden VBox in `workbenchRow`).
- Panel empty states: all list panels render zero rows when empty (e.g.
  Problems only fills on diagnostics — `problemsHaveDiagnostics`,
  `tool_panel_shell.zia:89`); no placeholder text anywhere.
- Widgets available: `GUI.Label` with word wrap, `GUI.Button`, `GUI.ListBox`,
  VBox/HBox — sufficient; no new C work.

## 3. Design

### 3.1 Welcome view

New module `viperide/src/ui/welcome_view.zia`:

```
VBox welcomeHost (in editorRow's parent area, sibling of editorRow)
├── "ViperIDE" title + version/build line (build_info.BuildSummary())
├── HBox columns
│   ├── Start:   [Open Folder…] [New File] [Open File…]
│   ├── Recent projects: ListBox (top 8, from sessionMgr recents)
│   └── Recent files:    ListBox (top 8)
└── footer hint: "Ctrl+Shift+P — all commands"
```

- Shown INSTEAD of `editorRow` when there are no open documents:
  `welcomeHost.SetVisible(docMgr.GetCount() == 0)` — driven from a small
  `UpdateWelcomeVisibility()` called after open/close/restore paths (not per
  frame; hook the places `docMgr` count changes: open, close-tab, restore,
  new-doc).
- Startup rule change at `main.zia:254-271`: when `restoredCount == 0`, do NOT
  create the welcome document; show the welcome view. (Delete the
  `welcomeText` block entirely; the "try typing Viper." teaching content moves
  to a `[New File]`-created sample only if desired — simplest: drop it.)
- Interactions (pumped in the main loop like other views):
  - Open Folder… → existing open-folder command handler.
  - New File → existing new-file command → welcome hides (count > 0).
  - Open File… → existing open-file dialog command.
  - Recent project row select → open project (existing recent-projects command
    path — grep the palette/menu handler for recent projects in
    `file_commands.zia`/`search_commands.zia` and call the same function).
  - Recent file row select → `explorer_action_runner.OpenFileAndRecord`
    (`main.zia:451` shows the exact call).
- Command `showwelcome` ("Help: Show Welcome") in `command_catalog.zia` →
  forces the view visible even with docs open (it then hides on next document
  activation) — cheap and useful.
- Dark theme styling only (theme colors come from the theme system; no
  hardcoded light colors).

### 3.2 Panel empty states

In `tool_panel_shell.zia`, for the stable-row panels
(Problems/Search/References/Variables/Call Stack, `ToolPanelRows` model
`:92-97`): when rendering zero rows, render one non-selectable muted row
(color `TOOL_COLOR_MUTED`, `:35`):

- Problems: "No problems detected"
- Search: "Type a query and press Search"
- References: "Run Find References to see results"
- Variables: "Start a debug session to inspect variables"
- Call Stack: "Not paused"

Guard selection handlers: the placeholder row carries empty item data →
existing `IsLocationData` checks (`main.zia:446,461`) already no-op on it;
verify each panel's selection handler tolerates it (Variables/Call Stack
handlers in `debug_commands` — add data checks if absent).

## 4. Implementation steps

1. `welcome_view.zia` (build + pump + recents population from `sessionMgr`).
2. Host wiring in `workbench_shell.Build` (sibling of editorRow) +
   `UpdateWelcomeVisibility` hooks at the ~5 doc-count-changing sites.
3. Startup change (`main.zia:254-271` block removal) + status-bar "Ready"
   retained; `SelectSurface`/breadcrumb calls skipped when no doc.
   **Audit:** everything in the loop assuming `docMgr.GetCount() > 0` /
   `GetActive() != null` — the guards already exist (`main.zia:587,603`);
   verify controllers tolerate zero documents for a full session (run the IDE,
   close all tabs, interact — this state exists today after closing all tabs,
   so risk is low; confirm).
4. `showwelcome` command + catalog + dispatch.
5. Empty-state rows + selection-handler guards.
6. Probes: update any probe that asserts the welcome *document* exists at
   startup (grep probes for `Welcome` / `module Welcome` — `smoke_probe.zia`
   and `phase0_phase1_probe.zia` are the likely consumers; update expectations
   to the welcome view + zero documents). New assertions: fresh start → 0 docs,
   welcome visible; open file → welcome hidden; close all → welcome returns;
   empty-state row text present in Problems when no diagnostics.
   `LABELS "zia;viperide;shell"`.
7. Manual: fresh start (delete/rename settings dir to simulate), recents
   populate after use, all buttons, session-restore path skips welcome,
   `showwelcome` command, dark-theme look.
8. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/ui/welcome_view.zia` — **new**.
- `viperide/src/ui/workbench_shell.zia` — host.
- `viperide/src/main.zia` — startup block, visibility hooks, pump.
- `viperide/src/commands/command_catalog.zia` + handler module — showwelcome.
- `viperide/src/ui/tool_panel_shell.zia` — empty-state rows.
- `viperide/src/commands/debug_commands.zia` — selection guards if needed.
- Probes per step 6; `src/tests/CMakeLists.txt` if new probe files.
- `viperide/docs/status.md` — startup behavior line.

## 6. Testing

Probe updates/additions (step 6); the zero-document session audit (step 3) is
covered by driving the welcome→open→close-all→welcome cycle in the probe.

## 7. Acceptance criteria

- Fresh launch shows the welcome view (no phantom untitled document, no dirty
  prompt on immediate exit); recents are clickable and correct.
- Welcome hides the moment a document opens; returns when all close;
  `showwelcome` works.
- Empty panels teach instead of showing blank boxes; no selection crashes on
  placeholder rows.
- All probes green.

## 8. Repo rules (read before starting)

- Zia-only plan: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; widget construction lives in `src/ui/`.
- Dark theme is the primary theme (user requirement) — verify contrast there.
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
