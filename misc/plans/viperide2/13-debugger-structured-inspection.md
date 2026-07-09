# Plan 13 — Structured debugger inspection UI

## 1. Objective & scope

Turn the debugger's flat string dumps into a real inspection surface:
expandable object/list/map variables with lazy child loading, and a dedicated
watch UI with inline add/edit/remove — replacing today's command-palette-based
watch management.

**Depends on:** Plan 25 (debug adapter emits `varRef`/`childCount` and answers
`variables` requests). **Composes with:** Plan 11 (the Variables panel lives in
the unified bottom panel; either order works).

**In scope:** `debug_session.zia` protocol consumption, expandable tree-state
model over the existing `VirtualTree` grouping, Variables-panel interaction
(expand/collapse on click, indentation, type display), watch add/edit/remove
controls in-panel, probes.

**Out of scope:** protocol changes (plan 25), hover-to-evaluate in the editor,
"set value" editing of variables (future), memory/registers views.

## 2. Current state (verified anchors)

- Documented gap (`viperide/docs/status.md:261-268`): variables grouped but not
  expandable ("the debug adapter currently returns flat local/watch values");
  watch management is command-palette based.
- Zia consumer: `viperide/src/build/debug_session.zia` (782 lines) — owns the
  adapter process, JSON command send / event parse, watches list, locals, call
  stack (per `viperide/docs/architecture.md` Build/Run/Debug section:
  newline-JSON on stdin, sentinel-tagged JSON events on stderr).
