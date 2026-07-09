# Plan 22 — TreeView drag-and-drop runtime surface

## 1. Objective & scope

Expose the TreeView widget's already-implemented drag-and-drop machinery to Zia
through `runtime.def`, using the poll model the IDE uses everywhere (edge-latched
"was it triggered" + "take payload" accessors). This is a pure runtime-surface
plan; wiring drops to actual file moves in ViperIDE is **plan 12**.

**In scope:** C latch state on the treeview runtime wrapper, enable/disable
switch, poll accessors, veto hook policy, `runtime.def` registration, probe.

**Out of scope:** ViperIDE explorer behavior (plan 12), cross-widget drag-drop
(the existing app-level drag-drop in `rt_gui_app.c` handles that separately),
drag visuals beyond what the widget already paints.

## 2. Current state (verified anchors)

- `vg_treeview.c` fully implements DnD internally: drop positions classified
  BEFORE/INTO/AFTER with hover-zone logic (header comment `vg_treeview.c:22-24`),
  validity vetoing via `treeview_drop_is_valid(tree, source, target, position)`
  (`:71-75`) and a `can_drop` callback checked in
  `treeview_drop_is_valid` (`:862+`), drop-target highlight painting
  (`:756-766` — INTO highlight vs BEFORE/AFTER insertion line).
- The widget therefore already tracks `is_dragging`, `drop_target`,
  `drop_position` and completes drops internally (reordering its own nodes).
- `runtime.def` has 20 `Viper.GUI.TreeView` entries — **none** mention drag or
  drop (verified by grep). Zia code cannot enable, observe, or veto drops.
- ViperIDE stores **absolute file paths as node data** on tree nodes
  (`viperide/docs/architecture.md` — "uses absolute paths as tree-node data";
  `core/project_manager.zia` populates the tree), so exposing source/target
  *node data strings* is sufficient for plan 12 — no node-handle marshaling needed.
- Poll-model precedent to copy: the CodeEditor gutter click latch —
  `gutter_clicked` / `gutter_click_read` / `gutter_clicked_line/slot` fields
  (`vg_ide_widgets_editor.h:337-341`) exposed as
  `WasGutterClicked`/`TakeGutterClick`/`GetGutterClickLine`/`GetGutterClickSlot`
  (`runtime.def:2803-2806`).
- TreeView's runtime bridge functions live in
  `src/runtime/graphics/gui/rt_gui_widgets_complex.c` (grep `rt_treeview_` to
  find the block).

## 3. Design

### 3.1 Widget-level additions (`vg_treeview.c` / its header in `vg_ide_widgets_tree.h`)

```c
// Config
bool drag_drop_enabled;          // default false — existing behavior unchanged

// Completed-drop latch (edge-triggered, poll-consumed)
bool drop_latched;
char *drop_source_data;          // strdup of source node's data ("" if none)
char *drop_target_data;          // strdup of target node's data ("" if none)
int  drop_pos;                   // 0=BEFORE, 1=INTO, 2=AFTER (mirror vg_tree_drop_position_t)
```

Behavior changes:

- When `drag_drop_enabled == false` (default): the widget's internal DnD is
  fully suppressed (no drag start), so existing users see zero change.
- When enabled and a drop completes **and is valid**, the widget:
  1. records the latch fields (freeing any previous strdups),
  2. does **NOT** self-mutate its node order. Application-directed mode: the
     Zia side is the source of truth (ViperIDE will move the file and refresh
     the tree). Internal self-reorder remains available only when no latch
     consumer exists — simplest correct rule: when `drag_drop_enabled` was set
     through the runtime API, self-reorder is off. Document this in the header.
- `can_drop` veto: default policy when enabled from the runtime is
  "INTO only onto expandable (dir) nodes; BEFORE/AFTER suppressed" — plan 12's
  need. Provide `vg_treeview_set_drop_into_only(tree, bool)` (default true when
  runtime-enabled) rather than exposing a callback to Zia (the poll model has no
  synchronous callback path).

### 3.2 Runtime bridge (`rt_gui_widgets_complex.c`)

```c
void   rt_treeview_set_drag_drop_enabled(void *tree, int64_t enabled);
int64_t rt_treeview_was_drop_received(void *tree);        // true once per latch
void  *rt_treeview_get_drop_source_data(void *tree);      // rt_string
void  *rt_treeview_get_drop_target_data(void *tree);      // rt_string
int64_t rt_treeview_get_drop_position(void *tree);        // 0/1/2
void   rt_treeview_clear_drop(void *tree);                // consume latch
```

