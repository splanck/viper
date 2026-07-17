# Plan 13 — Ranged Combat and Ray-Queryable Hurt Regions

## Outcome

Add a reusable, bounded `Ballistics3D` layer for hitscan, penetration, and
radial damage using plan-10 entity-aware queries and the existing
Hitbox3D/Health3D/event systems. Weapon definitions, ammo, cadence, recoil, AI,
and scoring remain game-owned.

## Problem statement

Game3D combat currently provides bone/entity hitboxes, melee collision testing,
Health3D, hit/damage events, knockback, hit stop, and ragdoll integration. It
does not provide a ray-to-hurt-region damage path. Ashfall therefore maintains:

- body-to-target/region registration;
- hitscan and penetration loops;
- blast overlap/dedup;
- team/friendly-fire filters;
- impact event routing.

This is reusable engine policy, but Ashfall's ten weapons and resource model
are not. The runtime layer should stop at a deterministic shot/damage result.

## Dependencies

- plan 10 WorldHit3D/hurt-region query integration;
- plan 11 pooled feedback for optional consumers, not automatic effects;
- existing Hitbox3D, Health3D, SurfaceTable3D, hit/damage events, teams/channels;
- API register and risk R14.

## ADR and API

Required: public Ballistics3D/DamageSpec/result surfaces, ray-hurtbox behavior,
damage ordering, penetration/material contract, event production, and bounds.
The ADR must state which values are authoritative simulation results and which
feedback remains caller-owned.

## Scope

In scope:

- ordered hitscan with a bounded impact count;
- per-region damage multiplier/tag;
- entity/team/friendly-fire resolution;
- simple penetration budget through ordered impacts;
- radial overlap, distance falloff, line-of-sight option, dedup by health/entity;
- impulse/knockback through existing APIs;
- hit/damage event compatibility and result objects;
- deterministic tests and Ashfall adoption spike.

Out of scope:

- weapon inventory/ammo/reload/fire rate/spread patterns;
- visual tracers/muzzle flash/audio/camera recoil;
- full projectile lifecycle; games can use existing physics bodies/entities;
- destructible geometry/material simulation;
- network replication.

## Primary source owners

- `rt_game3d_combat.c`, Hitbox3D/Health3D internals and events;
- plan-10 query implementation;
- `rt_game3d_surfaces.c` for surface/material tags if appropriate;
- new focused ballistics implementation/header;
- Game3D world/controllers definitions, class IDs, CMake;
- combat/query unit tests, Zia ballistics fixture, docs.

## Combat contract

### DamageSpec3D

An immutable/configuration value includes:

- base amount and integer damage tag;
- maximum impacts and penetration budget;
- impulse magnitude;
- friendly-fire flag and optional team/channel filters;
- radial falloff mode and line-of-sight flag where applicable.

Validate finite/nonnegative values and hard-cap impact counts. Do not embed a
weapon pointer or script callback.

### Hitscan

1. Normalize/validate ray and range.
2. Query ordered WorldHit3D impacts.
3. Resolve active hurt region; otherwise entity Health3D fallback.
4. Apply team/channel/friendly-fire filter.
5. Compute region multiplier and penetration cost using explicit surface/region
   data; unknown material uses a conservative default. The natural material
   key is the `SurfaceType` already exposed by raw hits (carried through
   WorldHit3D); note that `SurfaceTable3D` is currently a footstep/audio
   mapping, so a penetration-cost table is new policy keyed by the same
   surface IDs, not an overload of the audio table.
6. Apply Health3D damage once per accepted impact, then impulse.
7. Record impact result and existing hit/damage events.
8. Continue only while penetration budget and max impacts permit.

Tie/order behavior must be deterministic. A non-damageable body may still
consume penetration and appear in the result.

### Radial damage

1. Perform bounded overlap.
2. Deduplicate multiple bodies/hurt regions belonging to one Health3D/entity.
3. Sort by stable entity/body ID.
4. Optionally run a bounded line-of-sight query per target.
5. Apply deterministic distance falloff, region/entity multiplier policy, and
   outward impulse.
6. Emit one damage event per affected health owner.

## Implementation sequence

### Phase 0 — Characterize current combat

Freeze tests for melee Hitbox3D, Health3D invulnerability/death/knockback,
hit/damage event order, team/channel behavior, and ragdoll. Trace Ashfall's
ballistics/target registry and classify every behavior as generic or game-owned.

### Phase 1 — Hurt-region metadata

