# GUI Runtime Additions — Recommendations from the ViperIDE Hand-Rolling Review

**Status:** Recommendations (no runtime code yet — each item below requires an ADR before
implementation, per [`docs/adr/0006-spec-currency-and-adr-triggers.md`](../../../docs/adr/0006-spec-currency-and-adr-triggers.md)).
**Driver:** ViperIDE (`viperide/src/`, 104 `.zia` files, ~105K LOC) — the largest real GUI app
written in Viper, hence the best evidence of where GUI programming on the runtime forces
hand-rolling.
**Audience:** runtime maintainers. **Cross-reference:** [`docs/viperlib/gui/`](../../../docs/viperlib/gui/).

---

## 1. Executive summary

This is **not** a "missing toolkit" story. The Viper runtime already ships a rich GUI surface:
30+ widgets (`VBox/HBox/SplitPane/CodeEditor/ListBox/TreeView/TabBar/MenuBar/Toolbar/StatusBar/
ContextMenu/CommandPalette/Minimap/VirtualList/FloatingPanel/…`), an anti-aliased 2D core
(`vg_draw`), full input (`Input.Keyboard/Mouse/Pad/Action/Manager/KeyChord`), file dialogs,
clipboard, drag-drop, tweens/easing/timers, themes, and tooltips. ViperIDE is *thin* over this
toolkit — it delegates rendering, hit-testing, scrolling, and text editing to widgets.

The hand-rolling that remains is concentrated and falls into three shapes, **none of which is a
truly absent capability category**:

1. **No command/event binding.** Widgets have *no* callback API — they are polled. The runtime
   ships three *independent* command registries that nothing ties together or to menus/toolbars,
   so the app hand-writes the glue (R1).
2. **Primitives trapped below the widget layer or in the wrong namespace.** Text metrics exist on
   `Canvas` and `CodeEditor` but not on other text widgets (R2); keyed timing primitives exist in
   `Viper.Threads.*` but lack the one feature an editor needs and aren't where GUI devs look (R3);
   only `App.GetScale()` is exposed, so every app redoes physical↔logical DPI math (R4).
3. **A few missing convenience widgets/helpers** — panel centering and a grid/table (R5), a
   reusable filtered popup (R6), and identifier/caret text helpers (R7).

The biggest single payoff (R1) is mostly **unifying pieces that already exist**, not inventing new
concepts.

## 2. Method & evidence basis

Gap = *(ViperIDE hand-rolled patterns)* ∩ *(runtime surface that is absent, mislayered, or
misnamed)*. ViperIDE patterns came from a full read of `viperide/src/`; the runtime surface was
read directly from [`src/il/runtime/runtime.def`](../../../src/il/runtime/runtime.def) (the
authoritative registry) and confirmed against the live binary. Every "exists / absent" label below
was verified by grep against `runtime.def`, not from memory — see the Appendix for the commands.

## 3. What the runtime already provides (do not re-propose)

So readers don't reinvent these, the directly relevant existing surface:

- **Command pieces:** `Viper.GUI.Shortcuts` (Register/WasTriggered/SetEnabled, ~2567),
  `Viper.GUI.CommandPalette` (AddCommand/WasSelected/GetSelected, ~3022),
  `Viper.GUI.CommandState` (SetEnabled/SetChecked/Snapshot, ~3066). *Three registries, no binder.*
- **Editor geometry:** `CodeEditor.GetCursorPixelX/Y`, `GetLineAtPixel`, `GetColAtPixel` (9349-9352);
  `Canvas.TextWidth/TextHeight`.
- **Timing:** `Viper.Threads.Scheduler` (Schedule/Cancel/IsDue/Poll/Pending/Clear, 4576-4582),
  `Viper.Threads.Debouncer` (4567), `Viper.Threads.Throttler` (4563-4570); `Viper.Time.Clock.Ticks`;
  `Viper.Game.Timer/Tween` + `Viper.Math.Easing`.
- **DPI:** `App.GetScale()`. **Theming/UX:** `Theme.SetDark/Light`, `Tooltip.SetDelay`, drag-drop,
  `FileDialog`, `MessageBox`, `Clipboard`.

## 4. Recommendations

Per item: **Gap · Evidence · Proposed API · Beneficiaries · Effort / ADR.**

### Tier 1 — highest impact × breadth

#### R1 · `Viper.GUI.Command` — command/action binding

