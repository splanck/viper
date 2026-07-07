# Plan 21 — `Viper.GUI.EditorBuffer`: detachable per-document editor state

## 1. Objective & scope

Introduce a first-class **editor buffer** object in the GUI runtime: a handle that
owns everything document-specific inside a `CodeEditor` (text lines, undo/redo
history, fold state, semantic tokens, highlight spans, cursor/selection/scroll,
revision counters) and can be detached from one editor widget and attached to
another — or held off-screen while a different buffer is displayed.

This is the enabling refactor for three ViperIDE plans:

- **Plan 01** — undo history must survive tab switches (today `SetText` destroys it).
- **Plan 10** — tab switching must stop re-splitting/re-highlighting the whole document.
- **Plan 17** — split editors must be able to show two views, potentially of the same buffer.

**In scope:** C-side `vg_editor_buffer_t` extraction inside `src/lib/gui`;
attach/detach/swap API on `vg_codeeditor_t`; runtime bridge + `runtime.def`
registration as `Viper.GUI.EditorBuffer` plus `CodeEditor.AttachBuffer` /
`CodeEditor.DetachBuffer`; C unit tests; a Zia probe.

**Out of scope:** actually using the new API in ViperIDE (plans 01/10/17 do that);
multiple *views* of one buffer simultaneously rendering (plan 17 decides whether
to share or clone; this plan must merely not preclude it); any change to editing
behavior.

## 2. Current state (verified anchors)

- `vg_codeeditor_t` (`src/lib/gui/include/vg_ide_widgets_editor.h:139-365`) holds
  document state directly as struct fields: `lines`/`line_count`/`line_capacity`
  (`:143-145`), `document_buffer` mirror (`:146`), cursor/selection (`:149-152`),
  scroll (`:155-158`), `history` (`:250`), `modified`/`revision`/`highlight_generation`
  (`:223-225`), layout cache (`:231-239`), highlight spans (`:277-286`), semantic
  tokens (`:291-300`), inlay hints (`:315-324`), gutter icons (`:327-335`), fold
  regions (`:344-353`), extra cursors (`:356-364`).
- History is created and destroyed only with the widget:
  `editor->history = edit_history_create()` in `vg_codeeditor_lifecycle.inc:177-179`,
  `edit_history_destroy(editor->history)` at `:208-210`.
- `vg_codeeditor_set_text` "replaces the entire document … and clears undo/redo
  history" (`vg_ide_widgets_editor.h:377-380`, implementation
  `vg_codeeditor_api.inc:59+`) — this is why every ViperIDE tab switch destroys undo.
- The editor implementation is split into include files under
  `src/lib/gui/src/widgets/`: `vg_codeeditor.c` (aggregator),
  `vg_codeeditor_{core,lifecycle,api,editing,history,input,paint}.inc`.
- The runtime bridge is `src/runtime/graphics/gui/rt_gui_codeeditor.c` (2186 lines)
  + `rt_gui_codeeditor_syntax.c`; registration lives in `src/il/runtime/runtime.def`
  (`Viper.GUI.CodeEditor` RT_FUNCs at `:2411+`, class block near `:9141`).
- There is **no** buffer, history-handle, or document-object API anywhere in
  `runtime.def` (verified by grep for `Undo|History|Buffer` — only
  `CodeEditor.Undo/Redo/CanUndo/CanRedo` exist).
- Sub-object handle precedent: `src/runtime/graphics/gui/rt_gui_subhandle.c`
  wraps non-widget C objects for Zia; `rt_zia_completion.cpp` shows the
  `rt_obj_new_i64(classId, size)` typed-handle pattern (`kSemanticJobClassId`,
  `rt_obj_is_instance`) for GC-managed handles.

## 3. Design

### 3.1 The split: buffer state vs. view state vs. derived state

Move into `vg_editor_buffer_t` (document truth + per-document UX state that users
expect to survive a tab switch):

