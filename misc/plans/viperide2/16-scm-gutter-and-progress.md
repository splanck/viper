# Plan 16 — SCM gutter change markers + job progress

## 1. Objective & scope

Two connected upgrades to daily Git use:

1. **Editor gutter diff markers** — added/modified/deleted line indicators
   (thin colored bars) for the active file vs HEAD, updating as you type-and-save.
2. **SCM job progress + queueing** — replace the single-`activeJob` +
   static-text model with a small job queue and a visible progress row
   (spinner/status + cancel) so push/pull/fetch don't freeze the view's
   usefulness or serialize silently.

**In scope:** async `git diff` parsing in `scm_git.zia`, per-document diff
state + gutter painting (small CodeEditor extension for bar-style markers),
refresh triggers (save/stage/commit/branch-switch/external change), SCM job
queue + progress UI, probes.

**Out of scope:** credential prompting UI (surface stderr as today), inline
diff hunks/peek view, staging from the gutter, conflict-resolution workflows.

## 2. Current state (verified anchors)

- SCM view serializes one job: `expose scm_git.GitJob activeJob`
  (`viperide/src/scm/scm_view.zia:28,105-136`) with "Waiting for <kind>..."
  status text (`:105,273`); architecture doc confirms "one active Git job at a
  time" (`viperide/docs/architecture.md` Source Control; `docs/status.md:307-317`
  lists progress/credential gaps).
- `scm_git.zia` (677 lines) is an async argv wrapper: resolves `git`, captures
  stdout/stderr/exit via `Viper.System.Process`, parses porcelain v2 status,
  exposes cancellation (`architecture.md` SCM section).
- Gutter substrate: `CodeEditor.SetGutterIcon(line, icon, slot)` /
  `ClearGutterIcon(s)` (`runtime.def:2801-2806,9141-9144`); C-side icons render
  as colored discs or RGBA images (`vg_gutter_icon`,
  `vg_ide_widgets_editor.h:327-335` — `type` field: 0=breakpoint, 1=warning,
  2=error, 3=info). Discs are wrong for diff markers — VS Code-style **bars**
  need a new icon style.
- Breakpoints + diagnostics already share the gutter
  (`debug_commands.PumpGutter`, `main.zia:368`; diagnostics minimap/inline
  marks, `editor/diagnostics.zia:23,334-341`) — collision policy needed.
- Refresh triggers available: save flows in `file_commands.zia`, SCM
  stage/commit completions in `scm_view.zia`, branch ops, and the file watcher
  (`app/file_watch_controller.zia`).
- Minimap markers API exists (`vg_minimap_marker`,
  `vg_ide_widgets_editor.h:731-737`) — diff colors can mirror into the minimap
  the way diagnostics do (`diagnostics.zia:23`).

## 3. Design

### 3.1 Gutter bar style (small C extension)

Add marker style to gutter icons: extend `vg_gutter_icon` with
`int style` (0 = disc [default, unchanged], 1 = bar). Bar paint: 3px-wide
vertical bar at the gutter's left edge spanning the line height (deleted-lines
marker: small left-pointing triangle at the boundary line). Runtime:

```c
RT_FUNC(GuiCodeEditorSetGutterBar, rt_codeeditor_set_gutter_bar, "Viper.GUI.CodeEditor.SetGutterBar", "void(obj,i64,i64,i64)")
// (line, colorRGB, slot) — style=bar; reuse existing clear APIs (slot-based)
```

Reserve a dedicated slot number for SCM (breakpoints and diagnostics use their
own `type` slots today — read `debug_commands.PumpGutter` +
`diagnostics.zia:341` to pick a non-colliding slot value and document the slot
registry in a comment in `editor_engine.zia`). Bars render beside discs, not
instead (different x-offset), so a modified line with a breakpoint shows both.

### 3.2 Diff computation

New module `viperide/src/scm/scm_diff.zia`:

- Runs `git diff --no-color --no-ext-diff --unified=0 -- <relpath>` (unstaged
  vs index) OR `git diff HEAD --unified=0 -- <relpath>` (working tree vs HEAD —
  choose HEAD; simpler mental model, matches VS Code default) through the
  existing `scm_git` async job machinery.
- Parses only `@@ -a,b +c,d @@` hunk headers (`--unified=0` makes ranges exact):
  `d>0 && b==0` → added lines c..c+d-1; `b>0 && d==0` → deletion marker at line
  c boundary; both >0 → modified lines c..c+d-1. No content parsing needed.
- Produces `List of {startLine, endLine, kind}` (kind: added/modified/deleted).
- **Unsaved edits shift lines.** v1 policy: markers are computed against the
  *saved* file and refreshed on save/auto-save; while the buffer is dirty,
  markers may be off by the unsaved delta — acceptable v1 (note in code).
  (Do NOT attempt live re-mapping via edit deltas in v1; plan 08's journal
  could enable that later.)

