---
status: active
audience: contributors
last-verified: 2026-07-24
---

# ADR 0172: Expose SceneNode Lights and Add Studio Light Authoring

## Status

Accepted (2026-07-24)

## Context

VSCN v5 and later already preserve a `Light3D` attached to a `SceneNode`, and
`SceneGraph.Draw` transforms and submits those lights with the node hierarchy.
The native runtime also has internal node-light getter and setter functions.
However, `SceneNode.Light` is absent from the public runtime registry and C
header. Zia, BASIC, and Zanna Studio therefore cannot inspect, create, replace,
or remove the light component that the loader, serializer, and renderer already
understand.

Spot lights have a second authoring gap. Their constructor accepts inner and
outer cone angles in degrees, while VSCN and the renderer retain cosines. No
public getters expose the authored angles, and there is no atomic way to retune
both sides of the cone. An editor cannot faithfully populate its controls from
an imported or reopened spot light.

Imported scene templates may share mutable component objects. Editing a
`Light3D` in place can consequently change an unrelated node or another scene
instance. Light editing also needs the exact no-op, undo, and failed-save
guarantees used by Studio's existing material and hierarchy transactions.

## Decision

Expose a typed read/write `SceneNode.Light` property backed by the existing
native node-light ownership functions:

```zia
node.Light = light
var current = node.Light
node.Light = null
```

The setter retains a valid `Light3D`, releases the previous value, accepts
`null` to clear the component, and rejects a non-light handle without mutation.
The getter returns the borrowed attached light or `null`.

Add this spot-cone surface to `Light3D`:

```zia
light.InnerConeDegrees
light.OuterConeDegrees
light.SetSpotCone(innerDegrees, outerDegrees)
```

The read-only properties return degrees derived from the retained cosines. They
return zero for a non-spot light. A spot value is clamped to the constructor's
finite `0..89` degree domain and the returned pair has the same minimum
`0.01`-degree separation as newly constructed lights.

`SetSpotCone` is a no-op for a null, invalid, or non-spot light. For a valid spot
light it sanitizes both values together through the constructor rules, replaces
both retained cosines, and advances the light mutation generation once.

Zanna Studio adds:

- a **+ Light** action that creates a named point-light node;
- a single-node light inspector supporting directional, point, ambient, spot,
  rectangle-area, sphere-area, and volume lights;
- controls for every applicable retained field: color, intensity, enabled
  state, shadow eligibility, local offset, local direction, attenuation,
  decay, finite range, emitter dimensions, radius, and spot cone;
- hierarchy light markers and viewport markers with local direction and
  bounded range feedback.

Position and direction fields are labelled as light-local values. The node
transform remains the coarse placement and orientation, and
`SceneGraph.Draw` applies the complete world transform.

Studio never edits the currently attached light in place. It first normalizes
the inspector draft and constructs a complete independent `Light3D`. If the
replacement is observably equivalent to the current component, Studio discards
the draft and creates no history. Otherwise it assigns the staged light and
serializes exactly once. Add, apply/type conversion, and remove each create one
canonical VSCN history entry. Serialization failure reloads the preceding
canonical document, preserving selection and history through the existing
rollback path.

The inspector deliberately supports one selected node at a time. A
multi-selection reports that light editing requires a single node instead of
pretending that shared and mixed light fields are resolved. Batch light editing
can be added later with an explicit sparse-patch contract.

Studio does not impose the obsolete documented eight-light limit. The scene
format may preserve more authored lights; draw-time backend capacity remains
observable through the existing fixed-forward and clustered-light telemetry.

## Consequences

- VSCN's existing native-light support becomes authorable from Zia, BASIC, and
  Studio instead of being import-only.
- Reopened and imported spot lights can be inspected and reconstructed without
  losing cone angles.
- Type conversion and ordinary edits cannot mutate shared imported light
  objects.
- A focused native test must pin node ownership/type rejection and spot-cone
  sanitization. A focused Studio probe must pin all seven constructors,
  apply/remove no-op behavior, exact undo/redo, hierarchy presentation, and
  VSCN round trips.
- Generated runtime documentation and the Graphics3D guide must describe the
  new public properties and correct the stale light-capacity table.

## Alternatives Considered

- **Keep lights import-only.** Rejected because lighting is fundamental scene
  content, and imported VSCN data already proves the runtime can preserve it.
- **Store Studio-only light metadata.** Rejected because it would duplicate
  canonical renderer state and require a second conversion path at run time.
- **Mutate the attached light in place.** Rejected because imported components
  can be shared and a failed VSCN save could leave aliases observably changed.
- **Expose retained cone cosines directly.** Rejected because constructors and
  authoring tools use degrees; exposing both representations would invite
  mismatched validation and inverted inner/outer comparisons.
- **Cap Studio at eight authored lights.** Rejected because that table is stale:
  current fixed-forward and clustered paths support different capacities and
  already expose overflow telemetry.
