# Plan 20 — Crash-safe swap-file recovery

## 1. Objective & scope

Protect unsaved work against crashes. Today recovery is a base64 blob inside
the session INI, capped at 200K characters total, written only on clean exit
paths — a crash mid-session with large modified files loses everything since
the last save. Implement per-document swap files written continuously (on the
autosave debounce tick) for **every** modified buffer regardless of size, plus
a restore-on-launch prompt when swap files outlive their session, and cleanup
on save/close/clean-exit.

**In scope:** swap-file writer (atomic, debounced, per-document), stale-swap
detection + restore flow at startup, cleanup lifecycle, retirement of the
INI-blob mechanism for editable text buffers, probe.

**Out of scope:** undo-history persistence, versioned local history
("timeline"), scene/binary documents (text buffers only, as today).

## 2. Current state (verified anchors)

- Bounded INI recovery: `MAX_SESSION_RECOVERY_CHARS = 200000`
  (`viperide/src/core/session.zia:34`); per-tab `recovery` +
  `recoveryEncoding=base64` + `recoveryModified` keys written during
  `sessionMgr.Save` (`:84-89`); docs call it "bounded base64 crash-recovery
  text for modified editable buffers" (`architecture.md` Sessions).
- Session save runs on close request and clean shutdown
  (`main.zia:347-356` — `WasCloseRequested` → SaveEditorState + Save), NOT
  periodically — a hard crash loses all of it.
- Autosave debounce machinery already ticks in the loop:
  `AUTO_SAVE_IDLE_MS = 1500`, revision-driven `autoSaveDebounce.Signal()` /
  `get_IsReady` (`main.zia:107,329-330,603-619`) — the swap writer hooks the
  same rhythm and works even when `appSettings.autoSave` is **off** (autosave
  writes the real file; swap writes the shadow — independent policies).
- Settings dir as the storage root: `appSettings.settingsPath` already anchors
  breakpoints + session storage (`main.zia:233-242`) — platform-correct config
  path handled by `core/settings.zia`.
- Atomic-write precedent: "Save As uses same-directory temporary writes"
  (`docs/status.md:329`) — reuse the same write-temp-then-rename helper from
  `services/workspace_edits.zia` / `core/project_file_ops.zia` (locate via
  grep for the temp-write pattern).
- Restore-time UX precedent: missing-file conflict flow requires confirmation
  (`docs/status.md:327-329`); non-modal overlays available
  (`ui/explorer_actions.zia` confirmation style).
- Session identity: nothing currently marks "this session exited cleanly" —
  needed for stale-swap detection.

## 3. Design

### 3.1 Swap store layout

`<settingsPath>/recovery/` directory:

```
recovery/
  session.lock              # written at startup: pid + startTimeMs; deleted on clean exit
  <hash>.swp                # one per modified doc; content = header line + full text
```

- `<hash>` = stable hash of the absolute file path (use the runtime's string
  hash exposed to Zia — grep `Viper.String`/`Viper.Crypto` for a hash API; a
  simple FNV-1a in `services/` is fine if none exists; hex-encoded).
  Untitled documents: `untitled-<docIndex>-<startTimeMs>.swp`.
- Header line (single JSON object or `key=value` line — match INI style used
  elsewhere): original absolute path, revision, modified flag, timestamp,
  content byte length. Body: raw UTF-8 text (no base64 — full fidelity, half
  the size).