### 3.3 Controller + triggers

New `scm/scm_gutter_controller.zia`: owns per-active-document diff state.
Triggers a diff job when: active document changes (tab switch, if file is in a
git repo), file saved (incl. auto-save — hook the same place
`fileWatchCtrl.ResetPendingReload()` is called after save, `main.zia:613`),
SCM operations complete (stage/unstage/commit/pull/branch — hook scm_view job
completion), external change reload. Debounce 300ms via `editorScheduler`'s
pattern or a `Threads.Debouncer`. Applies results:
clear SCM slot → `SetGutterBar` per range → mirror to minimap markers.
Skip silently when: no repo (`scmView` already detects repo-ness), file
untracked (whole-file "added" — show green bars for all lines? No: untracked →
no markers, matches VS Code), file outside repo root.

### 3.4 SCM job queue + progress

In `scm_view.zia`:

- Replace single `activeJob` with `List pendingJobs` + `activeJob`; enqueue
  instead of reject; pump starts the next job on completion. Cap queue at 8
  with status-bar warning (runaway protection).
- Progress row in the SCM view (host `scmViewHost`): `GUI.ProgressBar`
  (exists — `vg_progressbar.c`) in indeterminate mode (check for an
  indeterminate API; else a lightweight text spinner cycling chars via a frame
  counter) + label "<kind> running…" + the existing cancel affordance
  (`status.md:311` — jobs already cancelable).
- Push/pull stderr progress lines (git writes progress to stderr): append last
  line to the label as it streams (the job machinery already captures stderr
  incrementally — verify; if capture is end-only, keep label static and note it).
- Diff jobs from §3.3 run at LOW priority: status/stage/commit jobs jump ahead
  of queued diff jobs (simple two-tier queue).

## 4. Implementation steps

1. C gutter-bar style + RT_FUNC/RT_METHOD + completeness check + C paint test
   (classification-level: icon with style=bar stores/paints without trap;
   visual check manual).
2. `scm_diff.zia` hunk-header parser + unit-style probe coverage (pure parsing:
   feed captured `git diff -U0` outputs incl. renames/binary/no-newline cases —
   binary files → no markers).
3. `scm_gutter_controller.zia` + triggers + slot registry doc + minimap mirror.
4. Job queue + progress row in `scm_view.zia`.
5. Probes: `viperide/src/probes/scm_probe.zia` exists (378 lines, scripted git
   repos) — extend: init temp repo, commit a file, modify lines 3-4 + append
   line, save, run the controller's diff cycle synchronously (probe mode), and
   assert marker ranges; enqueue two jobs and assert serialization order +
   progress state transitions. `LABELS "zia;viperide;scm"`.
6. Manual: real repo — edit/save (bars appear), stage/commit (bars clear),
   branch switch (bars recompute), push with progress label, queue a status
   refresh during a slow pull (queued, then runs), breakpoint+modified line
   coexistence, dark-theme colors (green/blue/red bars — pick from the
   existing TOOL_COLOR palette, `tool_panel_shell.zia:31-35`).
7. Full no-skip build + test run.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h`,
  `vg_codeeditor_paint.inc` (+ api/lifecycle touches) — bar style.
- `src/runtime/graphics/gui/rt_gui_codeeditor.c`, `src/il/runtime/runtime.def`.
- `viperide/src/scm/scm_diff.zia` — **new**.
- `viperide/src/scm/scm_gutter_controller.zia` — **new**.
- `viperide/src/scm/scm_view.zia` — queue + progress.
- `viperide/src/scm/scm_git.zia` — diff command support if needed.
- `viperide/src/main.zia` — controller wiring/pump + save hooks.
- `viperide/src/editor/editor_engine.zia` — slot registry comment.
- `viperide/src/probes/scm_probe.zia` — coverage; `src/tests/CMakeLists.txt` if split.
- `viperide/docs/status.md` — SCM section update (keep honest).

## 6. Testing

Parser probe cases (step 2) are the correctness core; scm_probe extensions
(step 5) cover the pipeline; manual pass (step 6) for visuals + queue feel.

## 7. Acceptance criteria

- Saved edits show correct added/modified/deleted gutter bars vs HEAD within
  ~300ms; clean file shows none; untracked/binary files show none.
- Bars coexist with breakpoints and diagnostics without collision.
- Two SCM operations issued back-to-back queue and both complete with visible
  progress; cancel works on the active one.
- All scm/editor probes green.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake (C gutter work),
  then `./scripts/build_ide.sh` for the Zia side.
- New runtime function: RT_FUNC + RT_METHOD +
  `./scripts/check_runtime_completeness.sh`.
- Full Viper header on modified C files; Zia modules per
  `viperide/docs/architecture.md` (SCM state stays in `src/scm/`).
- 100% cross-platform; git invocation is argv-based, no shell.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
