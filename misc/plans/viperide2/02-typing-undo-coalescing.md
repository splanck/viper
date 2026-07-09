# Plan 02 — Typing undo coalescing (word/time-grouped undo)

## 1. Objective & scope

Make undo operate on natural typing units instead of single characters. Today
typing "hello world" creates 11 separate undo operations; every mainstream
editor coalesces consecutive typing into word/burst-sized units. Implement
coalescing in the CodeEditor's history layer with the standard break rules.

**In scope:** `vg_codeeditor_history.inc` coalescing logic + break-rule hooks in
the editing/input paths; C tests; no API changes required (behavioral change
inside `vg_codeeditor_undo`'s unit size).

**Out of scope:** per-document history retention (plan 01), redo semantics
changes (redo already replays whatever units exist), grouping of programmatic
multi-edit operations (already handled via `edit_history_begin_group`).

## 2. Current state (verified anchors)

- History model: `vg_edit_history_t` stores an array of `vg_edit_op_t`
  (`src/lib/gui/include/vg_ide_widgets_editor.h:42-75`) with `group_id`
  ("Non-zero if part of a group", `:63`) and grouping state
  (`is_grouping`, `current_group`, `next_group_id`, `:72-74`).
- Grouping exists but is used **only** for multi-cursor edits:
  `edit_history_begin_group` is called at exactly one site, guarded by
  `target_count > 1` (`src/lib/gui/src/widgets/vg_codeeditor_editing.inc:471-474`).
- Group creation/close helpers: `edit_history_begin_group`
  (`vg_codeeditor_history.inc:192-197`), `edit_history_end_group` (`:199+`),
  push path sets `op->group_id` when grouping (`:154-156`).
- Character input path: `VG_EVENT_KEY_CHAR` → `vg_codeeditor_insert_text` with a
  1-char string (`vg_codeeditor_input.inc:1913-1948`); Enter/Tab/Backspace/Delete
  each call their own editing helpers (`:1861-1891`).
- Undo/redo walk `current_index` over `operations` — grep how group replay works
  in `vg_codeeditor_history.inc` (undo of a grouped op must already replay the
  whole group for multi-cursor; reuse that mechanism).

## 3. Design

### 3.1 Approach: merge-into-previous-op (not groups)

Two viable designs: (a) wrap typing bursts in groups, (b) merge consecutive
single-char INSERT ops into the previous op's `new_text`. Choose **(b) merge**:

- Groups are open/close brackets driven by callers; typing has no natural
  "close" event, so (a) needs timeout-driven closing — awkward in an
  event-driven widget.
- Merging keeps one `vg_edit_op_t` whose `new_text` grows, `end_line/end_col`
  and `cursor_*_after` advance — undo/redo code paths need zero changes.

Add to `vg_edit_history_t`:

```c
uint64_t last_insert_time_ms;   // wall time of last merged insert
bool     coalescing_enabled;    // default true; tests can disable
```

New internal function in `vg_codeeditor_history.inc`:

```c
/// Try to merge a single-character insert at (line,col) into the previous op.
/// Returns true when merged (caller skips pushing a new op).
static bool edit_history_try_coalesce_insert(vg_edit_history_t *h,
                                             int line, int col,
                                             const char *text,
                                             uint64_t now_ms);
```

### 3.2 Merge conditions (all must hold)

1. Previous op exists, `current_index == count` (nothing undone — never merge
   across an undo boundary), previous op is `VG_EDIT_INSERT` with
   `group_id == 0` and `cursor_id == 0` (never touch multi-cursor groups).
2. Insertion point continues the previous op exactly:
   `line == prev->end_line && col == prev->end_col`.
3. The inserted text is a single ASCII/UTF-8 codepoint **that is not a
   newline** (Enter always breaks; `codeeditor_insert_newline_with_indent` is a
   separate path anyway — do not add coalescing there).
4. Word-boundary rule: if the new char is whitespace and the previous op's last
   char is non-whitespace → break (start a new op). Effect: "hello world"
   becomes two units ("hello ", "world") — pick the VS Code convention:
   whitespace *joins* the previous word's op; the break happens on the first
   non-space after whitespace. Implement exactly: break when
   `is_word_char(new) && !is_word_char(prev_last) && prev_last != ' '` is too
   clever — keep it simple and predictable:
   **break when transitioning from whitespace to non-whitespace.**
5. Time window: `now_ms - last_insert_time_ms <= 800` (constant
   `CODEEDITOR_UNDO_COALESCE_MS 800` next to the other `CODEEDITOR_*` constants).
6. Size cap: `strlen(prev->new_text) < 64` bytes — bound memmove cost and unit size.

Backspace coalescing (symmetric, optional but cheap and expected): consecutive
single-char BACKSPACE deletes at a receding position merge into one DELETE op,
same time window, prepending to `old_text`. Break on word boundary similarly.
Guard: only when the previous op is a DELETE created by backspace (track with a
`bool from_backspace` bit — add to `vg_edit_op_t` only if there is a spare way
to encode; otherwise infer from position adjacency, which is sufficient).

### 3.3 Explicit break points

Force the next insert to start a new op (`last_insert_time_ms = 0` or a
`force_break` flag) on: cursor movement of any kind (arrow/click/goto), undo,
redo, paste, cut, save (`vg_codeeditor_clear_modified`), selection creation,
focus loss. Implementation: add
`static void edit_history_break_coalescing(vg_edit_history_t *h)` and call it
from those code paths in `vg_codeeditor_input.inc` (navigation cases already
funnel through `codeeditor_finish_keyboard_navigation`, mouse-down case at
`:1604`) and `vg_codeeditor_api.inc` (undo/redo/set_cursor/set_selection/paste).

### 3.4 Time source

The widget has no clock dependency today; blink uses accumulated `dt`
(`vg_codeeditor_tick`). Use the same pattern: add
`uint64_t typing_clock_ms` to the editor, advanced in `vg_codeeditor_tick` by
`dt*1000`, passed into the history call. This keeps the widget deterministic
under tests (tests control time by calling tick) and avoids platform clock
calls in the gui lib.

## 4. Implementation steps

1. Add fields/constants; implement `try_coalesce_insert` + `break_coalescing`
   in `vg_codeeditor_history.inc`.
2. Hook the single-char insert path: in the internal push-op flow used by
   `vg_codeeditor_insert_text` (find the push site in
   `vg_codeeditor_editing.inc` — the non-grouped `target_count == 1` path),
   attempt coalesce before pushing.
3. Wire break points (§3.3).
4. Backspace merge (same file, symmetric).
5. C tests in `src/lib/gui/tests/` (drive via public API + tick):
   - type 5 chars → one undo restores all 5;
   - "hello world" → two undos;
   - type, wait >800ms (tick), type → two units;
   - type, click elsewhere, type → two units;
   - type, undo, type → undo boundary respected (no merge across);
   - multi-cursor typing unaffected (still grouped per existing behavior);
   - backspace×4 → one undo restores all 4.
6. Verify IDE feel: `./scripts/build_ide.sh`, type a paragraph, undo in
   word-sized steps; Ctrl+Z after paste undoes exactly the paste.
7. Full no-skip build + test run (`ctest --test-dir build -L viperide` included —
   `editor_hot_path_probe` and any probe that counts undo steps must be checked;
   if a probe asserts per-char undo granularity, update the probe and say so in
   the summary).

## 5. Files to modify

- `src/lib/gui/src/widgets/vg_codeeditor_history.inc` — core logic.
- `src/lib/gui/src/widgets/vg_codeeditor_editing.inc` — insert push-site hook.
- `src/lib/gui/src/widgets/vg_codeeditor_input.inc` — break points.
- `src/lib/gui/src/widgets/vg_codeeditor_api.inc` — break points + tick clock.
- `src/lib/gui/include/vg_ide_widgets_editor.h` — struct fields (+ header docs).
- `src/lib/gui/tests/` — new C test file.

## 6. Testing

C unit tests (step 5) are the primary net — they encode every break rule.
No new Zia probe needed: the behavior is widget-internal, and plan 01's
`undo_across_tabs_probe` exercises undo through the runtime surface.

## 7. Acceptance criteria

- Typing a word then Ctrl+Z removes the whole word (≤2 units per short phrase).
- No merge across undo boundaries, cursor moves, pastes, or >800ms pauses.
- Multi-cursor undo behavior unchanged; all existing gui/viperide tests pass.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after the C changes + full build.
- Full Viper header on modified C files (already present; keep them updated).
- 100% cross-platform; no platform code involved (deterministic tick clock, not
  OS time).
- Zero external dependencies.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
