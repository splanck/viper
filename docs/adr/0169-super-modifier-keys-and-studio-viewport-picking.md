---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0169: Add Super Keys and Geometry-Aware Studio Viewport Interaction

## Status

Accepted (2026-07-23)

## Context

Zanna Studio's 3D viewport renders authored meshes through the production
runtime, but pointer selection still tests only a small marker at each node
origin. Large, offset, or overlapping meshes can therefore be visible without
being practically selectable. The viewport also orbits on both auxiliary mouse
buttons but has no camera-plane pan, and viewport clicks cannot build the
multi-node selections already supported by the hierarchy and inspector.

Conventional primary-modifier selection is Control on Windows and Linux and
Command on macOS. The input backend already reports the GLFW-compatible
left/right Super key codes used for Command and Windows keys, but
`Zanna.Input.Key` does not expose them. Studio must not infer the host OS and
substitute Control on macOS, nor may it depend on backend-private constants.

Adding public runtime C ABI entry points requires this decision under ADR 0006.

## Decision

`Zanna.Input.Key` gains two additive side-specific properties:

```text
LeftSuper: Integer
RightSuper: Integer
```

Their GLFW-compatible values and C ABI entry points are:

```c
#define ZANNA_KEY_LSUPER 343
#define ZANNA_KEY_RSUPER 347

int64_t rt_keyboard_key_lsuper(void);
int64_t rt_keyboard_key_rsuper(void);
```

`Keyboard.KeyName` returns `Left Super` and `Right Super` for those values.
These are key-code constants, not a new aggregate modifier-state API. Callers
that need a platform-neutral primary modifier test either Control key or Super
key with `Keyboard.IsDown`.

Zanna Studio configures its retained orthographic viewport camera before both
rendering and picking. A viewport click resolves in this order:

1. the active transform gizmo handle;
2. the closest visible mesh node whose transformed world-space bounds intersect
   the camera ray from `Camera3D.ScreenToRayOrigin` and `ScreenToRay`, using
   `SceneGraph.RaycastNodes`; and
3. the existing bounded origin-marker test, so meshless nodes remain
   selectable.

Picking is deliberately geometry-bound aware, not a per-triangle query.
Shaded and wireframe modes use the same scene, camera, hierarchy visibility,
and ray-query path.

A plain hit replaces the selection. Shift-click adds the hit. Control- or
Super-click toggles the hit. A plain click on empty viewport space clears the
selection, while a modified empty-space click preserves it. Two nearby
consecutive viewport clicks on the same node keep the same selection semantics
and then frame the primary hit; unrelated toolbar or inspector clicks cannot
satisfy that gesture.

Shift plus middle- or right-button drag pans in the camera's screen plane;
unmodified middle- or right-button drag continues to orbit. A positive pointer
delta moves rendered content with the pointer. Panning updates only the
document's workspace camera target and never canonical VSCN bytes, revision,
dirty state, or undo/redo history.

## Consequences

- Visible meshes can be selected across their projected bounds instead of only
  through a fourteen-pixel origin target, including depth-overlapping meshes.
- Multi-selection behavior is conventional on macOS, Windows, and Linux
  without platform conditionals in Studio.
- Meshless organizational nodes retain the marker fallback, and gizmos retain
  first priority so selection cannot steal transform drags.
- Bounds can select empty space inside a sparse mesh's world AABB. Exact
  triangle picking remains a possible additive runtime feature if authoring
  workflows later prove it necessary.
- The shared retained camera prevents render, marker, and pick projections from
  drifting independently.
- Runtime unit tests, registry completeness checks, generated API
  documentation, public input guidance, and a focused display probe must cover
  the new keys and viewport interaction contract.

## Alternatives Considered

- **Keep marker-only selection.** Rejected because it scales poorly with dense
  scenes and makes the truthful mesh renderer largely decorative.
- **Implement Studio-only mesh intersection.** Rejected because it would
  duplicate transform, hierarchy visibility, bounds, and camera semantics
  already owned by the runtime.
- **Treat Control as primary on every platform.** Rejected because Command is
  the established primary selection modifier on macOS.
- **Use backend-private Command/Windows constants.** Rejected because Studio is
  portable Zia code and must consume only public runtime APIs.
- **Pan in world X/Y.** Rejected because it becomes unintuitive after orbiting;
  camera right/up preserves screen-plane motion at every yaw and pitch.
