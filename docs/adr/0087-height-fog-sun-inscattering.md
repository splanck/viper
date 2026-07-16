---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0087: Height-Fog Sun Inscattering

Date: 2026-07-10

## Status

Accepted

## Context

Exponential height fog already existed across all four backends
(`Canvas3D.SetHeightFog`: per-fragment density from world height, combined
with distance fog by joint transmittance) with committed goldens depending on
its exact output. The plan's remaining atmosphere gap was directional: fog
looked identical toward and away from the sun, losing the "silver lining"
that sells outdoor mood. The plan also proposed replacing the fog curve with
a camera-integrated closed form — rejected here because the existing
per-fragment-height model is an established, goldened contract and the
integral would silently regress every scene using it.

## Decision

- **Keep the existing height-fog model** (per-fragment density x view
  distance). The upgrade is additive only.
- **Sun inscattering:** `Canvas3D.SetHeightFogSun(r, g, b, power, amount)`
  shifts the fog color toward the sun tint by
  `amount * pow(max(dot(viewDir, sunDir), 0), power)` at every fog
  application point. `amount` defaults to 0, keeping all existing output
  bit-identical. The sun direction resolves per frame on the CPU from the
  first enabled directional light slot (no light → term off for the frame)
  and uploads with the scene constants, appended at the end of each backend's
  uniform block so existing offsets never move.
- **`ClearHeightFog()`** disables height fog without touching distance fog
  (the pre-existing `ClearFog` clears both), and **`HeightFogEnabled`**
  reports the state for tooling.
- **`EnvHandle.withHeightFog(density, height, falloff)`** is the Game3D
  fluent sugar.

## Consequences

- All four shader sources carry the same scalar tint math (SW is the
  reference; Metal verified locally via the GPU smoke lane; GLSL/HLSL ported
  mechanically under the standard cross-platform waiver).
- The visual-polish Zia probe pins the behavior: with a green sun tint, the
  sun-facing view reads brighter in green than the anti-sun view, and the
  enable/clear state round-trips.
- Scenes that never call `SetHeightFogSun` render byte-identically — the
  compat contract the goldens enforce.
