---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0038: Graphics Factory Aliases

## Status

Accepted

## Context

Several Graphics3D and Game3D factory methods repeated `New` even when the class
name already supplied the noun:

- `Mesh3D.NewBox`
- `Material3D.NewPBR`
- `Light3D.NewDirectional`
- `World3D.NewWithCamera`

The runtime overhaul naming policy keeps `New()` for default constructors but
prefers named factories such as `Mesh3D.Box(...)` and
`World3D.WithCamera(...)`.

Existing programs and demos already use the old names, so the change must remain
additive.

## Decision

Add canonical factory aliases:

- `Mesh3D.Box`, `Sphere`, `Plane`, `Cylinder`
- `Material3D.FromColor`, `Textured`, `PBR`
- `Light3D.Directional`, `Point`, `Ambient`, `Spot`
- `Collider3D.Box`, `Sphere`, `Capsule`
- `World3D.WithCamera`, `WithHorizontalCamera`

Keep all existing `New*` names as compatibility aliases. `Material3D.FromColor`
is used instead of `Material3D.Color` because `Color` is already a material
property.

## Consequences

- New graphics examples read as named constructors rather than low-level
  allocation calls.
- Existing source and IL continue to work.
- Runtime API dumps expose both name sets for now.
- Factory audits should prefer the canonical aliases except where documenting
  compatibility.
