# Plan 11 — Unified resizable bottom panel

## 1. Objective & scope

Give the bottom tool area one consistent, user-resizable home. Today each panel
(Problems, Output, Search, References, Variables, Call Stack, Debug Console,
Terminal) is a separate sibling widget with a hardcoded height — 120px lists,
100px output, 170px search, 220px terminal — shown/hidden per tab. Consequences:
the editor viewport jumps by up to 120px when switching panel tabs, and nothing
can be resized. Replace with: a single bottom container inside a vertical
split, drag-resizable, uniform height across tabs, height persisted in settings.

**In scope:** `workbench_shell.zia` + `tool_panel_shell.zia` layout
restructure, splitter behavior + persistence, per-panel `SetFlex(1.0)`
migration, panel show/hide toggle command behavior, probes.

**Out of scope:** panel content virtualization, dockable/floating panels,
activity-bar changes, new panels.

## 2. Current state (verified anchors)

- Panel construction (`viperide/src/ui/tool_panel_shell.zia:157-260`):
  `toolTabBar` + each panel added directly to `editorArea` with fixed heights —
  `diagListBox.SetSize(0, 120)` (`:174`), `outputPane.SetSize(0, 100)` (`:180`),
  `outputListBox.SetSize(0, 100)` (`:185`), `searchPanel.SetSize(0, 170)`
  (`:190`), `referencesListBox`/`debugListBox`/`variablesListBox`/
  `callStackListBox` 120 (`:226-244`), `terminalPanel.SetSize(0, 220)` (`:249`).
- Editor + panels share `editorArea` (a VBox); the editor row has
  `SetFlex(1.0)` (`workbench_shell.zia:143-147`), so panel height directly
  steals editor height per-panel-differently.
- SplitPane widget: `GUI.SplitPane.New(parent, direction)`
  (`runtime.def:2401-2405`, `First`/`Second` child hosts, `SetPosition(f64)`
  fraction); `VG_SPLIT_VERTICAL` divides top/bottom
  (`src/lib/gui/src/widgets/vg_splitpane.c:68`); drag-to-resize is built into
  the widget (drag handling around `:386-456`). The sidebar already uses a
  horizontal one (`workbench_shell.zia:89-91`, direction arg `1`).
  **Verify the direction enum value for vertical** (read
  `vg_splitpane.c`/its header + the `rt_splitpane_new` mapping — likely `0`).
- Panel switching logic: `SelectToolPanel`/`PumpToolTabs` inside
  `tool_panel_shell.zia` (the tab bar at `:159-169`); view-menu toggles in
  `commands/view_commands.zia`.
- Settings: `core/settings.zia` persists ints/bools/strings; applied at startup
  via `view_commands.applyPersistentSettings` (`main.zia:236`).
- Sidebar-collapse precedent for splitters: `ToggleSidebar` sets position 0.0 /
  0.20 (`workbench_shell.zia:155-163`).

## 3. Design

### 3.1 Layout

```
editorArea (VBox)                        // unchanged parent
└── editorSplit (SplitPane, VERTICAL)    // NEW — SetFlex(1.0)
    ├── First:  editor host VBox         // tabBar? NO — see below
    │           └── editorRow (existing) // editor + minimap
    └── Second: bottomPanelHost (VBox)   // NEW
        ├── toolTabBar (existing)
        └── panelStack (VBox, flex 1)    // all panels, SetFlex(1.0) each,
                                         // one visible at a time
```

Decisions:

- `tabBar` (document tabs), `breadcrumb`, `findBar` stay ABOVE the split in
  `editorArea` (they belong to the editor, not the panel area) — only
  `editorRow` moves into `First`.
- The tool tab bar moves INTO the bottom host so the whole bottom region
  (tabs + content) shows/hides as one unit.
- Every panel: replace `SetSize(0, H)` with `SetFlex(1.0)`; the *host* owns the
  height via the split position. Search panel keeps its internal rows; its
  results list already has `SetFlex(1.0)` (`tool_panel_shell.zia:224`).
- Hidden state: when no panel is visible (user closed the bottom area), set
  split position to 1.0 (bottom collapsed) and hide `bottomPanelHost` — mirror
  `ToggleSidebar`'s approach. Restore to the persisted fraction on show.