1. Add `DamageMultiplier` and `RegionTag` to Hitbox3D through ADR.
2. Validate ranges and defaults (`1.0`, tag `0`).
3. Ensure bone-attached region transforms used by plan-10 ray queries match
   current animation pose.
4. Keep melee collision semantics unchanged; multiplier can be read by callers
   but must not silently change existing melee damage unless explicitly wired.
5. Add surface/team/channel lookup helpers without pointer-based identity.

### Phase 2 — DamageSpec and result objects

Implement validated, bounded configuration and immutable shot/impact result.
Results copy stable scalar/vector/ID data and use existing stale-safe entity/
hitbox handles. Define lifetime independent of plan-10 scratch lists so a
caller can inspect the complete result after query scratch reuse.

### Phase 3 — Hitscan core

Implement a pure internal loop first with unit fixtures. Separate:

- query;
- filter/dedup;
- penetration calculation;
- damage application/event append;
- result construction.

This separation lets tests inject ordered hits without rendering/physics and
prevents effect/audio code from entering combat authority.

Penetration v1 uses explicit numeric budget/cost from surface/region/default
policy. Do not infer real-world material thickness beyond available hit data.
If exit thickness is unavailable, document the simplified per-impact cost.

### Phase 4 — Radial damage core

Use World3D overlap and a reusable dedup scratch table. Cap candidates and
line-of-sight rays; expose truncation in the result/diagnostics. Apply falloff
with finite clamping. Ensure a multi-body enemy receives damage once.

### Phase 5 — World events and feedback seam

Append current hit/damage events exactly once. Optionally add a ballistics
impact event kind to plan-10 stream if it contains stable data and ADR approves.

Do not emit particles/sounds automatically. The caller consumes `ShotResult3D`
or events and calls named cues from plan 11. This keeps headless determinism and
game art policy separate.

### Phase 6 — Registration and tests

Add defs/IDs/real/disabled symbols/CMake/docs/audits. Tests:

- miss, one hit, raw non-damageable body, entity fallback;
- head/body regions with multipliers/tags;
- inactive region and destroyed entity;
- friendly/team/channel filtering;
- penetration stop/continue/max impacts and stable order;
- unknown/known surface costs;
- radial falloff, line of sight, occlusion, multi-body dedup;
- Health invulnerability/death/knockback/ragdoll interaction;
- hit/damage/unified event order exactly once;
- fixed replay determinism;
- scratch/result lifetime and overflow diagnostics;
- zero steady-state allocation after preallocated result capacity where the
  API permits reuse; otherwise bounded documented allocation per explicit shot;
- low-level/melee combat compatibility.

### Phase 7 — Ashfall and generic adoption spikes

1. Recreate one single-hit, one penetrating, and one radial Ashfall weapon using
   Ballistics3D while retaining weapon stats/ammo/events in Ashfall.
2. Compare target health, impact order, tags, impulses, and stress timing to
   Ashfall's current probes.
3. Add a small non-FPS turret/raycast example to prove the API is not tied to
   viewmodel weapons.
4. Document behaviors Ashfall still needs game-side; do not expand runtime for
   weapon-specific exceptions without multi-game evidence.

## Performance budget

- hitscan cost follows broadphase query plus bounded impacts;
- radial uses one overlap plus at most capped LOS rays;
- dedup uses reusable scratch and is not O(n²);
- no per-hit string allocation;
- result sizes and overflow are bounded/observable;
- burst stress meets or improves Ashfall's current probe budget.

## Validation

Run combat, health/hitbox, physics query, ragdoll/time, event, Ashfall combat and
stress probes, surface audits, disabled graphics, deterministic replay,
graphics3d label, platform policy, and full builds.

## Acceptance criteria

- Ray hits resolve correct entity and hurt region multiplier/tag.
- Penetration and radial damage are bounded, ordered, deterministic, and deduped.
- Health/events/knockback/ragdoll behavior composes without duplicate events.
- Ashfall spikes match existing health/impact traces within stated tolerance.
- Weapon/art/audio policy stays outside runtime.
- Existing melee/low-level query surfaces remain compatible.

## Stop conditions

Stop if ballistics needs a weapon class, script callback, full hitbox scan,
unbounded LOS rays, or effect/audio side effects. Narrow the generic result and
leave genre policy in the game.

## Handoff evidence

Provide Ashfall behavior classification, damage/order traces, penetration
policy, radial caps/dedup measurement, stress results, event comparison,
surface diff, and ADR.

