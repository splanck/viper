# Plan 10 — Entity-Aware World Queries and Event Stream

## Outcome

Add World3D query results that resolve raw physics hits to `Entity3D` and
registered hurt regions, plus a bounded unified World3D event view over stable
runtime event families. Keep low-level PhysicsWorld3D queries and existing
typed collision/hit/damage polling APIs compatible.

## Problem statement

World3D already maintains body-to-entity and name indexes and exposes
entity-aware collision events. Low-level raycasts, sweeps, overlaps, and hit
lists return physics bodies/hits, so games build another registry:

- bowling scans collision events and maps ball/pins manually;
- Ashfall's `weapons/targets.zia` maps body handles to target IDs and body
  regions; `core/events.zia` routes many gameplay events;
- controller/camera/combat helpers repeatedly bridge raw queries back to
  entities.

Existing Hitbox3D combat is collision/melee oriented; documentation notes that
ordinary physics rays do not automatically see hurt hitboxes. Ranged combat in
plan 13 needs a first-class world identity result.

## Dependencies

- plan 00 lifetime/determinism rules;
- existing body/name indexes, collision event batches, hit/damage events;
- plan 04 optional raw-body association, but query work can begin first;
- API register and risks R10/R11.

## ADR and API

Required: new result/list/event classes, event lifetime/order contract, public
C ABI, and relationship to low-level physics and current typed APIs. The ADR
must bound list/event storage and define behavior for raw bodies with no entity.

## Scope

In scope:

- closest/all ray, sphere sweep, and sphere overlap World3D wrappers;
- entity/body/hitbox/region resolution;
- miss result semantics;
- a bounded immutable event view over existing stable World3D event families;
- deterministic ordering, clear boundary, telemetry, and tests.

Out of scope:

- replacing PhysicsHit3D/PhysicsHitList3D;
- a global application/UI event bus;
- arbitrary script-defined event payloads;
- allocating a permanent entity wrapper for every raw physics body;
- ranged damage policy, owned by plan 13.

## Primary source owners

- `rt_game3d_indices.inc`, `rt_game3d_events.inc`, World3D struct/lifecycle;
- low-level physics query/raycast/probe implementation for delegation only;
- combat/hitbox internals for region lookup;
- new focused query/event implementation and public headers;
- Game3D defs/class IDs/CMake;
- query/collision/combat/unit/fixture tests and docs.

## Query contract

`WorldHit3D` is non-null for hit and miss. A hit exposes:

- raw PhysicsHit3D data: point, normal, distance/fraction, body, penetrating;
- optional `Entity3D` resolved through the world body index;
- optional active hurt `Hitbox3D` and region tag when its geometry is hit;
- stable entity ID/tag copied into the result for diagnostics;
- surface/material tag where existing surface systems can resolve it.

A raw body not attached to an Entity3D remains a valid hit with Entity null.
This is important for bowling and low-level users. Query results must never
invent entities or hide raw bodies.

All-hit ordering follows low-level distance/fraction order with deterministic
tie breaking by body/entity stable ID, not pointer address. Overlap ordering is
explicitly stable by entity/body ID.

## Event contract

The first unified stream mirrors only stable runtime-owned families:

- collision enter/stay/exit;
- hit and damage;
- animation event;
- interaction focus/use where runtime already records it;
- stream cell loaded/unloaded if existing data is stable;
- sound report/perception event if already world-owned.

It does not absorb application scene/menu/objective events. Existing typed
counts/accessors remain authoritative and source-compatible.

Events are ordered by simulation production phase, then source sequence. Each
has frame, kind, stable IDs/entities where applicable, point/normal/value/tag.
They are valid until `ClearEvents`, the documented next simulation boundary, or
world destroy. Define one automatic clear boundary; do not allow events to
silently accumulate forever.

## Implementation sequence

### Phase 0 — Inventory and trace

1. Enumerate every current query API, result type, allocation pattern, ordering,
   mask rule, and miss behavior.
2. Trace body-index add/remove on spawn, attach body, despawn, and world destroy.
3. Enumerate current event buffers, their clear/update phases, capacities, and
   object lifetime.
4. Trace hitbox geometry/ownership and prove why physics rays cannot resolve
   them today.
5. Create an event phase/order table before designing the unified view.

### Phase 1 — Entity-aware query core

1. Add an internal converter from low-level hit + world to world-hit data.
2. Resolve body through the existing O(1)/indexed world lookup; do not linearly
   scan all entities.
