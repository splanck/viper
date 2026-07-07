# Plan 19 — Unified palette modes (`>` commands, `#` symbols, `:` line, files)

## 1. Objective & scope

Collapse ViperIDE's three navigation interaction styles into one palette with
live prefix modes:

- *(no prefix)* — Quick Open files (today's behavior, kept)
- `>` — commands (today's default palette content)
- `#` — workspace symbols, **live results while typing** (today: type into a
  small input overlay, results dumped into a bottom panel)
- `:` — go to line (today: input overlay)

One keystroke surface, one muscle memory, live results everywhere.

**Depends on:** Plan 24 (CommandPalette live-query API: `GetQuery`,
`GetQueryGeneration`, `SetQuery`, `SetClientFiltered`).

**In scope:** a palette mode router, mode implementations reusing existing
search/symbol/line logic, palette open-with-prefix commands and shortcuts,
migration of the two overlay flows, probe.

**Out of scope:** project-wide text search (stays in the docked Search panel —
right home for many-result workflows), BASIC/Zia symbol *provider* changes
(reuse what exists), fuzzy-scoring changes.

## 2. Current state (verified anchors)

- Palette controller: `viperide/src/app/command_palette_controller.zia` owns
  mode transitions today ("command-palette mode transitions",
  `architecture.md`); commands vs Quick Open switching via
  `ResetToCommands`/`OpenCommands`/`IsQuickOpen` (`main.zia:384-403`).
- Quick Open: `commands/quick_open_commands.zia` — palette rows, id encoding,
  deterministic scoring (`QuickOpenScore` exact/prefix + runtime fuzzy
  `Text.FuzzyMatch.Match(needle, rel)` fallback, `:161-182`); selection handled
  at `main.zia:386-389`.
- Workspace symbols: `COMMAND_INPUT_WORKSPACE_SYMBOLS` single-value overlay →
  `search_commands.handleWorkspaceSymbolsText(...)` → results into a bottom
  panel via `locStore` (`main.zia:532-537`).
- Go to Line: `COMMAND_INPUT_GOTO_LINE` overlay →
  `edit_commands.handleGoToLineText` (`main.zia:520-523`).
- Widget fuzzy filter exists for static rows (`vg_commandpalette.c:117`);
  plan 24 adds query access + client-filtered mode + open-state repopulation.
- Symbol providers: Zia via `Viper.Zia.ProjectIndex`-backed flows; BASIC via
  scanner (`docs/status.md` Language Support) — both already produce the rows
  `handleWorkspaceSymbolsText` renders; this plan re-plates them.
- Location navigation: `locStore.OpenLocation(shell, docMgr, engine, tabs, data)`
  (`main.zia:438`) — the single call symbol/file selections should end in.

## 3. Design

### 3.1 Mode router

Extend `command_palette_controller.zia` (it already owns mode state):

```zia
final MODE_COMMANDS = 0;   // ">" or opened via the commands shortcut
final MODE_FILES = 1;      // no prefix (Quick Open)
final MODE_SYMBOLS = 2;    // "#"
final MODE_GOTO_LINE = 3;  // ":"
```

Per-frame pump (new `PumpLiveQuery(shell, ...)` called from `main.zia` next to
the existing palette handling `:384-403`):

1. If palette not visible → reset lastGen, return.
2. `gen = palette.GetQueryGeneration()`; if unchanged → return.
3. Parse prefix from `palette.GetQuery()` → target mode; on mode change,
   switch `SetClientFiltered` (true for FILES/SYMBOLS/GOTO_LINE — the router
   ranks; false for COMMANDS — widget's own fuzzy filter over the static
   command list is already ideal).
4. Dispatch the modes:
   - **FILES:** rest-of-query → existing Quick Open scoring over the cached
     file list (`quick_open_commands` already builds rows + ids); repopulate
     top N (50).
   - **SYMBOLS:** rest-of-query → the same provider call
     `handleWorkspaceSymbolsText` uses, but returning rows instead of filling
     the panel — refactor that handler into
     `search_commands.CollectWorkspaceSymbols(query, ...) -> List` + a thin
     panel-filling wrapper (panel flow stays for anyone who liked it; palette
     calls the collector). Debounce: reuse the scheduler's search job kind
     (`JOB_SEARCH` idle 50ms, `editor/scheduler.zia:9,18`) or a simple 100ms
     `Debouncer` in the controller — symbols queries can hit the index; do not
     query on every keystroke.
   - **GOTO_LINE:** parse `:<n>` (and `:<n>,<m>` col form if
     `handleGoToLineText` supports it — reuse its parser); single synthetic row
     "Go to line N" for feedback.
5. Selection (`WasSelected`, existing handling at `main.zia:384-399`): route by
   mode — files/symbols → `locStore.OpenLocation`/`OpenFileAndRecord`;
   goto-line → `handleGoToLineText`; commands → existing dispatch. Id encoding:
   prefix row ids (`"file:"`, `"sym:"`, `"line:"`) the way Quick Open already
   encodes ids (see `quick_open_commands` id encoding).

### 3.2 Entry points + migration

- Existing shortcuts keep working and now just prefill:
  Quick Open shortcut → open with empty query; command-palette shortcut →
  open with `SetQuery(">")`; workspace-symbols shortcut → `SetQuery("#")`;
  go-to-line shortcut → `SetQuery(":")`. Catalog entries
  (`command_catalog.zia`) update descriptions accordingly.
- The two `commandInputOverlay` flows (`COMMAND_INPUT_WORKSPACE_SYMBOLS`,
  `COMMAND_INPUT_GOTO_LINE`) are REMOVED from `main.zia` (`:520-523,532-537`)
  and their input kinds retired from `ui/command_input.zia` — the overlay stays
  for its other users (Add Watch, rename, extract names, output filter).
- Placeholder text cycles per mode ("Type `>` for commands, `#` for symbols,
  `:` for line...") via `SetPlaceholder` on mode change.

### 3.3 Performance guardrails

- Repopulation cap 50 rows/mode; `Clear`+`AddCommand` churn per keystroke is
  bounded (plan 24 keeps the widget stable under open-state repopulation).
- Symbols mode shows a "Searching…" synthetic row while the debounced provider
  runs; stale-result rejection by query-generation check before applying
  (pattern: completion's stale-result rejection, `docs/status.md` Zia list).

## 4. Implementation steps

1. Refactor `handleWorkspaceSymbolsText` → collector + wrapper
   (`search_commands.zia`); confirm `handleGoToLineText`'s parser is callable
   with a raw string (it is — it takes the overlay text, `main.zia:521`).
2. Mode router + pump in `command_palette_controller.zia`; wire into
   `main.zia` beside the existing palette block.
3. Selection routing by id prefix (extend the `WasSelected` branch,
   `main.zia:384-399`).
4. Entry-point prefills + catalog updates + overlay-flow removal.
5. Probe `viperide/src/probes/palette_modes_probe.zia`: drive queries via
   plan 24's `SetQuery` (no synthetic keystrokes needed): set `">for"` →
   command rows; `"#main"` → symbol rows (temp workspace with a known symbol);
   `":42"` → goto row, select → cursor at 42; `"proj"` → file rows; mode
   switches mid-open; stale-symbol-result rejection (set query twice fast).
   Register `LABELS "zia;viperide;palette"`.
6. Update probes that used the workspace-symbols overlay flow (grep probes for
   `WORKSPACE_SYMBOLS` / goto-line overlay usage — `console_search_probe.zia`,
   `phase2_phase3_probe.zia` candidates) to the palette path.
7. Manual: muscle-memory pass — all four modes, prefix switching mid-typing,
   Esc, Enter, empty results, huge workspace symbol latency feel.
8. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/app/command_palette_controller.zia` — router + pump.
- `viperide/src/commands/search_commands.zia` — symbols collector refactor.
- `viperide/src/commands/quick_open_commands.zia` — row reuse hooks (minor).
- `viperide/src/commands/edit_commands.zia` — goto-line reuse (minor).
- `viperide/src/main.zia` — pump call, selection routing, overlay-flow removal.
- `viperide/src/ui/command_input.zia` — retire two input kinds.
- `viperide/src/commands/command_catalog.zia` — entries/labels/shortcuts.
- `viperide/src/probes/palette_modes_probe.zia` — **new**; probe updates;
  `src/tests/CMakeLists.txt`.
- `viperide/docs/status.md` — navigation section update.

## 6. Testing

Probe (step 5) covers all four modes + routing + staleness; updated legacy
probes keep the collector/parser logic covered; manual pass for latency feel.

## 7. Acceptance criteria

- One palette serves files/commands/symbols/goto-line with live results and
  prefix switching mid-session; selections navigate correctly.
- Old shortcuts land in the right mode; the two retired overlay flows are gone
  with no dangling commands.
- Symbol queries are debounced and stale results never apply.
- All probes green.

## 8. Repo rules (read before starting)

- Zia-only plan (runtime surface from plan 24): rebuild with
  `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; command metadata lives in
  `command_catalog.zia`; behavior in feature command modules (ownership rules,
  `viperide/docs/architecture.md`).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
