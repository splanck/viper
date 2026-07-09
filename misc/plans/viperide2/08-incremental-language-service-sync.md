# Plan 08 — Incremental text sync to language services

## 1. Objective & scope

Stop shipping the entire document to the language-service layer on every
semantic request. Today each diagnostics/completion/hover/token job materializes
the full buffer as a fresh string (every keystroke bumps the revision, and the
per-revision snapshot cache then refills with a full O(document) copy). For a
5k-line file that is megabytes of copying per keystroke burst, on the UI thread.

Design goal: the C++ language-service side keeps a persistent per-path document
mirror, updated by **edit deltas** (range + replacement) pushed from the editor;
semantic jobs read the mirror instead of receiving a full string parameter.

**In scope:** delta capture on the Zia/editor side, mirror + delta application
on the `rt_zia_completion.cpp` side, new runtime entry points, fallback full-sync
path, counters proving the win, probes.

**Out of scope:** incremental *parsing/sema* inside the Zia compiler (the
frontend still re-analyzes the mirror text; that is a compiler project, not an
IDE one), BASIC service (string-based today; can adopt later via the same
entry points).

## 2. Current state (verified anchors)

- Snapshot caching exists but is per-revision:
  `EditorEngine.GetTextSnapshot()` caches `cachedTextSnapshot` keyed by
  `cachedTextRevision` (`viperide/src/editor/editor_engine.zia:39-47,157-160`) —
  it deduplicates *within* one revision only; every keystroke is a new revision
  → new full `editor.Text` materialization (`rt_codeeditor_get_text`,
  `runtime.def:2413`).
- Consumers passing full text per call: completion
  (`ZiaCompletion.CompleteForFile(text, sourcePath, line, col)` —
  `viperide/src/editor/completion.zia` header notes), diagnostics, hover,
  signature, symbols, semantic tokens, inlay hints (all under
  `viperide/src/editor/`, each calling `engine.GetTextSnapshot()`).
- The C++ service: `src/frontends/zia/rt_zia_completion.cpp` (2575 lines),
  async `SemanticJob` machinery on `std::thread` (`:46,327-548`), stub at
  `src/runtime/core/rt_zia_completion_stub.c`.
