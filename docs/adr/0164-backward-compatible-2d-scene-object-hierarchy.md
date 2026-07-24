---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0164: Add Backward-Compatible 2D Scene Object Hierarchy

## Status

Accepted (2026-07-23)

## Context

`Zanna.Game2D.SceneDocument` stores placed objects in one ordered array. Zanna
Studio consequently presents them as a flat list with Up and Down buttons. The
format cannot express organizational groups, retained expansion, parent-aware
selection, or direct `BEFORE`/`INTO`/`AFTER` hierarchy drops. Those limitations
make substantial 2D scenes increasingly difficult to navigate and edit.

Object IDs cannot safely identify parents because the runtime permits empty and
duplicate IDs. Introducing a required top-level object field or incrementing
the canonical scene version would also make older runtimes drop or reject new
documents unnecessarily. The existing object property map is typed,
round-tripped by old runtimes, and already part of canonical version 1.

The hierarchy needs explicit runtime ownership because array edits change
indices. Leaving parent-index repair to each editor would let `MoveObject`,
`RemoveObject`, and `DuplicateObject` silently corrupt documents. The new
methods and C entry points change the runtime surface, so ADR 0006 requires an
explicit decision.

## Decision

`Zanna.Game2D.SceneDocument` gains these additive methods:

```text
ObjectParent(index: Integer) -> Integer
TrySetObjectParent(index: Integer, parent: Integer) -> Boolean
```

Their C ABI entry points are:

```c
int64_t rt_game_scene_object_parent(void *scene, int64_t index);
int8_t rt_game_scene_try_set_object_parent(
    void *scene, int64_t index, int64_t parent);
```

`ObjectParent` returns `-1` for a root or invalid object. A parent of `-1`
means root. `TrySetObjectParent` accepts a valid object and either `-1` or a
valid different object, rejects self-parenting and cycles, and returns false
without mutation for every invalid request. Reparenting does not modify `x` or
`y`: object positions remain absolute scene-space coordinates. This hierarchy
is organizational and ordering-oriented, not transform inheritance.

### Version-1 persistence

A non-root object's parent index is serialized as the integer-valued reserved
property:

```json
"zanna.hierarchy.parentIndex": 3
```

The canonical scene version remains 1. Older runtimes retain this namespaced
scalar as an ordinary typed property even though they do not interpret it.
Current runtimes extract it into structural state while loading, omit it from
`ObjectKeys`, report it as absent through generic object property reads, and
reject generic set/remove attempts without changing either properties or
hierarchy. Only `ObjectParent` and `TrySetObjectParent` own the reserved key's
invariants.

The reserved key counts toward the serialized per-object property limit.
Therefore a non-root object may contain at most 255 public properties under
the existing 256-entry limit. The runtime rejects parenting a root that already
has 256 public properties. Once parented, adding a new public property at the
255-property capacity is rejected, while replacing or removing an existing
property remains valid. Making the object a root frees the reserved slot.

Loading validates the reserved value after all objects are available. A
non-integer value, an index outside the loaded object array, self-parenting, or
a cycle records an error diagnostic and normalizes every affected link to
root. The parser never publishes a cyclic hierarchy.

### Index-changing edits

The runtime keeps parent indices correct whenever the ordered object array
changes:

- `MoveObject(from, to)` applies the same old-to-new permutation to every
  object and parent reference.
- `RemoveObject(index)` promotes direct children to the removed object's
  parent, then remaps later parent indices.
- `DuplicateObject(index, id)` inserts the copy after the source and gives it
  the source's parent after index remapping. Descendants are not duplicated by
  this single-object API.
- `AddObject` creates a root object.

The array remains the canonical global draw order. Among children of the same
parent, array order is sibling order. Zanna Studio may move complete selected
subtrees as one undoable transaction by composing the parent and move APIs.

## Consequences

- Version-1 scenes gain formal hierarchy without a migration cliff.
- Studio can replace its flat object list with the retained multi-select,
  row-aware TreeView introduced by ADR 0163.
- Parent relationships survive save/load, object moves, duplication, removal,
  undo snapshots, and older-runtime round trips.
- Runtime diagnostics make malformed hierarchy data explicit while ensuring
  editor traversal is always finite.
- Public property enumeration remains focused on user-authored component data.
- Native runtime tests, registry manifests, generated runtime documentation,
  authored scene documentation, and Studio probes must cover the new surface.

## Alternatives Considered

- **Store parent IDs.** Rejected because IDs are neither required nor unique.
- **Add a top-level `parent` field and bump the schema.** Rejected because a
  typed namespaced property provides a compatible extension point.
- **Expose hierarchy only inside Studio.** Rejected because runtime array edits
  and non-Studio tooling would not preserve its invariants.
- **Make child positions parent-relative.** Rejected because it would change
  established scene-space behavior and require transform migration.
- **Recursively remove or duplicate descendants.** Rejected because it would
  silently change the established single-object API contract; editors can
  explicitly compose subtree transactions.
