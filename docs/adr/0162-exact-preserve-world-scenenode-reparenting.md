---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0162: Add Exact Preserve-World SceneNode Reparenting

## Status

Accepted (2026-07-23)

## Context

`SceneNode.TryAddChild` changes hierarchy while retaining the child's local
translation, rotation, and scale. That is the correct primitive for code that
authors parent-relative transforms, but it moves an existing object in world
space whenever the old and new parents differ.

Zanna Studio previously exposed only that local-preserving behavior. Reparenting
an arranged prop, trigger, spawn point, camera rig, or imported subtree beneath
a transformed organizational node could therefore teleport, rotate, or resize
it. Editor-side conversion from the public `WorldPosition`, `WorldRotation`, and
`WorldScale` getters is not sufficient: the full composed matrix can contain a
reflection or shear, and `WorldScale` intentionally reports magnitudes.

SceneNode stores local TRS rather than an arbitrary affine matrix. Some
preserve-world requests are consequently impossible without losing authored
data. A rotated child moved beneath a differently oriented non-uniform scale,
for example, can require local shear. A zero-scale destination parent has no
inverse at all. Silently approximating either case would make undoable editor
content visually unstable.

This change adds a public runtime C ABI entry point, so ADR 0006 requires an
explicit decision.

## Decision

`Zanna.Graphics3D.SceneNode` gains this additive instance method:

```text
TryAddChildPreserveWorld(child: SceneNode) -> Boolean
```

Its C ABI entry point is:

```c
int8_t rt_scene_node3d_try_add_child_preserve_world(
    void *parent, void *child);
```

The proposed parent computes:

```text
candidate_local = inverse(parent_world) * child_world
```

before changing hierarchy state. The runtime accepts the operation only when
all of the following hold:

- the handles and hierarchy relationship are valid;
- the child is not an implicit scene root and the link cannot form a cycle;
- the destination world matrix is invertible;
- the candidate local basis is finite, non-degenerate, and orthogonal;
- candidate local TRS recomposes to the complete candidate local matrix; and
- the new parent multiplied by that local TRS recomposes to the complete prior
  child world matrix within bounded floating-point tolerance.

The decomposition preserves reflections by carrying a negative determinant on
one scale axis. It does not discard or approximate shear. Invalid handles,
roots, cycles, singular matrices, degenerate bases, shear, or decomposition
drift return false before parent slots, ownership, local transforms, or dirty
state change. An already-satisfied parent returns true without requiring an
inverse, matching `TryAddChild`.

After successful conversion preflight, the existing transactional attachment
path performs retention, prior-parent removal, scene-owner propagation, and
target insertion. Publishing the already-computed finite local TRS cannot fail.
The child retains its complete world matrix; because descendant local
transforms do not change, its complete subtree retains world placement as well.
Normal runtime allocation failures retain the existing trap semantics of
`TryAddChild`.

Graphics-disabled builds export the same symbol and return false.

Zanna Studio enables **Keep world transform** by default beside the Parent
chooser. Users can clear it to request the established local-preserving
behavior. A multi-root edit invokes the exact runtime primitive for each
top-level selected root, then serializes the complete result once. If any root
cannot be represented or the canonical commit fails, Studio reloads the prior
VSCN bytes and restores the complete selection, so partial group movement never
enters document history.

## Consequences

- Runtime callers and Studio can reparent nodes without unexpected world-space
  movement when exact local TRS exists.
- Singular or shear-producing conversions fail visibly instead of corrupting
  or approximating authored transforms.
- Local-preserving reparenting remains available for intentionally
  parent-relative workflows.
- The public API is additive and does not change VSCN: successful operations
  persist the newly derived local TRS through the existing serializer.
- Native runtime tests, the reviewed Graphics3D ABI manifest, generated runtime
  docs, the Graphics3D guide, and Studio probes cover success and rejection.
- This decision does not add general affine/shear storage, drag-and-drop
  hierarchy rows, or an all-or-nothing runtime batch-reparent API. Studio
  supplies batch atomicity through canonical document rollback.

## Alternatives Considered

- **Always preserve local TRS.** Rejected as the only editor mode because it
  makes ordinary hierarchy organization move existing world content.
- **Preserve only position, rotation, and scale getters.** Rejected because
  magnitudes cannot reconstruct reflection and separate values can hide shear
  in the complete matrix.
- **Approximate with orthonormalization.** Rejected because a successful return
  would silently change geometry; explicit failure is safer and undoable.
- **Store a general affine local matrix on every SceneNode.** Deferred because
  it would change animation, physics, serialization, inspector, and verifier
  assumptions far beyond the reparenting workflow.
- **Perform matrix math only in Studio.** Rejected because it would duplicate
  runtime transform rules, lose private numeric guards, and leave code-first
  callers without the same safe primitive.
