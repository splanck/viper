---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0163: Add Stable Multi-Select and Row-Aware TreeView Editing

## Status

Accepted (2026-07-23)

## Context

`Zanna.GUI.TreeView` already retains a selection bit on every concrete node,
but the control exposes and paints only one primary node. It has no way to
return the stable data attached to every selected row. Scene editors therefore
have to flatten a hierarchy into a `ListBox` to support multi-object editing,
losing native expand/collapse behavior and making the displayed indentation a
fragile substitute for structure.

The poll-model drag-and-drop surface has a second mismatch. Its documented
position values are `0=before`, `1=into`, and `2=after`, while the native enum
currently assigns `1=after` and `2=into`. Poll-model drops are also forced
`INTO` and accept only folder-like nodes. That is appropriate for the project
Explorer but cannot express sibling ordering in a scene hierarchy.

Zanna Studio needs retained-node multi-selection and application-directed
`BEFORE`/`INTO`/`AFTER` drops to make hierarchy edits structural, transactional,
and undoable. These additions change the public runtime C ABI and registry
surface, so ADR 0006 requires an explicit decision.

## Decision

`Zanna.GUI.TreeView` gains these additive instance methods:

```text
SetMultiSelect(enabled: Boolean)
GetSelectedData() -> Seq[String]
SetDragDropMode(mode: Integer)
```

Their C ABI entry points are:

```c
void rt_treeview_set_multi_select(void *tree, int64_t enabled);
void *rt_treeview_get_selected_data(void *tree);
void rt_treeview_set_drag_drop_mode(void *tree, int64_t mode);
```

### Retained-node selection

Multi-selection applies only to a retained-node TreeView:

- an unmodified pointer click replaces the selection;
- Ctrl/Command-click toggles one node;
- Shift-click replaces the selection with the inclusive visible-row range from
  the anchor to the clicked node;
- Shift plus Up/Down applies the same visible-row range rule;
- ordinary keyboard navigation replaces the selection;
- `Select(node)` adds the node when multi-select is enabled, allowing a caller
  to restore a complete selection after rebuilding the tree;
- `Select(null)` always clears every selected node; and
- disabling multi-select keeps only the primary node.

`GetSelected()` remains the primary node. The most recently selected target is
primary; toggling the primary off promotes the first remaining selected node
in retained preorder. The anchor follows an ordinary or additive selection,
but remains fixed while extending a range.

`GetSelectedData()` returns one byte-exact copy of each selected retained
node's `Node.SetData` value in full retained preorder. Collapsed descendants
remain selected and are included. A selected node without data contributes an
empty string so sequence cardinality is preserved. Invalid handles and an
empty selection return a valid empty sequence. Virtual TreeViews remain
single-select and return an empty sequence because their rows have no retained
node data.

Removing a selected subtree, clearing the tree, changing a multi-selection, or
reducing it to the primary node produces exactly one logical selection-change
edge per operation. Every selected row is painted as selected; only the primary
row receives the accent marker.

### Application-directed drag and drop

The native drop-position values become explicit and match the runtime contract:

```text
BEFORE = 0
INTO   = 1
AFTER  = 2
```

`SetDragDropMode` accepts:

```text
0 = disabled
1 = legacy container-only INTO drops
2 = row-aware BEFORE/INTO/AFTER drops
```

Values outside that range are ignored without changing the current mode.
Changing modes cancels any drag or latched drop and releases TreeView input
capture. Mode 1 preserves the project Explorer contract: only targets that
advertise or contain children are accepted and every drop is `INTO`. Mode 2
accepts leaf targets and classifies the top 30 percent of a row as `BEFORE`,
the middle as `INTO`, and the bottom 30 percent as `AFTER`. Existing self,
descendant-cycle, and application callback validation still applies.

`SetDragDropEnabled(true)` remains a compatibility wrapper for mode 1;
`SetDragDropEnabled(false)` selects mode 0. Native callback-directed dragging
continues to use `vg_treeview_set_drag_enabled` and the row-aware position
classifier without latching.

Zanna Studio's 3D scene hierarchy uses retained parent/child nodes, enables
multi-selection and mode 2, and treats each latched drop as one canonical
transaction. `INTO` reparents selected top-level roots beneath the target.
`BEFORE` and `AFTER` move them to the target's parent and establish stable
sibling order. Validation, exact preserve-world conversion, serialization,
rollback, selection restoration, and undo remain editor responsibilities.

## Consequences

- Studio can present a real expandable hierarchy without sacrificing stable
  multi-object selection.
- Scene reparenting and sibling reordering can be driven by direct row gestures
  while remaining application-validated and undoable.
- The Explorer keeps its established folder-drop behavior.
- The numeric drop-position contract now agrees at the native, runtime, and
  documentation layers.
- Existing single-select TreeView programs and virtual models remain
  compatible.
- Native widget tests, runtime ABI tests, the reviewed GUI manifest, generated
  runtime documentation, authored widget documentation, and Studio probes must
  cover the new behavior.

## Alternatives Considered

- **Keep a flattened ListBox hierarchy.** Rejected because textual indentation
  cannot provide native expansion, structural hit targets, or row-aware drops.
- **Return selected labels.** Rejected because labels need not be unique and
  are presentation rather than stable identity.
- **Expose selected node handles as a sequence.** Rejected because it expands
  lifetime-sensitive subhandle ownership; TreeView nodes already have a stable
  string data slot.
- **Change `SetDragDropEnabled(true)` to row-aware mode.** Rejected because
  existing Explorer code depends on folder-only `INTO` drops.
- **Make virtual TreeViews multi-select implicitly.** Deferred because virtual
  selection belongs to the external model and needs a separate bitmap/model
  protocol rather than retained-node semantics.
