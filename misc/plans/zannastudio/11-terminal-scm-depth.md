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