- The widget already maintains a parallel text-engine mirror:
  `document_buffer` ("Shared text-engine mirror used for full-document
  queries", `vg_ide_widgets_editor.h:146`) — evidence that mirror-style sync is
  the intended architecture.
- Copy cost is already instrumented: `full_text_copies` /
  `full_text_copy_bytes` counters (`vg_ide_widgets_editor.h:108-109`) with
  probe assertions in `viperide/src/probes/editor_hot_path_probe.zia`.
- Edits flow through a small set of widget entry points:
  `vg_codeeditor_insert_text`, `delete_selection`, replace-targets path in
  `vg_codeeditor_editing.inc` (all funnel into internal
  insert/delete-at-range helpers), plus undo/redo replaying ops.

## 3. Design

### 3.1 Delta capture at the widget layer

The single choke point is the editing core: every content mutation (typing,
paste, delete, undo, redo, programmatic ApplyEdit) passes through the internal
insert/delete range helpers in `vg_codeeditor_editing.inc`. Add a bounded
**edit journal** to the editor (or buffer, if plan 21 landed — coordinate; the
journal belongs with document state):

```c
typedef struct vg_edit_delta {
    uint64_t revision;          // revision AFTER applying this delta
    int start_line, start_col;  // byte columns, 0-based
    int end_line, end_col;      // range replaced (empty for pure insert)
    char *text;                 // replacement text (owned; "" for delete)
} vg_edit_delta_t;
// ring buffer, capacity 256; on overflow set journal_overflowed = true
```

Runtime accessors (bridge in `rt_gui_codeeditor.c`):

```c
RT_FUNC(GuiCodeEditorTakeDeltas, rt_codeeditor_take_deltas, "Viper.GUI.CodeEditor.TakeDeltasJson", "str(obj,i64)")
```

`TakeDeltasJson(sinceRevision)` returns a compact JSON array of deltas newer
than `sinceRevision` and prunes consumed entries; returns `"overflow"` when the
ring wrapped past `sinceRevision` (caller must full-sync). JSON keeps the
signature simple (one string) and matches the debug adapter's precedent of
JSON-over-string; the volume is tiny (deltas are keystrokes).

### 3.2 Service-side mirror

In `rt_zia_completion.cpp`, add a document store keyed by source path:

```cpp
struct DocumentMirror { std::string text; uint64_t revision; };
// map<path, DocumentMirror> guarded by the existing service mutex pattern
```

New runtime functions (registered like the existing `Viper.Zia.*` entries —
find the RT_FUNC block for `Viper.Zia.Completion` in runtime.def and extend):

```c
RT_FUNC(ZiaDocSyncFull,  rt_zia_doc_sync_full,  "Viper.Zia.Document.SyncFull",  "void(str,str,i64)")   // path, text, revision
RT_FUNC(ZiaDocSyncDelta, rt_zia_doc_sync_delta, "Viper.Zia.Document.SyncDelta", "i1(str,str,i64)")    // path, deltasJson, endRevision → false = need full
RT_FUNC(ZiaDocClose,     rt_zia_doc_close,      "Viper.Zia.Document.Close",     "void(str)")
```

`SyncDelta` applies deltas in order to the mirror (line/col → offset conversion
against the mirror's current text; reject and return false on any mismatch,
e.g. revision gap). Then add mirror-reading variants of the hot query entry
points, e.g. `CompleteForFileAt(path, line, col)` (no text parameter) — same
for diagnostics/hover/signature/symbols/tokens/inlay jobs, following each
existing function's signature minus the text argument. Existing string-taking
entry points remain untouched (used by external tools/probes and as fallback).

**Leaf-name check:** `Viper.Zia.Document` — class leaf `Document` must be
globally unique across runtime classes; if taken (check
`./scripts/check_runtime_completeness.sh` / grep), use `Viper.Zia.SourceDoc`.

### 3.3 IDE adoption

- `EditorEngine` gains `SyncActiveDocument(path)`: on each semantic-job
  dispatch (the controllers already funnel through
  `language_tool_frame.zia`/`scheduler.zia`), before queueing, push deltas:
  `TakeDeltasJson(lastSyncedRevision)` → `SyncDelta`; on `"overflow"`/false →
  `SyncFull(path, GetTextSnapshot(), revision)`.
- First open / tab switch / external reload → `SyncFull`.
- Controllers switch to the `-At` query variants one at a time (diagnostics
  first — highest volume; then tokens, completion, hover, signature, symbols,
  inlay). Each switch is independently revertible.
- `DocumentManager` close → `Viper.Zia.Document.Close(path)`.

### 3.4 Verification of the win

`full_text_copies`/`full_text_copy_bytes` (already tracked) must drop to ~0
during a typing burst after adoption; `editor_hot_path_probe.zia` gets a new
assertion: type N chars with diagnostics enabled → snapshot copies ≤ 1.

## 4. Implementation steps

1. Widget edit journal + `TakeDeltasJson` (+ overflow rule + tests at the C
   level: apply edits, take deltas, replay them onto a copy of the original
   string, assert equality with `vg_codeeditor_get_text`).
2. `runtime.def` entries + service-side mirror + `SyncFull/SyncDelta/Close`
   with a C++-level replay test (feed deltas, compare mirror to reference).
   Stub-runtime no-op counterparts in `rt_zia_completion_stub.c`.
3. Mirror-reading query variant for **diagnostics** only; adopt in
   `diagnostics.zia`; probe `intellisense_probe.zia` extended to cover
   delta-synced diagnostics equivalence (same source, same diagnostics via both
   paths).
4. Roll out to the remaining controllers (tokens, completion, hover, signature,
   symbols, inlay hints) — mechanical, one commit-sized step each.
5. Hot-path assertion in `editor_hot_path_probe.zia` (§3.4).
6. `./scripts/check_runtime_completeness.sh`; full no-skip build + test run.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h`,
  `src/lib/gui/src/widgets/vg_codeeditor_editing.inc` (journal capture),
  `_lifecycle.inc` (init/free), `_api.inc` (undo/redo journaling — undo/redo
  are content mutations and MUST journal their effects too).
- `src/runtime/graphics/gui/rt_gui_codeeditor.c` — TakeDeltasJson bridge.
- `src/frontends/zia/rt_zia_completion.cpp` — mirror + sync + `-At` variants.
- `src/runtime/core/rt_zia_completion_stub.c` — stubs.
- `src/il/runtime/runtime.def` — new entries (editor + zia service).
- `viperide/src/editor/editor_engine.zia`, `diagnostics.zia`,
  `semantic_tokens.zia`, `completion.zia`, `hover.zia`, `signature.zia`,
  `symbols.zia`, `inlay_hints.zia`, `app/language_tool_frame.zia`,
  `core/document_manager.zia` — adoption.
- Probes: `editor_hot_path_probe.zia`, `intellisense_probe.zia`.

## 6. Testing

- C journal replay test + C++ mirror replay test are the correctness core
  (delta math must be byte-exact, including UTF-8 multibyte edits and CRLF-free
  invariants — the editor stores lines without terminators; the mirror joins
  with `\n` exactly like `vg_codeeditor_get_text`).
- Equivalence probe: both sync paths produce identical diagnostics/tokens for
  a scripted edit sequence.
- Hot-path counter assertion proves the perf goal.

## 7. Acceptance criteria

- Typing burst with live diagnostics: full-buffer copies ≤ 1 per burst
  (counter-verified), where today it is one per keystroke-revision job.
- All semantic features byte-identical results via the mirror path.
- Overflow/fallback path exercised by test (force a tiny journal in a test
  build knob or type >256 edits) and recovers via SyncFull.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD`/registration
  entries; run `./scripts/check_runtime_completeness.sh`; unique class leaf names.
- Full Viper header on new/modified C/C++ files.
- 100% cross-platform; no platform code involved.
- Zero external dependencies (in-tree JSON only).
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
