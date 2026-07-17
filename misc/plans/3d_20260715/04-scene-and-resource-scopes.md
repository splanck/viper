# Plan 04 — Scene and Resource Scopes

## Outcome

Add an idempotent `SceneScope3D` that can own and release the heterogeneous
resources created by a scene, level, match, or transient gameplay section. A
scope must use existing public lifecycle operations, preserve shared-resource
rules, and make partial setup/teardown safe.

## Problem statement

All three games maintain manual ownership:

- bowling holds raw scene nodes, bodies, materials, lights, post-FX, and game
  objects across setup/reset paths;
- Ridgebound coordinates world, terrain, water, vegetation, weather, critters,
  particles, decals, and sounds;
- Ashfall's `LevelBase` has explicit `ownedNodes`/`ownedBodies`, while
  `LevelManager` invokes multiple subsystem unload/reset paths.

World3D correctly owns registered entities/effects, but it does not give a game
a smaller ownership boundary than the whole world. A scope provides that
boundary without turning World3D into a scene-state monolith.

## Dependencies

- plan 00 lifetime contract;
- plan 03 for application/frame integration and safe scene-boundary timing;
- API register typed tracking decision;
- risk R4 review.

## ADR and public surface

Required: new public class/C ABI, ownership rules, and a new dependency among
world, raw Graphics3D objects, and application scopes. The ADR must decide typed
tracking methods, nested scopes, duplicate/shared ownership behavior, release
order, and stale-handle semantics.

## Scope

In scope:

- entities, raw scene nodes, raw physics bodies, lights, particles, decals,
  SoundSource3D values, and nested scopes;
- reverse-order, type-aware release;
- idempotent release, partial construction, manual early removal, and world
  destruction;
- diagnostics/counters and tests;
- integration hooks for plans 05, 11, 12, and 14.

Out of scope:

- serializing scope contents;
- general-purpose GC replacement;
- automatically discovering every object reachable from script;
- owning immutable shared meshes/materials/assets by default;
- scene transitions/UI orchestration, owned by plan 12.

## Primary source owners

- new `rt_game3d_scene_scope.c/.h` (exact name after ADR);
- `rt_game3d_internal.h`, `rt_game3d.h`, ID registry;
- World3D create/destroy path for registered scope invalidation;
- Game3D `.def` owner and runtime CMake lists;
- entity/world/effects/audio public lifecycle helpers;
- focused unit tests and Zia lifecycle fixture;
- Game3D ownership/lifecycle docs.

## Ownership decisions

### Exclusive tracking

A mutable live resource may have at most one owning SceneScope3D. Registering it
twice in the same scope is idempotent. Registering it in another live scope
must either fail with a clear diagnostic or use an explicit `TransferTo`
operation approved by the ADR. Silent double ownership is forbidden.

Immutable asset references such as meshes/materials/templates are not tracked
in v1; the objects that use them retain them through existing runtime rules.

### Release order

Release in dependency-safe reverse order:

1. mark scope releasing; reject new tracking;
2. release child scopes newest first;
3. stop/detach sounds and transient registered services;
4. remove effects/decal/particle registrations;
5. despawn live Entity3D values through World3D;
6. remove raw physics bodies from their world;
7. remove raw scene nodes from their graph;
8. detach/remove lights from canvas/world slots;
9. release retained references and ownership-index entries;
10. mark released and zero counts.

Adjust exact ordering if source proves a dependency, but document and test it.
Never directly free a runtime object that has a public remove/despawn path.

### World destruction

World destruction invalidates its scopes before freeing world-owned systems.
Later scope finalization becomes a safe reference cleanup, not a second
despawn/remove pass. A scope retains the world while live only if that does not
create a cycle; otherwise use the established weak back-reference/invalidation
pattern.

## Implementation sequence

### Phase 0 — Inventory lifecycle operations

Create an internal table for each supported type:

| Type | Current owner | Public add/remove | Stale validation | Shared allowed |
|---|---|---|---|---|
| Entity3D | World3D registry | Spawn/Despawn | current stale entity rules | no mutable double ownership |
| SceneNode | SceneGraph | Add/Remove child/node API | inspect node graph membership | no |
| PhysicsBody3D | PhysicsWorld3D | `Add`/`TryAdd`/`Remove` | `ContainsBody`/body world field | no |
| Light3D | Canvas/World slots | current light add/clear/remove behavior | inspect slot ownership | possibly shared asset, not shared registration |
| Particles/Decal | EffectRegistry or manual | Add/Clear/expiry | inspect registry item | no registration duplication |
| SoundSource3D | Sound3D/audio backend | Play/Stop | inspect source validity | no live voice duplication |