- UI: `variablesListBox` (a plain ListBox, `tool_panel_shell.zia:236-239`);
  rows produced through a `VirtualTree` model for Watches/Locals groups
  before rendering into the ListBox (status.md "Variables panel rows are
  grouped through a `VirtualTree` model") — find the model usage in
  `debug_commands.zia`/`tool_panel_shell.zia` (grep `VirtualTree`).
- Watch commands today: palette flows `handleAddWatchExpression`
  (`main.zia:524-526` via `commandInputOverlay`), plus remove/refresh/clear
  palette commands (status.md:256).
- Selection→navigation pattern for panel rows: `callStackListBox` selection →
  `debug_commands.handleCallStackSelect` (`main.zia:475-477`).
- Gutter/breakpoint UI patterns: `ui/debug_breakpoint_overlay.zia` shows the
  overlay style for small debug input forms (`:205-223`).

## 3. Design

### 3.1 Data model (`build/debug_session.zia` or a new `build/var_model.zia`)

```zia
class VarNode {
    expose String name;      // display name
    expose String value;     // display string
    expose String typeName;
    expose Integer varRef;   // 0 = leaf
    expose Integer childCount;
    expose Boolean expanded;
    expose Boolean childrenLoaded;
    expose List children;    // List of VarNode
    expose Integer depth;    // for indentation
}
```

- Roots: `Watches` group (one VarNode per watch expression, refreshed at stop)
  and `Locals` group — mirroring today's grouping.
- Expansion: click on an expandable row → if `!childrenLoaded`, send
  `{"cmd":"variables","varRef":N,"start":0,"count":100}`; children arrive via
  the event pump → populate → re-render. Show a transient `…loading` child row
  while in flight (removed on arrival). >100 children: append a
  `"… (+K more)"` leaf; clicking it pages the next 100 (`start` advances).
- Invalidation: on any resume/step (the adapter kills refs per plan 25), clear
  `children/childrenLoaded/varRef` on all nodes but **preserve the expansion
  path by name**: after the next stop's roots arrive, auto-re-expand nodes
  whose (path-of-names) was expanded before — this is what makes stepping
  through code with an expanded struct feel right.

### 3.2 Rendering into the ListBox

Keep the ListBox (no new widget): rows are flattened depth-first from the
VarNode forest, formatted as
`"{indent}{twisty} {name}: {value}    [{type}]"` with `twisty` = `▸`/`▾`/space
(expandable-collapsed / expanded / leaf). Row metadata (ListBox item data,
same mechanism as `locStore` location data — `GUI.ListBox.ItemGetData`,
`main.zia:438`) encodes the node id so clicks map back to VarNodes. Rebuild
through the existing bounded stable-row model (`tool_panel_model.ToolPanelRows`,
`tool_panel_shell.zia:92-97`) so memory stays capped.

Interaction: `variablesListBox.WasSelectionChanged()` → if selected node is
expandable, toggle expand/collapse (selection acts as activation — consistent
with how other panels navigate on selection, `main.zia:435-477`).

### 3.3 Watch management in-panel

Add a small control row above the variables list (in the Variables panel host):
`[+ Add]` `[Edit]` `[Remove]` `[Refresh]` `[Clear]` buttons
(`GUI.Button`, pumped like `terminalKill` in `main.zia`/panel pump).

- Add/Edit use the existing non-modal `commandInputOverlay`
  (`COMMAND_INPUT_ADD_WATCH` already exists, `main.zia:524`; add
  `COMMAND_INPUT_EDIT_WATCH` following the same enum/flow in
  `ui/command_input.zia`).
- Remove/Clear act on the selected watch root / all watches — logic already
  exists behind palette commands (`debug_commands.zia` watch handlers); the
  buttons call the same handlers. Palette commands stay (both entry points).
- Watch rows render like locals (expandable when the adapter returns refs for
  watch results — plan 25 covers watches too).

## 4. Implementation steps

1. Read `debug_session.zia` + `debug_commands.zia` watch/locals plumbing fully;
   locate the VirtualTree usage and the stop-event parse site.
2. `VarNode` model + parse of `varRef`/`childCount` from stop events
   (tolerant: absent fields → leaf, so the plan works against an old adapter).
3. `variables` request/response wiring in the session's command/event pump;
   in-flight bookkeeping keyed by varRef; resume invalidation + named-path
   re-expansion.
4. Flattened rendering + twisty formatting + click-to-toggle via row metadata.
5. Watch control row + edit flow + handler reuse.
6. Probe: extend `viperide/src/probes/debug_probe.zia` (it already scripts
   adapter sessions) — fixture with a class instance + list; stop; expand;
   assert child rows appear and paging row shows for >100; step; assert
   re-expansion by name. If plan 25's probe fixtures exist, reuse them.
7. Update `viperide/docs/status.md` debugger section (remove the two gap
   bullets this plan closes; keep honest about what remains — no set-value,
   no hover-evaluate).
8. Full no-skip build + test run; manual debug session against a real program
   (`viperide/src/probes` fixtures or an example game) — expand/collapse feel,
   stepping with expanded nodes, watch add/edit/remove from the panel.

## 5. Files to modify

- `viperide/src/build/debug_session.zia` — protocol + model (or new
  `build/var_model.zia` if >150 lines of model code — respect the 300-line
  review trigger).
- `viperide/src/commands/debug_commands.zia` — handlers, button pump, row
  interaction.
- `viperide/src/ui/tool_panel_shell.zia` — Variables panel control row + host.
- `viperide/src/ui/command_input.zia` — edit-watch input kind.
- `viperide/src/main.zia` — pump wiring for the new buttons/input kind (follow
  the `COMMAND_INPUT_ADD_WATCH` pattern, `main.zia:524-527`).
- `viperide/src/probes/debug_probe.zia` — coverage.
- `viperide/docs/status.md` — honesty update.

## 6. Testing

Probe-driven adapter session (step 6) is primary; existing debug probe
scenarios (breakpoints/stepping/locals/watch persistence) must stay green.
Manual interactive session (step 8) validates feel.

## 7. Acceptance criteria

- A stopped frame shows `▸ player: Player {…}`; clicking expands to fields;
  nested expansion works; lists page at 100.
- Stepping preserves expansion by name; stale refs never crash (adapter
  rejects them; UI treats failure as collapsed).
- Watches are added/edited/removed from the panel without opening the palette;
  palette commands still work.
- Flat-mode compatibility: against an adapter without plan 25, the panel
  renders exactly today's flat rows.

## 8. Repo rules (read before starting)

- Zia-only plan: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; keep files under the size budget (split the
  model out rather than growing `debug_session.zia` past ~900 lines).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
