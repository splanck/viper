# ADR 0072: Vegetation3D Scatter Seed API

Date: 2026-07-09
Status: Accepted

## Context

`Vegetation3D.Populate` scatters blades with a deterministic LCG, but the seed
was hardcoded. That made every vegetation object produce the same layout by
default and gave authors no way to intentionally reproduce or vary a specific
foliage distribution.

## Decision

Add `Vegetation3D.SetSeed(seed: Integer) -> Void`.

Each `Vegetation3D` object now receives a non-zero per-instance default scatter
seed. `SetSeed` stores a mixed non-zero seed on the object; subsequent
`Populate` calls use that stored seed, so repeated populates with the same seed
and inputs produce identical placement.

## Consequences

- Separate vegetation objects no longer share identical default scatter layouts.
- Authors can pin a seed for reproducible foliage placement.
- Existing code remains source-compatible; omitting `SetSeed` still produces a
  deterministic layout for each object instance.
- Disabled Graphics3D builds expose a no-op stub, matching other vegetation
  mutators.