Do not code a generic destructor until every row has a safe operation.

### Phase 1 — ADR and internal representation

1. Choose a tagged entry containing retained object, type, owner reference or
   stable membership data, and insertion sequence.
2. Use one growable entry array plus a process/world ownership index, or typed
   arrays if that materially simplifies safe release. Document complexity.
3. Define capacity/overflow/OOM behavior. Registration failure must leave the
   original object untouched and return/diagnose consistently.
4. Define child relationship and cycle rejection.
5. Append a class ID and full source headers.

### Phase 2 — Core scope lifecycle

Implement constructor, typed track methods, child creation, release, finalizer,
counts, and diagnostics. Requirements:

- tracking retains only after all validation/allocation succeeds;
- duplicate-same-scope returns the original object without another retain;
- released scopes reject tracking;
- release can be called twice;
- finalizer calls the same safe release core;
- an exception/trap during one resource removal must not leave the rest
  permanently unreleasable—prefer non-trapping internal validated operations;
- zero per-frame work; scopes act only on registration/release.

### Phase 3 — World integration

1. Add `World3D.CreateScope(name)` convenience if approved.
2. Register live scopes with the world for invalidation/destruction.
3. Ensure manual `World3D.Despawn`, raw `PhysicsWorld3D.Remove`, or node
   removal updates or safely coexists with the scope entry. Scope release
   should observe already removed state and drop the reference.
4. Expose scope counts in diagnostics only if useful; avoid a global debug API
   for one test.
5. Verify no reference cycle among world, scope, child scope, and entities.

### Phase 4 — Runtime registration

Add public declarations, `.def` class, qualified signatures, CMake sources,
disabled-graphics symbols, runtime docs, and surface audits. If typed methods
create excessive API surface, revisit a safe common ownership protocol through
ADR; do not replace type safety with unchecked `obj`.

### Phase 5 — Tests

Implement every lifecycle sequence from the shared matrix plus:

- mixed resource release order observed through counts;
- same object tracked twice in one scope;
- cross-scope duplicate rejection and explicit transfer if provided;
- parent/child release and cycle rejection;
- entity manually despawned before scope release;
- body/node manually removed before scope release;
- self-expiring effect before scope release;
- stopped sound before scope release;
- world destroyed with live scopes;
- scope released while world remains usable;
- 100+ level load/unload cycles with stable entity/body/node/effect counts;
- partial setup failure after each entry type;
- disabled-graphics behavior;
- no effect on shared mesh/material/template lifetimes.

Add a Zia fixture that builds a small mixed level, releases it, recreates it,
and prints before/after counts. Run twice for deterministic ordering.

### Phase 6 — Adoption spike

Before declaring the API complete, replace only the ownership arrays in small
test copies of:

- Ashfall LevelBase;
- one Ridgebound environment subsystem;
- bowling lane setup/reset.

Do not merge full migrations here. Record missing types/operations and adjust
the ADR/API before freeze.

## Performance and limits

- registration may allocate/grow; release is linear in tracked resources;
- no per-frame scan or allocation;
- membership lookup must be bounded—linear lookup is acceptable only with a
  documented small cap, otherwise maintain an index;
- release of 10,000 tracked objects should be measured and must not exhibit
  quadratic behavior;
- counters use checked sizes and reject unreasonable growth per runtime policy.

## Validation

Run focused scope/lifecycle tests, existing Game3D stale/destroy tests,
runtime robustness, graphics3d label, surface audits, disabled graphics,
platform policy, sanitizer lane where available, and full build scripts.

## Acceptance criteria

- The mixed-level fixture returns entity/body/node/effect/audio counts to its
  baseline after repeated release/recreate cycles.
- Release is idempotent under every early/manual removal sequence.
- Cross-scope double ownership cannot silently occur.
- World and scope destruction order cannot UAF or leak a cycle.
- The three adoption spikes require no direct internal free/remove access.
- Public surface, docs, disabled graphics, and all audits pass.

## Stop conditions

Stop if a supported resource lacks a safe public/internal removal operation, if
generic object tracking would require guessing type layout, or if world/scope
retains create an unresolved cycle. Narrow v1 support or add the prerequisite
lifecycle API through ADR rather than shipping unsafe ownership.

## Handoff evidence

Provide the type lifecycle table, release-order trace, ownership-index design,
cycle analysis, repeated-load counts, adoption spike diffs, API dump diff, and
ADR.