`WasDropReceived` follows the gutter-click convention: returns true while a
latch is pending and unconsumed; `ClearDrop` consumes. (Alternative single-shot
`TakeDrop` returning an object was rejected — the existing IDE code style polls
scalar accessors, see `debug_commands.PumpGutter` usage.)

### 3.3 `runtime.def` entries

```c
RT_FUNC(GuiTreeViewSetDragDropEnabled, rt_treeview_set_drag_drop_enabled, "Viper.GUI.TreeView.SetDragDropEnabled", "void(obj,i1)")
RT_FUNC(GuiTreeViewWasDropReceived,    rt_treeview_was_drop_received,     "Viper.GUI.TreeView.WasDropReceived",    "i1(obj)")
RT_FUNC(GuiTreeViewGetDropSourceData,  rt_treeview_get_drop_source_data,  "Viper.GUI.TreeView.GetDropSourceData",  "str(obj)")
RT_FUNC(GuiTreeViewGetDropTargetData,  rt_treeview_get_drop_target_data,  "Viper.GUI.TreeView.GetDropTargetData",  "str(obj)")
RT_FUNC(GuiTreeViewGetDropPosition,    rt_treeview_get_drop_position,     "Viper.GUI.TreeView.GetDropPosition",    "i64(obj)")
RT_FUNC(GuiTreeViewClearDrop,          rt_treeview_clear_drop,            "Viper.GUI.TreeView.ClearDrop",          "void(obj)")
```

plus matching `RT_METHOD` lines inside the existing
`RT_CLASS_BEGIN("Viper.GUI.TreeView", ...)` block. Run
`./scripts/check_runtime_completeness.sh` afterwards.

### 3.4 Cross-platform / determinism notes

All of this is widget-internal mouse handling — no platform code. The latch
strdups must be freed in the treeview destroy path (add to its vtable destroy).

## 4. Implementation steps

1. Add config + latch fields to the treeview struct; initialize in create;
   free strdups in destroy.
2. Gate drag initiation on `drag_drop_enabled`; suppress BEFORE/AFTER when
   `drop_into_only`; on valid drop completion, fill the latch and skip
   self-reorder (per 3.1).
3. Widget-level C test in `src/lib/gui/tests/`: synthesize mouse events
   (down on node A, move over node B, up) via the widget's `handle_event`
   vtable with `vg_event_t` records; assert latch contents and that node order
   did not change.
4. Runtime bridge functions + `runtime.def` entries + completeness check.
5. Zia probe `viperide/src/probes/tree_dnd_probe.zia`: build a small tree with
   node data, enable DnD, drive events through the GUI test/virtual helpers
   already used by existing probes (`Viper.GUI` test helpers are available per
   `viperide/docs/architecture.md` "command palette, and test/virtual-list
   helpers"; follow `file_tree_probe.zia` / `multi_root_file_tree_probe.zia`
   for how tree probes drive interaction), assert `WasDropReceived` +
   source/target/position, and that `ClearDrop` consumes the latch.
   Register in `src/tests/CMakeLists.txt`, `LABELS "zia;viperide;explorer"`.
6. Full build + test run.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_tree.h` — struct fields + new functions.
- `src/lib/gui/src/widgets/vg_treeview.c` — gating, latch, drop-into-only.
- `src/runtime/graphics/gui/rt_gui_widgets_complex.c` — six bridge functions.
- `src/il/runtime/runtime.def` — six RT_FUNC + six RT_METHOD entries.
- `src/lib/gui/tests/` — C test.
- `viperide/src/probes/tree_dnd_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

- C test (step 3) covers gesture → latch mechanics including the
  no-self-reorder rule and the into-only veto.
- Probe (step 5) covers the Zia surface.
- Regression: existing `file_tree_probe.zia` and
  `multi_root_file_tree_probe.zia` must stay green — they exercise the treeview
  with DnD disabled (the default), proving no behavior change.

## 7. Acceptance criteria

- With `SetDragDropEnabled(false)` (or never called): behavior byte-identical
  to today; all existing tree probes pass.
- With it enabled: dragging node A onto expandable node B latches one drop
  record `(A.data, B.data, INTO)`; dropping onto a leaf or invalid target
  latches nothing; the tree never reorders itself.
- `viper --dump-runtime-api` lists the six new TreeView members;
  `./scripts/check_runtime_completeness.sh` passes.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` (needs `build/src/tools/viper/viper`).
  C/runtime changes: full build first, then `build_ide.sh`.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD`/`RT_PROP`
  entries; run `./scripts/check_runtime_completeness.sh` after `runtime.def` edits.
- Full Viper file header on all new/modified C files.
- 100% cross-platform; platform code only in approved adapter layers.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