- **Gap.** No widget exposes a click/event callback (verified: the only "*Clicked" method is the
  poll-style `Toast.WasActionClicked`; `Viper.Core.MessageBus` is a generic bus, not widget-bound).
  Everything is polled. The three command registries (Shortcuts, CommandPalette, CommandState) are
  not connected to each other or to menu items / toolbar buttons, so the app must register a command
  in each place and keep them in sync by hand.
- **Evidence.** `viperide/src/app/dispatch_helpers.zia:23-62` (`Triggered`/`TriggeredNoMenu` exist
  solely to OR-together menu+toolbar+shortcut+palette); `commands/command_registry.zia` (336 LOC,
  the catalog) + `commands/main_command_dispatcher.zia` (486 LOC, the routing); **65 `WasClicked()`
  poll sites** across the IDE.
- **Proposed API.** A `Viper.GUI.Command` object: `New(id, title)`, `SetShortcut(str)`,
  `SetEnabled(bool)` / `SetEnabledPredicate`, `SetChecked(bool)`, `BindMenuItem(item)`,
  `BindToolbarItem(item)`, and a single `WasInvoked() -> bool`. A `Viper.GUI.CommandRegistry` owns
  commands and, in one `Poll()`, routes menu + toolbar + shortcut + palette to the right command and
  pushes enabled/checked state to every bound widget. Folds the existing Shortcuts / CommandPalette /
  CommandState into one model instead of three.
- **Beneficiaries.** Every app with menus + toolbars + keyboard shortcuts (the default desktop-app
  shape).
- **Effort / ADR.** Medium-high, but mostly unification. **ADR required.** Highest payoff.

#### R2 · Widget-level text/cell metrics

