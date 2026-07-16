---
status: active
audience: contributors
last-verified: 2026-07-16
---

# ADR 0104: Normalize Entity3D Position Accessors to Properties

Date: 2026-07-01

Status: Accepted; verified against the live runtime registry, the caller sweep,
and the full test suite on 2026-07-01

## Context

`Viper.Game3D.Entity3D` exposed `Position` and `WorldPosition` as **methods**
(`RT_METHOD`, called `entity.Position()`), while every other 3D class that
reports a position — `Camera3D`, `Light3D`, `SceneNode`, `SoundListener3D`,
`SoundSource3D`, `Physics3DBody`, `Character3D`, `Transform3D`, `NavAgent3D` —
exposes it as a **property** (`RT_PROP get_Position`, read `obj.Position`).

This lone inconsistency made `entity.Position` (property-style, the form every
other class uses) surprising. It also intersected a Zia front-end defect: a
paren-less access to a runtime method resolved to nothing and lowered to invalid
IL. That front-end defect is fixed independently (the runtime-class member
resolver now diagnoses paren-less method access with a fix-it). This ADR removes
the *root* inconsistency so `Entity3D` matches its peers.

## Decision

Reclassify `Entity3D.Position` and `Entity3D.WorldPosition` from methods to
read-only properties:

- In `src/il/runtime/runtime.def`, rename the `RT_FUNC` public names
  `Viper.Game3D.Entity3D.Position` → `.get_Position` and `.WorldPosition` →
  `.get_WorldPosition`, and replace the two `RT_METHOD("Position", "obj()", …)`
  entries with `RT_PROP("Position", "obj<Viper.Math.Vec3>", …, none)` (read-only,
  no setter), matching the `SceneNode` / `Character3D` model.
- The C handlers `rt_game3d_entity_position` / `rt_game3d_entity_world_position`
  are **unchanged**: their signature `obj<Viper.Math.Vec3>(obj)` already matches
  a property getter, so this is a registry-classification change only.

This is a **breaking runtime-C-ABI surface change**: callers written as
`entity.Position()` must become `entity.Position`. All in-repo callers were
swept as part of this change.

## Implementation Status

Verified on 2026-07-01:

- `src/il/runtime/runtime.def`: the two accessors are now `get_Position` /
  `get_WorldPosition` `RT_FUNC` names classified by `RT_PROP` in the
  `Viper.Game3D.Entity3D` class block; `viper --dump-runtime-api` reports them as
  properties.
- Caller sweep: 17 `.Position()` sites across 10 files (`.zia` tests + examples,
  including `examples/games/game3d-showcase/worldsim.zia` and
  `examples/3d/openworld_slice/main.zia`) switched to the property form; a
  repo-wide re-grep for `\.(Position|WorldPosition)\(\)` returns none.
- Docs: `docs/viperlib/graphics/game3d.md` updated to the property form.
- Runtime-surface goldens regenerated; `test_runtime_class_qualified_surface`,
  the Graphics3D/Game3D ABI-surface tests, `test_rt_game3d`,
  `check_runtime_completeness.sh` (handler ids unchanged), and the full `ctest`
  suite pass; the game3d-showcase demo builds and smoke-runs.

## Consequences

The position API is now uniform across all 3D classes, and `entity.Position`
reads naturally as a property. External callers using `Entity3D.Position()` must
migrate to `Entity3D.Position`.

**Known follow-on debt (out of scope):** `Entity3D.IsSpawned()`,
`IsDestroyed()`, and `IsGroup()` are also zero-argument methods that peer classes
expose as properties. Normalizing those is deferred to a future ADR to keep this
change focused on the position accessors.

## Spec Impact

No IL opcode, IL type, verifier rule, linker, VM execution, numeric-semantics, or
native-codegen changes. This is a runtime-registry classification change
(method → property) on the public `Viper.Game3D` surface. Per ADR 0004 such
surface work is registry-only, but because this **reclassifies an existing public
member** (a breaking change) rather than adding a new one, it carries its own ADR
and caller sweep.