- Write atomically: `<hash>.swp.tmp` then rename (same-directory rename is
  atomic on all three platforms via the runtime's move primitive).

### 3.2 Writer lifecycle (new `core/recovery_store.zia`)

- `Tick(docMgr, engine, nowReadyFlag)` called from the autosave block region in
  `main.zia` (~`:603-619`): when the debouncer fires (reuse a second
  `Threads.Debouncer` with the same 1500ms idle, keyed on revision like
  autosave), write swaps for every open document whose `isModified` and whose
  swap-revision differs from the current revision. Active document reads
  through `engine.GetTextSnapshot()`; inactive documents already hold content
  in `doc.content` (kept fresh by `SaveEditorState` on switch) — after plan 21,
  read through `doc.buffer.get_Text` for inactive docs instead.
- Delete the swap on: successful save (hook `file_commands` save completion),
  tab close (discard-changes path), document becoming unmodified.
- `session.lock`: written at startup; removed in
  `lifecycle_controller.CleanupIde` (clean exit). Crash = lock present at next
  launch.
- Bounds: per-file no cap (that's the point); global sanity cap 64MB total —
  if exceeded, skip largest files with a status-bar warning (never silently).
- Perf: writes happen at most once per idle-debounce and only for changed
  revisions; a 1MB buffer written every idle period is fine. Never write on
  the frame path directly.

### 3.3 Startup restore flow

In startup (after settings load, before session restore, `main.zia:241-246`):

1. If `session.lock` exists → previous session crashed. Scan `recovery/*.swp`.
2. For each swap: compare against the on-disk file (if the disk file is newer
   AND identical → drop swap silently; if swap differs → candidate).
3. Candidates → non-modal prompt (overlay, listing files):
   "Recovered unsaved changes for N file(s) — Restore / Discard".
   Restore: open the document with swap content, marked modified (do NOT write
   to disk automatically). Discard: delete swaps.
4. Then write a fresh `session.lock` and proceed normally.
5. Retire the INI mechanism: `sessionMgr.Save` stops writing `recovery*` keys;
   `Restore` still READS them once for migration compatibility (one release),
   then the read path can be noted for removal. Update
   `MAX_SESSION_RECOVERY_CHARS` usage accordingly.

### 3.4 Interaction with session restore

Session restore re-opens tabs from disk; when a restored path has a live swap
chosen for restore, the swap content wins (applied after the tab opens).
Untitled-doc swaps restore as new untitled tabs. Keep ordering deterministic:
restore session first, then apply swap contents, then the welcome-vs-documents
decision (plan 18) runs on the final count.

## 4. Implementation steps

1. `core/recovery_store.zia`: paths, hashing, atomic write, delete, lock file,
   scan. Unit-style probe for pure pieces (hash stability, header round-trip).
2. Writer tick wiring in `main.zia` + delete hooks in save/close flows
   (`file_commands.zia` — find the post-save success points; `document_manager`
   close path).
3. Lock lifecycle in startup + `lifecycle_controller.CleanupIde`.
4. Startup scan + restore prompt overlay + apply-after-session-restore ordering.
5. INI retirement (write-side) + migration read.
6. Probe `viperide/src/probes/recovery_probe.zia`: simulate the full cycle
   in-process — create store, write swaps for fake modified docs, "crash"
   (skip cleanup, new store instance over same dir), assert detection +
   candidate filtering (disk-identical case dropped), restore application,
   cleanup on save/close. Register `LABELS "zia;viperide;session"`.
7. Manual: real crash test — `kill -9` the IDE with unsaved edits in a large
   (>200K, above the old cap) file; relaunch; restore; verify content exact.
   Also: clean exit leaves no lock/swaps; save deletes the swap immediately.
8. Full no-skip build + test run (session-related probes must stay green;
   update any probe asserting the INI `recovery` keys are written).

## 5. Files to modify

- `viperide/src/core/recovery_store.zia` — **new**.
- `viperide/src/main.zia` — tick wiring, startup scan ordering.
- `viperide/src/core/session.zia` — INI recovery retirement + migration read.
- `viperide/src/commands/file_commands.zia` — save/close delete hooks.
- `viperide/src/core/document_manager.zia` — close hook.
- `viperide/src/app/lifecycle_controller.zia` — lock removal.
- `viperide/src/ui/` — restore prompt overlay (reuse/extend an existing
  confirmation overlay module rather than a new widget file if it fits).
- `viperide/src/probes/recovery_probe.zia` — **new**; `src/tests/CMakeLists.txt`.
- `viperide/docs/status.md` — Data Safety section update (remove the "capped"
  gap honestly, state the new mechanism + its bounds).

## 6. Testing

Probe (step 6) covers the full logic cycle deterministically; the `kill -9`
manual test (step 7) is the real-world proof and must be performed and
reported. Session probes guard the migration path.

## 7. Acceptance criteria

- `kill -9` with unsaved multi-hundred-KB edits → next launch offers restore →
  content recovered byte-exact, marked modified, not auto-written to disk.
- Clean exit / save / close leave no stale swaps or lock.
- Writes are debounced (no per-keystroke disk I/O) and skipped for unmodified
  docs; global cap warning path works.
- Old-format INI recovery still restores once (migration), then stops being
  written.

## 8. Repo rules (read before starting)

- Zia-only plan: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; shared path/edit rules in `services/`.
- Atomic file operations must be cross-platform (same-dir rename via the
  runtime primitive — no platform ifdefs in Zia).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
