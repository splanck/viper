# Plan 11 — Terminal and SCM Depth

Date: 2026-07-17 · Track: D · Loop: Zia · Size: M-L

## 1. Objective

A terminal that runs vim/less/htop, and a Source Control panel with commit
history, concurrent jobs, and credential prompting.

## 2. Terminal scope

Screen-model rewrite in `src/zannastudio/src/terminal/`
(`terminal_session.zia` / `terminal_controller.zia`), replacing the
line-append OutputPane model with a cell-grid emulator:

- Main + alternate screen grids; DECSET/DECRST 1049 (alt screen), 25
  (cursor), 2004 (bracketed paste).
- Cursor addressing (CUP/CUU/CUD/CUF/CUB/CNL/CPL/CHA/VPA), erase (ED/EL),
  insert/delete lines and chars (IL/DL/ICH/DCH), scroll regions (DECSTBM),
  full SGR (16/256/truecolor), tabs (HT/TBC/HTS), save/restore cursor.
- Rendering through the existing OutputPane surface upgraded to grid paint
  (or a dedicated grid painter if OutputPane's line model can't host it —
  decide at implementation start with a spike; either way damage-region
  friendly).
- **Acceptance boundary is a pinned sequence table** derived from what
  vim, less, and htop actually emit (captured fixtures) — VT completeness
  beyond the table is future work.

## 3. SCM scope

- Commit history/log view: lazy-paged `git log` parsing
  (`scm/scm_git.zia`), list with author/date/subject, per-commit file list,
  diff-on-select through the Plan 07 diff view.
- Multi-job queue: serialize git operations with visible progress and
  cancelation (today: one active job, blocking push/pull).
- Credential prompting: git runs under the PTY; detect
  username/password/passphrase prompt patterns and surface an in-app secure
  input (masked TextInput) — no external askpass helper binary (zero-dep).

## 3a. As-built record (2026-07-18)

- **Terminal: extension, not rewrite.** Recon found the OutputPane terminal
  mode already held a cursor-addressed cell/line model with SGR 16/256/
  truecolor, EL/ED, full cursor movement (CUU/CUD/CUF/CUB/CNL/CPL/CHA/CUP/
  VPA), save/restore, alt screen (47/1047/1049), and xterm-style key
  encoding — §2's "replace the line-append model" premise was stale. The
  gap was closed in place (`src/lib/gui/src/widgets/vg_outputpane.c`):
  - **Scroll regions**: DECSTBM (`CSI r`; full-screen margins normalize to
    unset so primary scrollback keeps growing after vim exits), SU/SD,
    IL/DL, and region-aware LF / IND / RI (bottom-margin scroll keeps
    status rows pinned). Row moves swap whole line structs so segment
    allocations travel with their text.
  - **In-row edits**: ICH/DCH/ECH on the cell buffer.
  - **Tabs**: HT is now a pure cursor move (no glyph writes) over a real
    tab-stop bitset; HTS (`ESC H`) and TBC (`CSI g`, 0/3) manage stops.
  - **Modes**: DECSET/DECRST ?25 (caret suppression, honored by the paint
    path), ?2004 (bracketed paste — Cmd+V / Ctrl+Shift+V paste wraps in
    `ESC[200~`/`201~`; plain Ctrl+V still reaches the child for vim
    blockwise), ?1 (application cursor keys → SS3 arrows/Home/End).
  - **Replies**: DSR 5/6 (status + cursor-position report) and primary/
    secondary DA answered onto the child-input queue.
  - **SGR 7/27** reverse video (htop bars) with theme-background
    substitution; **RIS** resets the new state.
  - **Clipboard chords**: Cmd+C / Ctrl+Shift+C copy the selection; plain
    Ctrl+C stays SIGINT.
  - Fixed while here: `expand`-side hazard equivalents — none; but HT
    previously overwrote existing cells with blanks (data loss on tab-over).
- **SCM depth** (`scm_git.zia` / `scm_view.zia`):
  - **History**: `StartLog` (RS/US-separated fields so any subject parses),
    `ParseLog`, `StartCommitFiles`/`ParseCommitFiles`, `StartShowAt`;
    History/More buttons page 50 at a time; selecting a commit lists its
    files; selecting a file chains parent→commit content and opens the
    plan-07 side-by-side diff view. `<- Back` returns to the log.
  - **Job queue**: actions clicked while git runs enqueue (bounded, depth
    shown in the status label) instead of being rejected; buttons poll
    before the pump so clicks land during long jobs; Cancel kills active +
    queued; switching repos drops everything stale.
  - **Credential prompts**: push/pull now run on a PTY (`GitPtyJob`) with
    prompting enabled; output streams live into the panel; `DetectPrompt`
    (pure, fails open) surfaces Username/Password/passphrase/host-key
    questions in a hidden credential row with `TextInput.SetPassword`
    masking; answers write to the PTY (child controls echo, so secrets are
    not echoed). Non-network git keeps the prompt-disabled environment.
  - Fixed while here: the Side-by-Side button check was nested inside the
    Stage button's click block (unreachable); it is now a top-level action.
- **Tests**: `test_vg_outputpane_term.c` (pinned sequence table: grid edits,
  tabs, region scrolling incl. pinned status row, modes, replies, RIS,
  scrollback-flow-unchanged) under the `gui` label;
  `terminal_altscreen_probe.zia` (RT-integrated alt-screen isolation,
  region scroll, IL/DL, ICH/DCH, CPR reply, mode swallowing);
  `scm_history_probe.zia` (seeded repo: paged log incl. tab+quote subject,
  per-commit files, both diff halves, added-file parent miss, prompt
  detector). Labels: gui 25 green, zannastudio 49 green.
- **Manual gate remaining (owner)**: run vim, less, and htop in the built
  IDE terminal on each platform; exercise a real credentialed push.

## 4. Runtime surface

None expected (PTY and process surfaces exist).

## 5. Tests / verification (exit gate)

- Parser core is PTY-independent: probe `terminal_altscreen_probe.zia`
  feeds scripted escape sequences and asserts grid state (exhaustive
  sequence-table coverage, including the vim/less/htop fixtures).
- `scm_history_probe.zia`: temp repo with seeded commits → paged log,
  per-commit files, diff selection.
- Manual gate: run vim, less, and htop in the built IDE terminal on the host
  platform.
- Incremental build + targeted ctest; `build_ide.sh` iteration loop.

## 6. Risks

- VT emulation long tail — bounded by the pinned sequence table.
- Grid rendering perf in the damage-region system — reuse the CodeEditor's
  per-line damage pattern.
- Credential prompt detection is heuristic — fail open (raw PTY passthrough
  visible in the terminal) so nothing is ever blocked on the heuristic.