3. Preserve raw hit even when entity lookup fails.
4. Add closest ray, all ray, sphere sweep, and overlap wrappers one at a time.
5. Define result/list ownership. Prefer a retained result object or world-owned
   bounded view with explicit invalidation; never return pointers to released
   low-level temporary objects.
6. Add deterministic tie breaking and capacity/truncation diagnostics.

### Phase 2 — Hurt-region query integration

Choose and ADR-approve one approach:

- register ray-queryable hitbox proxy geometry in a world combat index; or
- perform a bounded secondary ray test against active hurtbox shapes associated
  with candidate entities.

Requirements:

- no double physics response or collision event from query-only hurt regions;
- bone-attached/current transforms sampled at the documented simulation pose;
- layer/team/channel filters remain available to plan 13;
- closest/all ordering includes region intersections correctly;
- inactive/destroyed hitboxes are skipped;
- broadphase prevents a full hitbox scan per ray.

Do not change low-level PhysicsWorld3D ray semantics.

### Phase 3 — Unified event storage

1. Define a compact tagged event record with copied scalar/vector data and
   retained-or-stable entity references per stale-handle policy.
2. Reuse existing event production points; do not produce a second semantic
   event at a different phase.
3. Append a unified record when typed buffers append, or create a zero-copy
   view/index over them if lifetime/order can be guaranteed.
4. Preallocate/grow outside steady state, cap counts, and record drops.
5. Implement immutable event view objects and clear/invalidation.
6. Ensure clear methods for existing typed events and unified stream have a
   documented relationship. Avoid one clear leaving dangling indexes into the
   other.

### Phase 4 — Registration and compatibility

Add class IDs, defs with qualified types, real/disabled symbols, CMake, docs,
and surface audits. Existing collision/hit/damage APIs remain unchanged. If
event view properties use optional values, use established Option/nullability
patterns consistently across Zia/BASIC.

### Phase 5 — Tests

Query tests:

- hit attached entity, raw body, miss, stale/despawned body;
- masks/layers/triggers and started penetration;
- all-hit and overlap deterministic order/ties;
- hitbox region versus entity-body fallback;
- moving/bone-attached hurt region;
- inactive/destroyed region;
- result lifetime across subsequent query and clear;
- capacity truncation/drop diagnostic;
- no per-hit allocation churn after warm-up.

Event tests:

- exact order across collision, hit, damage, animation, interaction families;
- enter/stay/exit across frames;
- automatic/manual clear boundaries;
- entity despawn after event production;
- overflow/drop policy;
- typed and unified views describe the same event;
- identical scenarios produce identical event traces;
- no events retained across world destroy/recreate.

Add Zia fixtures that replace a small body registry and print stable IDs/kinds.

### Phase 6 — Adoption spikes

- Bowling: resolve ball/pin collision entities without a second full scan where
  possible; preserve scoring order.
- Ashfall: replace only target body lookup for a simple hitscan; preserve its
  game-specific target IDs/teams until plan 13.
- Ridgebound: use ground/query entity for traversal or interaction.

Compare performance and event traces. Full migrations remain later plans.

## Performance budget

- body-to-entity lookup is indexed, not O(entity count);
- query overhead is proportional to returned hits plus bounded hurt-region
  candidates;
- unified event production is O(1) append with zero steady-state allocation;
- no per-event string allocation; use integer kind/tag and existing stable name
  refs only where necessary;
- overflow is bounded and observable.

## Validation

Run physics query/raycast/probe, Game3D collision/combat/interaction/animation,
determinism, robustness, surface audits, disabled graphics, adoption fixtures,
graphics3d label, platform policy, and full builds.

## Acceptance criteria

- World queries return correct raw body and optional entity/hurt region.
- Raw low-level bodies remain queryable without forced entity creation.
- Hitbox ray integration is broadphase-bounded and does not affect physics.
- Unified event order/lifetime is documented, deterministic, bounded, and
  allocation-stable.
- Existing typed APIs and low-level queries remain behavior-compatible.
- Bowling/Ashfall/Ridgebound spikes remove useful mapping boilerplate without
  changing outcomes.

## Stop conditions

Stop if region queries require scanning every hitbox, if event views retain
unsafe entity pointers, if typed/unified clear semantics cannot be reconciled,
or if the design expands into an application-wide script event bus.

## Handoff evidence

Provide query/event inventories, event phase table, hitbox broadphase design,
ordering traces, overflow/allocation measurements, adoption results, surface
diff, and ADR.

