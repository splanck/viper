# Plan 24 — CommandPalette live-query API

## 1. Objective & scope

Give Zia code access to the CommandPalette's live query text so the IDE can
implement *dynamic* palette modes — repopulating items as the user types
(workspace symbols, file search, line numbers). Today the palette is a
fire-and-forget widget: Zia can add/remove/clear items and read the final
selection, but never sees what the user is typing, so every "live" experience
is impossible. Consumer: **plan 19** (unified palette modes).

**In scope:** query accessors + change latch, item mutation while open without
visual reset, optional programmatic query prefill; `runtime.def` entries; probe.

**Out of scope:** the IDE-side mode router (plan 19), changes to fuzzy scoring,
palette visuals.

## 2. Current state (verified anchors)

- Runtime surface today (`runtime.def:3077-3088` + class block `:3162-3169`):
  `New, Destroy, AddCommand, AddCommandWithShortcut, RemoveCommand, Clear, Show,
  Hide, IsVisible, SetPlaceholder, GetSelected, WasSelected`. **No query access.**
- Widget internals (`src/lib/gui/src/widgets/vg_commandpalette.c`): commands[]
  array + `filtered[]` view "rebuilt on every query change" (`:10-17`),
  borrowed pointers (`:23`), `fuzzy_match_score(pattern, text)` (`:117`) with
  word-boundary and consecutive-match bonuses. So a query string already exists
  inside the widget; it is simply not exported.
- Selection reset behavior: "selected_index is reset to 0 (or -1 when empty)
  after each filter pass" (`:17`).
- IDE usage today: `paletteCtrl.ResetToCommands`/`OpenCommands`
  (`viperide/src/app/command_palette_controller.zia`), Quick Open repopulates
  the palette by `Clear` + re-adding rows
  (`viperide/src/commands/quick_open_commands.zia:99+`) — but only *once* at
  open, because it cannot observe subsequent typing; filtering after that is the
  widget's static fuzzy pass over the pre-added rows.

## 3. Design

### 3.1 Widget additions (`vg_commandpalette.c` / its header)

```c
// State
uint64_t query_generation;      // bumped every time the input text changes
char    *last_taken_query;      // for the runtime layer only (optional)
```

New functions:

```c
const char *vg_commandpalette_get_query(vg_commandpalette_t *p);      // borrowed
uint64_t    vg_commandpalette_get_query_generation(vg_commandpalette_t *p);
void        vg_commandpalette_set_query(vg_commandpalette_t *p, const char *text); // prefill; triggers refilter
void        vg_commandpalette_set_client_filtered(vg_commandpalette_t *p, bool on);
```

`set_client_filtered(true)` is the key switch for live modes: the widget skips
its own fuzzy filter and shows items in insertion order (the Zia side is doing
the filtering/ranking as it repopulates per keystroke). Selection preservation:
when items are cleared+re-added while visible in client-filtered mode, keep the
palette open, keep the input text and caret untouched, and clamp
`selected_index` to the new count instead of resetting to 0 when the previous
selected id still exists (compare by command id string).

### 3.2 Runtime bridge + registration

Bridge functions live where the existing `rt_commandpalette_*` set lives
(grep `rt_commandpalette_new` — `src/runtime/graphics/gui/rt_gui_navaids.c` or
`rt_gui_widgets_complex.c`; put new ones beside them).

```c
RT_FUNC(CommandPaletteGetQuery,        rt_commandpalette_get_query,         "Viper.GUI.CommandPalette.GetQuery",        "str(obj)")
RT_FUNC(CommandPaletteGetQueryGen,     rt_commandpalette_get_query_gen,     "Viper.GUI.CommandPalette.GetQueryGeneration", "i64(obj)")
RT_FUNC(CommandPaletteSetQuery,        rt_commandpalette_set_query,         "Viper.GUI.CommandPalette.SetQuery",        "void(obj,str)")
RT_FUNC(CommandPaletteSetClientFilter, rt_commandpalette_set_client_filtered, "Viper.GUI.CommandPalette.SetClientFiltered", "void(obj,i1)")
```

plus matching `RT_METHOD` entries in the `Viper.GUI.CommandPalette` class block.
Generation-counter polling (`GetQueryGeneration`) replaces a boolean
`WasQueryChanged` latch: multiple Zia consumers can each keep their own last-seen
generation (same pattern as `CodeEditor.get_Revision`, `runtime.def:2414`).

### 3.3 Zia usage sketch (implemented by plan 19, shown here as the API contract)

```zia
if shell.commandPalette.GetQueryGeneration() != lastGen {
    lastGen = shell.commandPalette.GetQueryGeneration();
    var q = shell.commandPalette.GetQuery();
    // recompute rows for the active mode, then:
    shell.commandPalette.Clear();
    // AddCommand(id, label, detail) per row — palette stays open, caret intact
}
```

## 4. Implementation steps

1. Add `query_generation` bump wherever the palette's input text mutates
   (typed char, backspace, `set_query`, clear-on-show). Verify by reading the
   widget's key handling — the query lives in its internal input; every write
   path must bump.
2. Implement the four widget functions; implement selection-preservation rule
   for client-filtered repopulation while visible.
3. C test in `src/lib/gui/tests/`: simulate typing into the palette via
   `vg_event_t` key events; assert generation bumps, `get_query` content,
   client-filtered mode shows insertion order, and repopulate-while-open keeps
   the same selected id.
4. Runtime bridge + `runtime.def` + `./scripts/check_runtime_completeness.sh`.
5. Zia probe `viperide/src/probes/palette_query_probe.zia`: open palette,
   inject text (existing probes drive palette input — follow
   `console_search_probe.zia` / `intellisense_probe.zia` input-injection
   patterns), assert GetQuery/generation/SetQuery round-trip; register with
   `LABELS "zia;viperide;palette"`.
6. Full build + test run; confirm existing palette-consuming probes
   (`quick_open` flows inside `phase*` probes) stay green — default behavior
   (client_filtered=false) is unchanged.

## 5. Files to modify

- `src/lib/gui/src/widgets/vg_commandpalette.c` + its declaration header
  (`vg_ide_widgets_ui.h` or wherever `vg_commandpalette_t` is declared — locate
  via grep).
- Runtime bridge file hosting `rt_commandpalette_*`.
- `src/il/runtime/runtime.def` — 4 RT_FUNC + 4 RT_METHOD entries.
- `src/lib/gui/tests/` — C test.
- `viperide/src/probes/palette_query_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

- C test (step 3) is primary — it locks the generation/preservation semantics.
- Probe (step 5) locks the Zia surface.
- Regression: all existing viperide probes green with client-filtered off.

## 7. Acceptance criteria

- Typing in an open palette is observable from Zia within the same frame
  (generation changes, `GetQuery` returns the text).
- Clear+re-add of 100 items while open, in client-filtered mode, causes no
  caret loss, no input-text loss, no flicker-reset of the selected row when its
  id survives.
- `viper --dump-runtime-api` lists the four new members; completeness check passes.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD` entries; run
  `./scripts/check_runtime_completeness.sh` after `runtime.def` edits.
- Full Viper file header on all new/modified C files.
- 100% cross-platform; no platform code involved here.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