| Field group | Fields (from `vg_codeeditor_t`) |
| --- | --- |
| Text | `lines`, `line_count`, `line_capacity`, `document_buffer` |
| History | `history` |
| Identity | `modified`, `revision`, `highlight_generation` |
| Caret/view restore | `cursor_line`, `cursor_col`, `selection`, `has_selection`, `extra_cursors[+count/cap]`, `scroll_x`, `scroll_y` |
| Folds | `fold_regions`, `fold_region_count`, `fold_region_cap`, `has_folded_lines` |
| Semantic overlay | `semantic_tokens[+count/cap/sorted]` |
| Diagnostics overlay | `highlight_spans[+count/cap/sorted]` |
| Syntax cache state | per-line members already live in `vg_code_line_t` (`colors`, `syntax_state_*`) — they travel with `lines` for free |

Stay on the widget (view-only / derived / rebuildable):

- font, colors/token colors, gutter config, display options, word_wrap flag
- layout cache (`layout_cache_*`), runtime content-width cache, pair-match cache,
  highlight line index (`highlight_line_*`) — all keyed by generation counters and
  rebuilt lazily; attach simply invalidates them
- inlay hints and gutter icons (IDE controllers re-set these per active document
  already), drag/scrollbar transient state, blink state

Rationale: everything that is expensive or user-visible-on-return travels with the
buffer; everything derivable stays view-local and is invalidated on attach. This
also keeps plan 17's door open: two views of one buffer differ exactly in the
view-only set. (True simultaneous two-views-one-buffer needs cursor state per
view; plan 17 resolves that by cloning or by view-cursor override — this plan
just documents the constraint.)

### 3.2 C API (new header section in `vg_ide_widgets_editor.h`)

```c
/// Opaque detachable document state for CodeEditor.
typedef struct vg_editor_buffer vg_editor_buffer_t;

vg_editor_buffer_t *vg_editor_buffer_create(const char *text);       // NULL text = one empty line
void vg_editor_buffer_destroy(vg_editor_buffer_t *buf);              // must not be attached
uint64_t vg_editor_buffer_get_revision(const vg_editor_buffer_t *buf);
bool vg_editor_buffer_is_modified(const vg_editor_buffer_t *buf);
char *vg_editor_buffer_get_text(vg_editor_buffer_t *buf);            // caller frees

/// Swap the editor's current buffer for `buf`. Returns the previously attached
/// buffer (caller now owns it). Passing NULL creates/attaches a fresh empty
/// buffer. Invalidate-on-attach: layout cache, pair cache, highlight line
/// index, scrollbar/drag state; needs_layout + needs_paint set.
vg_editor_buffer_t *vg_codeeditor_swap_buffer(vg_codeeditor_t *editor,
                                              vg_editor_buffer_t *buf);
vg_editor_buffer_t *vg_codeeditor_get_buffer(vg_codeeditor_t *editor); // borrow, no transfer
```

Internally `vg_codeeditor_t` replaces the moved fields with a single
`vg_editor_buffer_t *buffer;` (never NULL after create — the widget constructor
creates a default buffer so all existing code paths keep working).

### 3.3 Field-access migration strategy

The editor `.inc` files reference the moved fields thousands of times. Keep the
diff mechanical and reviewable:

1. Define `vg_editor_buffer_t` with the exact moved fields.
2. In `vg_codeeditor_t`, replace moved fields with `vg_editor_buffer_t *buffer;`.
3. Do a mechanical rename across `vg_codeeditor*.inc`, `vg_codeeditor.c`,
   `rt_gui_codeeditor*.c`, `vg_minimap.c`, `vg_findreplacebar.c` (both read
   editor internals — verify with grep for `->lines`, `->history`,
   `->cursor_line`, etc.):
   `editor->lines` → `editor->buffer->lines`, and likewise for every moved field.
   Use per-field `sed`-style passes, compiling after each field group.
   **BSD sed on macOS — no GNU-only syntax.**
4. Do NOT add compatibility macros (`#define lines buffer->lines`) — they poison
   other structs sharing member names.

### 3.4 Runtime bridge + registration

New file `src/runtime/graphics/gui/rt_gui_editorbuffer.c` (with full Viper header):

