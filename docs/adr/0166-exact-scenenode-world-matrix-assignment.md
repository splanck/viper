---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0166: Add Exact SceneNode World-Matrix Assignment

## Status

Accepted (2026-07-23)

## Context

`SceneNode` exposes its composed `WorldMatrix`, but mutation is limited to
parent-relative position, rotation, and scale. A caller that wants to place a
node in world space must invert its parent's transform, derive local TRS, and
decide what to do when the result contains shear or a singular basis.

That calculation cannot be reproduced safely from only `WorldPosition`,
`WorldRotation`, and `WorldScale`: those convenience getters do not retain
reflection signs or arbitrary affine shear. Silently approximating a requested
matrix would make authoring tools visually unstable and would prevent exact
undo. Zanna Studio needs this operation for world-aligned Move, Rotate, and
Scale handles on parented nodes.

The runtime already performs the same exact representability proof for
`TryAddChildPreserveWorld`, but that helper can only retain the node's current
matrix while changing its parent. Reusing its private decomposition contract
avoids a second editor-side matrix policy.

Adding a public runtime C ABI entry point requires this decision under ADR 0006.

## Decision

`Zanna.Graphics3D.SceneNode` gains one additive instance method:

```text
TrySetWorldMatrix(worldMatrix: Zanna.Math.Mat4) -> Boolean
```

The runtime signature uses `obj<Zanna.Math.Mat4>` so language frontends can
reject wrong argument types before execution. The native boundary independently
requires the live `Zanna.Math.Mat4` class ID and rejects every other object.

Its C ABI entry point is:

```c
int8_t rt_scene_node3d_try_set_world_matrix(void *node, void *world_matrix);
```

The method accepts only live `SceneNode` and `Mat4` handles and only finite
affine matrices. It computes:

```text
prospectiveLocal = inverse(parentWorld) * requestedWorld
```

or uses the requested matrix directly for a parentless node. The runtime then
requires a finite, non-degenerate position/quaternion/scale decomposition,
recomposes both the local and resulting world matrices, and proves both against
the requested values with the same strict tolerance used by exact
preserve-world reparenting.

Success publishes all local TRS lanes together, dirties the node and its
descendants through the existing transform-revision path, and returns true.
An already-satisfied request is also successful. Invalid handles, projective
matrices, singular parents, degenerate bases, shear, or decomposition drift
return false without changing any transform lane.

Graphics-disabled builds expose the same inert symbol and return false.

Zanna Studio may construct a world-space delta around each selected node's own
pivot and call this method. A multi-node editor transaction must restore every
captured local origin if any member rejects, so one failed conversion cannot
leave a partial live group edit.

## Consequences

- Runtime and editor callers share one exact, reflection-aware matrix-to-TRS
  policy.
- World-space authoring can reject impossible operations truthfully rather than
  corrupting or approximating scene data.
- The API is useful beyond Studio for import, procedural placement, and tools
  that receive complete transforms from another coordinate system.
- Existing callers remain source- and binary-compatible because the method and
  C symbol are additive.
- Native unit tests, graphics-disabled linkage, the reviewed Graphics3D
  manifest, authored API documentation, and generated runtime documentation
  must cover the new surface.

## Alternatives Considered

- **Add writable WorldPosition/WorldRotation/WorldScale properties.** Rejected
  because three independent writes expose partial states and cannot preserve a
  complete matrix containing reflection or shear.
- **Approximate the closest local TRS.** Rejected because an apparently
  successful edit could visibly drift and would not round-trip exactly.
- **Implement inversion and decomposition in Studio.** Rejected because the
  runtime owns SceneNode representation, validation, and dirty propagation.
- **Expose an unrestricted local affine matrix.** Rejected because SceneNode
  storage, serialization, animation, physics synchronization, and rendering are
  intentionally TRS-based.