- **Gap.** `CodeEditor` and `Canvas` expose text geometry, but the *other* text widgets (OutputPane,
  the terminal's host pane, ListBox) expose none — so apps assume a fixed monospace cell.
- **Evidence.** `viperide/src/terminal/terminal_controller.zia:127` (`var cols = width / 8`), `:143`
  (`var rows = height / 18`); `viperide/src/ui/app_shell.zia:1602` (`col = app.GetWidth() / 8` wrap
  column); `viperide/src/ui/tool_panel_text.zia:41-59` hardcodes column widths
  (`KindColumnWidth()=10`, `LocationColumnWidth()=24`, …). Verified: `Viper.GUI.OutputPane.*` has no
  cell/measure/metrics method.
- **Proposed API.** On text widgets: `GetCellWidth()`, `GetCellHeight()`, `ColumnsForWidth()`,
  `RowsForHeight()`; and/or a generic `Widget.MeasureText(text) -> i64` that uses the widget's own
  font, or `Viper.GUI.Font.Measure(font, text)`. Mirrors what `CodeEditor` already has.
- **Beneficiaries.** Terminals, log/output panes, any tabular or proportional-font layout.
- **Effort / ADR.** Low-medium. **ADR required** (small surface).

#### R3 · Revision-aware idle scheduling (extend the existing scheduler — don't add a new one)

- **Gap.** The runtime *already* has keyed timing primitives in `Viper.Threads.*`: `Scheduler`
  (`Schedule(key, delayMs)/Cancel/IsDue/Poll/Pending/Clear`), `Debouncer`, `Throttler`. ViperIDE
  *uses* them for the simple cases. But its core editor scheduler can't be built on them because it
  needs **revision/generation supersession** — a job queued for `(path, revN)` must be invalidated
  the instant `revN+1` arrives — which a string-key-only `IsDue(key)` cannot express. So the IDE
  hand-rolls timing on raw `Viper.Time.Clock.NowMs()`.
- **Evidence.** Uses of the existing primitives: `editor/semantic_tokens.zia:68`,
  `editor/inlay_hints.zia:42`, `main.zia:280` (autosave Debouncer), `app/workspace_watcher.zia:53`
  (Throttler). Hand-rolled instead: `editor/scheduler.zia` (435 LOC — `Queue(kind, path, revision,
  delayMs)`, `IsQueued/IsDue` keyed on `(path, revision)` + `generation` tokens), with the raw-tick
  pattern repeated in `editor/hover.zia:152,182`, `editor/diagnostics.zia:165,401`,
  `editor/completion.zia:286,310,824` (15+ sites total).
- **Proposed API.** Add a generation dimension to `Viper.Threads.Scheduler` rather than a new class:
  `Schedule(key, delayMs, generation)`, `Poll()` yielding `(key, generation)`, and
  `IsDueGen(key, generation)` that fires only for the latest generation of a key. Document
  `Viper.Threads.{Scheduler,Debouncer,Throttler}` as the canonical "debounced background work" path
  (optionally surface a GUI-friendly alias) so editor code stops reaching for `GetTickCount`.
- **Beneficiaries.** Any responsive app doing edit-superseding background work — live diagnostics,
  search-as-you-type, live preview, incremental indexing.
- **Effort / ADR.** Small-medium (extend `rt_scheduler`). **ADR required.**

### Tier 2 — low-risk additive wins

#### R4 · DPI logical-unit helpers

- **Gap.** Only `App.GetScale()` is exposed; widget geometry getters return *physical* pixels while
  floating-panel `SetSize/SetPosition` take *logical* units, so every overlay redoes the conversion.
- **Evidence.** `physicalToLogical` is **duplicated by hand in three files**:
  `viperide/src/ui/ide_overlays.zia:717-728`, `viperide/src/ui/debug_breakpoint_overlay.zia:223-224`,
  `viperide/src/ui/explorer_actions.zia:346-347`. Verified: no `App.*` logical/ToLogical/ToPhysical
  method exists.
- **Proposed API.** `App.GetLogicalWidth()`, `App.GetLogicalHeight()`, `App.ToLogical(px)`,
  `App.ToPhysical(lu)`.
- **Beneficiaries.** Every HiDPI-aware app. **Effort / ADR.** Trivial. **ADR required** (tiny). Cheapest win.

#### R7 · Text/char editing helpers

- **Gap.** No identifier/word character classification and no "insert text then place caret at an
  offset within it" — both are common to any code/text editor.
- **Evidence.** `viperide/src/app/dispatch_helpers.zia:64-80` (`HasIdentifierInput` hand-classifies
  `a-z/A-Z/0-9/_`); `viperide/src/editor/completion.zia:724-744` (`PlaceCursorAtInsertedOffset` walks
  inserted text counting `\n` to compute the caret line/col). Verified: no `Viper.Text.Char` class
  and no `CodeEditor.InsertAndPlaceCursor`.
- **Proposed API.** `Viper.Text.Char.IsIdentifierStart/IsIdentifierPart/IsAlnum`;
  `CodeEditor.InsertAndPlaceCursor(text, caretOffset)`.
- **Beneficiaries.** Any text-editing UI. **Effort / ADR.** Small. **ADR required** (small surface).

### Tier 3 — larger / more structural

#### R5 · Layout conveniences (panel centering + grid/table)

- **Gap.** No helper to center/clamp a floating panel, and no column-aligned data widget.
- **Evidence.** `viperide/src/ui/ide_overlays.zia:690-715` (`layoutAboutPanel` centers and
  screen-clamps a modal by hand: `(rootW - panelW) / 2`, margin clamps); `viperide/src/ui/
  tool_panel_text.zia` pads columns with strings against the hardcoded widths from R2. Verified: no
  `FloatingPanel` center/align method and no `Viper.GUI.Grid`/`Table`.
- **Proposed API.** `FloatingPanel.CenterInParent()` / `App.CenterPanel(panel)`; a `Viper.GUI.Grid`
  (or `Table`) widget with auto-sized columns and optional headers.
- **Beneficiaries.** Dialogs/modals, property inspectors, any data table. **Effort / ADR.** Low
  (centering) to medium (grid). **ADR required.**

#### R6 · `Viper.GUI.PopupList` — reusable filtered popup

- **Gap.** No caret-anchored, filterable, keyboard-navigable popup list with dismiss-on-edit. Apps
  build the state machine by hand; *positioning* is the only solved part (caret-pixel methods exist).
- **Evidence.** `viperide/src/editor/completion.zia` (1,288 LOC) is largely a popup state machine
  (`isVisible`, `selectedIndex`, `lastLine`, `lastCol`, dismiss-on-line-change);
  `PositionPopupNearCursor` (`:1190`) already calls `editor.GetCursorPixelX/Y`. Verified: none of
  Dropdown/ContextMenu/CommandPalette/Tooltip is a caret-anchored filtered list.
- **Proposed API.** `Viper.GUI.PopupList`: `SetItems`, `SetFilter`, `AnchorAt(px, py)`,
  `NavigateUp/Down`, `AcceptSelected`, `DismissOnEdit`, `WasAccepted`, `GetSelected`. The
  language-specific filtering/ranking stays in the app; only the widget mechanics move to the runtime.
- **Beneficiaries.** Any autocomplete: search boxes, address bars, command inputs, snippet pickers.
- **Effort / ADR.** Medium-high; most speculative — **sequence last. ADR required.**

## 5. Prioritization & sequencing

1. **R4** (DPI helpers) and **R2** (widget metrics) — cheapest, immediately delete duplicated/guessed
   math, low ABI risk. Do first.
2. **R7** (char/caret helpers) — small, broadly reusable.
3. **R3** (scheduler generation dimension) — small extension of an existing class; lets the IDE
   replace 435 LOC and 15+ raw-tick sites.
4. **R1** (command binding) — the flagship; largest payoff but the most design. Do once the cheap
   wins prove the ADR cadence.
5. **R5** then **R6** — new widgets; most surface, least urgency.

## 6. ADR plan

Each recommendation adds to the runtime C-ABI surface (new/changed `runtime.def` entries), so each
needs its own ADR before implementation, per `docs/adr/0006-spec-currency-and-adr-triggers.md`.
Allocate the next free numbers (**0017+**; do not hard-assign here). Any future implementation must
also satisfy the established runtime gates:

- `scripts/check_runtime_completeness.sh` (every `RT_METHOD`/`RT_PROP` needs its `RT_FUNC`).
- Runtime class **leaf-name uniqueness** (`check_runtime_class_leaf_names` / the qualified-surface
  test) — new class leaves like `Command`, `PopupList`, `Grid` must be globally unique.
- `source_health_audit` `runtime_api_contract_files` baseline bump if new `rt_*.c/.h` files are added.

## 7. Non-goals (already provided — explicitly *not* recommended)

Listing these prevents duplicate proposals:

- **Caret-to-pixel anchoring** — `CodeEditor.GetCursorPixelX/Y`, `GetLineAtPixel`, `GetColAtPixel`.
- **Keyed scheduling / debounce / throttle** as a *new* class — `Viper.Threads.Scheduler/Debouncer/
  Throttler` exist; R3 only adds a generation dimension to what's there.
- **Tweens / easing / one-shot timers** — `Viper.Game.Tween/Timer`, `Viper.Math.Easing`.
- **Text measurement on a drawing surface** — `Canvas.TextWidth/TextHeight` (R2 is the *widget* gap).
- **Themes, delayed tooltips, drag-drop, file dialogs, message boxes, clipboard, DPI scale query** —
  `Theme.SetDark/Light`, `Tooltip.SetDelay`, widget drag-drop, `FileDialog`, `MessageBox`,
  `Clipboard`, `App.GetScale`.

## Appendix — verification commands

```sh
# Evidence (ViperIDE hand-rolling)
rg -n "WasClicked" viperide/src -g'*.zia'                 # ~65 poll sites (R1)
rg -n "/ 8|/ 18" viperide/src -g'*.zia'                   # monospace guesses (R2)
rg -n "physicalToLogical|PhysicalToLogical" viperide/src -g'*.zia'   # 3 files (R4)
rg -n "GetTickCount" viperide/src -g'*.zia'               # raw-tick timing (R3)

# Existing vs proposed surface (authoritative registry)
rg -n '"Viper\.GUI\.(Shortcuts|CommandPalette|CommandState)' src/il/runtime/runtime.def   # exist
rg -n '"Viper\.Threads\.(Scheduler|Debouncer|Throttler)'     src/il/runtime/runtime.def   # exist
rg -n '"Viper\.GUI\.Command"|"Viper\.GUI\.(PopupList|Grid|Table)'   src/il/runtime/runtime.def   # absent
rg -n '"Viper\.GUI\.App\.' src/il/runtime/runtime.def | grep -i logical                  # absent (R4)
rg -n '"Viper\.GUI\.OutputPane\.' src/il/runtime/runtime.def | grep -iE "cell|measure"   # absent (R2)

# Cross-check against the live binary (never drifts; generated from the running registry)
viper --dump-runtime-api | grep -iE "Viper.GUI.Command|OutputPane|Threads.Scheduler"
```