- Terminal resize: `terminalPane` cell metrics drive PTY size
  (`terminal_controller.zia:133-154`, `ResizeIfNeeded` per frame `:203-211`) —
  dragging the splitter now resizes the PTY live. No changes needed; verify.

### 3.2 Persistence + interaction

- Setting `bottomPanelHeightPct` (int, default 30 = bottom takes 30%);
  saved when the user releases a splitter drag: poll
  `editorSplit.GetPosition()` each frame in `PumpToolTabs` and persist on
  change-and-settle (write via the settings manager on a 500ms debounce —
  reuse `Viper.Threads.Debouncer` as in `main.zia:330`).
- Clamp: position clamped to [0.5, 0.95] visually (min editor 50%, min panel
  5%) — check whether SplitPane supports min sizes natively (grep `min` in
  `vg_splitpane.c`); if yes use that, else clamp in the pump.
- Double-click on splitter (if the widget reports it) is out of scope.

### 3.3 Migration mechanics

`tool_panel_shell.Build(editorArea, app)` signature stays; internally it builds
the split + host. `workbench_shell.Build` passes the same `editorArea` but the
call moves from "after editorRow" to owning the restructure: cleanest is to
build the split in `workbench_shell.Build` (it owns layout per its header
invariants, `workbench_shell.zia:10-12`) and pass `bottomPanelHost` to
`toolPanels.Build`. Update `AppShell.Build` ordering accordingly
(`app_shell.zia:72-76`). All widget references exposed by `ToolPanelShell`
keep their names — consumers (`main.zia`, controllers) are untouched except
`SelectSurface`/panel-visibility helpers inside the two shell modules.

## 4. Implementation steps

1. Verify the vertical direction constant via `vg_splitpane.c` + the
   `rt_splitpane_new` bridge; note it in code.
2. Restructure `workbench_shell.Build` (split; editorRow into First; host into
   Second) and `tool_panel_shell.Build` (tab bar + panelStack in host; flex
   migration; delete the seven `SetSize(0, H)` calls).
3. Panel show/hide path: single `ShowPanel(kind)` / `HideAllPanels()` in
   `tool_panel_shell.zia` managing visibility + split collapse/restore; update
   `view_commands.zia` toggles and `PumpToolTabs`.
4. Persistence: setting key, startup apply, drag-settle save.
5. Probes: extend `viperide/src/probes/terminal_render_probe.zia` /
   `console_search_probe.zia` if they assert panel heights (grep `SetSize` and
   height literals in probes; update expectations). New assertions in
   `smoke_probe.zia` or a small `bottom_panel_probe.zia`: switching all eight
   tabs keeps `editorRow` height constant; setting a split position then
   re-reading round-trips. `LABELS "zia;viperide;shell"`.
6. Manual: drag the splitter with terminal open (PTY reflows), switch every
   tab (no editor jump), close/reopen bottom area, restart IDE (height
   restored), F11 fullscreen + window resize sanity.
7. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/ui/workbench_shell.zia` — split construction.
- `viperide/src/ui/tool_panel_shell.zia` — host/stack/flex/show-hide.
- `viperide/src/ui/app_shell.zia` — build ordering (if signatures shift).
- `viperide/src/commands/view_commands.zia` — panel toggle commands.
- `viperide/src/core/settings.zia`, `app/settings_applier.zia` — persistence.
- `viperide/src/main.zia` — only if panel-visibility calls live there (grep
  `SetVisible` on panel widgets).
- Probes as per step 5; `src/tests/CMakeLists.txt` if a new probe is added.
- `viperide/docs/status.md` — update the "not resizable" limitation line.

## 6. Testing

Probe assertions (step 5) + full viperide label run. The manual pass (step 6)
is required because splitter feel and PTY reflow are visual/interactive.

## 7. Acceptance criteria

- All eight panel tabs share one height; switching tabs never moves the editor.
- The boundary drags smoothly; terminal PTY reflows live; height survives
  restart; collapsed state restores correctly.
- No probe regressions.

## 8. Repo rules (read before starting)

- Zia-only plan: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; keep `tool_panel_shell.zia` under control —
  it is already 1228 lines; extract a `bottom_panel_layout.zia` helper if the
  restructure adds >150 lines.
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