- Typed handle via the `rt_obj_new_i64` pattern (unique class id, e.g.
  `kEditorBufferClassId`), mirroring `rt_gui_subhandle.c` conventions. The handle
  owns the `vg_editor_buffer_t` unless attached; attaching transfers borrow to
  the widget but the Zia handle stays the owner of record (widget never frees a
  swapped-in buffer; `rt_codeeditor_destroy` frees only a buffer that was
  created internally and never exposed).
- Finalizer: if the buffer is still attached at GC time, detach-then-destroy is
  forbidden — instead the finalizer marks the handle dead and leaves the widget's
  buffer alone (the widget frees its internal buffer at destroy). Keep a
  `bool attached` + owning-widget pointer inside the handle to police this;
  `rt_gui` runs on one thread (`RT_ASSERT_MAIN_THREAD()` like `rt_gui_app.c:1631`).

`runtime.def` additions (follow the exact style of `Viper.GUI.SplitPane`
at `:2401-2405` + class block at `:9103`):

```c
RT_FUNC(GuiEditorBufferNew,        rt_editorbuffer_new,          "Viper.GUI.EditorBuffer.New",          "obj(str)")
RT_FUNC(GuiEditorBufferGetText,    rt_editorbuffer_get_text,     "Viper.GUI.EditorBuffer.get_Text",     "str(obj)")
RT_FUNC(GuiEditorBufferGetRevision,rt_editorbuffer_get_revision, "Viper.GUI.EditorBuffer.get_Revision", "i64(obj)")
RT_FUNC(GuiEditorBufferIsModified, rt_editorbuffer_is_modified,  "Viper.GUI.EditorBuffer.IsModified",   "i1(obj)")
RT_FUNC(GuiEditorBufferClearModified, rt_editorbuffer_clear_modified, "Viper.GUI.EditorBuffer.ClearModified", "void(obj)")
RT_FUNC(GuiCodeEditorAttachBuffer, rt_codeeditor_attach_buffer,  "Viper.GUI.CodeEditor.AttachBuffer",   "obj<Viper.GUI.EditorBuffer>(obj,obj)")
RT_FUNC(GuiCodeEditorGetBuffer,    rt_codeeditor_get_buffer,     "Viper.GUI.CodeEditor.get_Buffer",     "obj<Viper.GUI.EditorBuffer>(obj)")
```

`AttachBuffer` returns the previously attached buffer handle (or a fresh handle
wrapping it if the previous buffer was widget-internal), which is what makes the
Zia-side swap a one-call operation. Add the matching `RT_CLASS_BEGIN("Viper.GUI.EditorBuffer", ...)`
block with `RT_METHOD`/`RT_PROP` entries, and the two new methods inside the
existing `Viper.GUI.CodeEditor` class block. **Leaf name `EditorBuffer` must be
globally unique** — verify with `./scripts/check_runtime_completeness.sh`.

Graphics-stub builds: add no-op stubs alongside the real implementations, same
as every `rt_canvas_*`/`rt_gui_*` function pair (see the stub convention note in
`src/runtime/core/rt_zia_completion_stub.c` for placement of non-graphics stubs).
Adding a new `rt_*.c` file may trip the `source_health_audit` file-count baseline
— update `scripts/source_health_baseline.tsv` if the build script reports it.

### 3.5 Semantics to nail down in code comments

- `SetText` on the widget continues to operate on the *attached* buffer
  (unchanged external behavior, including history clear — plan 01 stops calling it).
- `swap_buffer` does NOT touch history of either buffer.
- Widget paint/hit-test code must never cache raw `buffer` pointers across
  frames beyond the attach generation; add `uint64_t buffer_attach_generation`
  bumped on every swap, and fold it into the existing layout/pair/highlight cache
  validity checks (they already compare generations — extend the key).

## 4. Implementation steps

1. **Extract struct** — define `vg_editor_buffer_t` (new section in
   `vg_ide_widgets_editor.h`); move fields; add `buffer` pointer +
   `buffer_attach_generation` to `vg_codeeditor_t`.
2. **Mechanical rename** — field group at a time (text → history → cursor/sel →
   folds → overlays), compiling `src/lib/gui` after each group:
   `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 ... ./scripts/build_viper_unix.sh`.
3. **Lifecycle** — `vg_editor_buffer_create/destroy`; constructor creates default
   buffer (`vg_codeeditor_lifecycle.inc`); destroy frees the internal buffer only.
4. **Swap API** — `vg_codeeditor_swap_buffer` + cache invalidation + generation
   bump; `vg_codeeditor_get_buffer`.
5. **C unit test** — extend the gui lib tests under `src/lib/gui/tests/`
   (follow the existing test layout there): create editor, type (insert text via
   `vg_codeeditor_insert_text`), swap to buffer B, type, swap back to A, assert
   undo works on A's edits and A's cursor/scroll/fold state survived.
6. **Runtime bridge** — `rt_gui_editorbuffer.c` (+ stub), `rt_codeeditor_attach_buffer`
   in `rt_gui_codeeditor.c`; `runtime.def` entries; run
   `./scripts/check_runtime_completeness.sh`.
7. **Zia probe** — `viperide/src/probes/editor_buffer_probe.zia`: build two
   buffers, attach/swap, assert `get_Text`, `get_Revision`, modified flags, and
   undo-across-swap behave; register in `src/tests/CMakeLists.txt` with
   `LABELS "zia;viperide;editorbuffer"`.
8. **Full build + full test run** (no skip flags), plus
   `ctest --test-dir build -L viperide --output-on-failure`.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h` — struct split + new API.
- `src/lib/gui/src/widgets/vg_codeeditor_{lifecycle,api,core,editing,history,input,paint}.inc`,
  `vg_codeeditor.c` — field renames + lifecycle + swap.
- `src/lib/gui/src/widgets/vg_minimap.c`, `vg_findreplacebar.c` — renames where
  they reach into editor internals.
- `src/runtime/graphics/gui/rt_gui_codeeditor.c` — attach/get-buffer bridge; renames.
- `src/runtime/graphics/gui/rt_gui_editorbuffer.c` — **new**.
- Stub counterpart for non-graphics builds (mirror existing stub placement).
- `src/il/runtime/runtime.def` — RT_FUNC/RT_METHOD/RT_PROP/RT_CLASS entries.
- `src/lib/gui/tests/` — new/extended C test.
- `viperide/src/probes/editor_buffer_probe.zia` — **new**; `src/tests/CMakeLists.txt`.
- `scripts/source_health_baseline.tsv` — only if the audit demands it.

## 6. Testing

- C test (step 5) is the primary regression net for the refactor.
- Existing editor coverage must stay green: `ctest --test-dir build -L viperide`
  (notably `zia_viperide_editor_hot_path`, syntax/semantic-token probes) — these
  exercise the renamed fields via every public entry point.
- New probe (step 7) covers the Zia surface end-to-end.
- Perf sanity: `editor_hot_path_probe.zia` asserts on
  `vg_codeeditor_perf_stats_t` counters — confirm no counter regresses (the
  refactor adds one pointer hop; no algorithmic change).

## 7. Acceptance criteria

- All pre-existing gui/viperide tests pass unmodified (except mechanical renames
  inside the gui C tests themselves, if any reach into struct fields).
- New C test demonstrates: undo history, cursor, selection, scroll, folds, and
  semantic tokens survive a detach→reattach round trip.
- `viper --dump-runtime-api` lists `Viper.GUI.EditorBuffer` with New/get_Text/
  get_Revision/IsModified/ClearModified and `CodeEditor.AttachBuffer/get_Buffer`.
- `./scripts/check_runtime_completeness.sh` passes.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` (needs `build/src/tools/viper/viper`).
  C/runtime changes: full build first, then `build_ide.sh`.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD`/`RT_PROP`
  entries; run `./scripts/check_runtime_completeness.sh` after `runtime.def` edits.
- Full Viper file header on all new/modified C files; Zia modules follow the
  header style in `viperide/docs/architecture.md`.
- 100% cross-platform; platform code only in approved adapter layers.
- Zero external dependencies. Zia code binds namespace aliases, never inline
  `Viper.X.Y.Z()` calls.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
