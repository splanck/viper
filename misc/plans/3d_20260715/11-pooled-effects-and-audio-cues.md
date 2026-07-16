# Plan 11 — Named Pooled Effects and Audio Cues

## Outcome

Extend the existing World3D `EffectRegistry3D` and `Sound3D` services with
named, bounded, reusable particle/decal and audio cues. Emitting a warmed cue
must allocate nothing, reset all prior state, honor quality budgets, and expose
drop/eviction telemetry.

## Problem statement

Game3D already tracks expiring particles/decals and Sound3D tracks sound
sources. `Effects3D` offers common presets. The games still implement feedback
policy:

- bowling has particle/decal/celebration/trail management;
- Ridgebound owns weather particles, sprint dust, decals, ambient/spatial audio;
- Ashfall has effect, light, projectile, and audio mix systems with bounded
  counts and quality scaling.

The gap is reusable named cue registration, reset-on-reuse, voice/effect budgets,
and diagnostics—not another particle or audio renderer.

## Dependencies

- plan 04 scopes for level/scene cue lifetime;
- plan 09 quality profile fields;
- plan 10 world events/identity for attached cues and optional feedback routing;
- current EffectRegistry3D, Effects3D, Sound3D, audio/reverb/ambient surfaces;
- API register and risk R12.

## ADR and API

Required: extension of public runtime C ABI, pool/eviction semantics, object
reset/lifetime rules, quality behavior, and optional scope linkage. Prefer
adding members to existing classes over a parallel Feedback3D class.

## Scope

In scope:

- named particle and decal prototype pools;
- named sound cues with bounded voice pools, priority and minimum interval;
- world/position/attached playback;
- reset, reuse, expiry, scope clear, quality budgets, diagnostics;
- existing one-shot helper compatibility.

Out of scope:

- new particle simulation or audio codec/DSP;
- game-specific screen shake/light flashes (camera/lighting systems consume cue
  events separately);
- music sequencing/mix snapshots;
- arbitrary script callbacks on cue completion;
- unbounded dynamic cue registration during gameplay.

## Primary source owners

- `rt_game3d_effects.c`, effect structs in `rt_game3d_internal.h`;
- `rt_game3d_audio.c`, Sound3D source tracking/repair;
- low-level Particles3D/Decal3D/SoundSource3D reset and lifecycle methods;
- Game3D controllers definitions, public headers, docs;
- focused pool tests, sound/effect fixtures, demo feedback spikes.

## Pool contract

Registration is configuration-time:

- name is unique within its registry;
- prototype/sound retained;
- fixed initial/max pool size and lifetime/voice policy validated;
- registration may allocate/clone resources;
- failure leaves no partial cue.

Emission/playback is hot-path:

- choose an available slot, otherwise apply documented priority/oldest/drop
  policy;
- completely reset transform, velocity/emission timer, color/lifetime, decal
  fade, sound position/attachment/volume/pitch/play cursor and any backend state;
- activate and return success/handle as approved;
- zero heap allocation after warm-up;
- increment active/available/dropped/evicted counters.

Prototype semantics must be proven for each low-level type. If Particles3D or
Decal3D cannot be reset/cloned safely, add the minimal low-level internal/public
prerequisite through ADR rather than shallow-copying runtime objects.

## Implementation sequence

### Phase 0 — Inventory current feedback lifecycle

1. Trace EffectRegistry add/update/draw/expiry/clear and object ownership.
2. Trace Sound3D PlayAt/Attached, source pruning, voice limits/eviction, and
   listener/world destruction.
3. Inventory low-level reset APIs and mutable fields for particles, decals, and
   sound sources.
4. Characterize current one-shot allocations and counts under a 1,000-event
   stress fixture.
5. Map each demo's effect/audio cases to generic cues versus game-specific
   orchestration.

### Phase 1 — ADR and cue registry data

Define bounded cue entries, name index, slot arrays, active/free ordering,
priority, min interval clock domain, quality scaling, and scope ownership.
Avoid per-emission string hashing cost if callers can cache cue IDs; the ADR may
add `FindCue(name)->Integer` and ID-based Emit/Play while keeping named
convenience methods.

Define what happens when quality shrinks a pool below current active count:
allow current instances to expire and cap new activations; do not abruptly
destroy visible/audio feedback unless policy explicitly requests it.

### Phase 2 — Effect pool core

1. Implement particle cue registration and safe clone/reset.
2. Implement decal cue registration and reset/fade/lifetime.
3. Implement free/active slot selection and expiry return.
4. Integrate existing EffectRegistry Update/Draw; avoid double updating pooled
   items through legacy item list.
5. Make Clear and scope release deactivate/reset all slots then release
   prototypes/clones.
6. Add budgets and stats.

### Phase 3 — Sound cue core

1. Register sound plus voice count/priority/min interval.
2. Precreate or lazily warm a bounded source pool; define when allocation is
   permitted.
3. Implement positional and entity-attached play with full reset.
4. Handle entity despawn by stop or detach according to cue policy.
5. Reuse existing Sound3D global voice priority/repair behavior rather than
   competing with it.
6. Add dropped/evicted/active telemetry and deterministic min-interval tests.

### Phase 4 — Quality and scope integration

- apply profile particle scale/decal budget/voice budget on explicit quality
  changes;
- register cues to scene scopes or provide a scoped clear token;
- ensure world destruction invalidates pools before low-level backend teardown;
- allow persistent/global cues in a root scope and level cues in child scopes.

### Phase 5 — Compatibility

Keep `AddParticles`, `AddDecal`, `Effects3D.*`, `Sound3D.PlayAt`, and
`PlayAttached` working. They may continue one-shot behavior or internally use a
private generic pool only if visible/lifetime behavior remains identical.

Do not require users of current APIs to pre-register cues.

### Phase 6 — Tests

Effects:

- warm emit has stable allocation count;
- slot reuse resets every mutable property;
- expiry returns slot exactly once;
- pool exhaustion/drop/priority behavior;
- quality shrink/grow;
- clear/scope/world destroy order;
- legacy add/preset compatibility;
- repeated emit/update/draw deterministic counts.

Audio:

- voice reuse restarts correct sound and pose;
- attached entity moves, despawns, and scope releases;
- min interval, priority, oldest/eviction, global voice limits;
- volume/pitch reset between callers;
- listener/world destruction and repair;
- no warmed-play allocation;
- software/no-audio/disabled behavior is recoverable and observable.

Cross-system: same named cue cannot collide across effect/sound namespaces in a
confusing way; document names or IDs.

### Phase 7 — Adoption spikes

- Bowling: migrate one impact, one trail/celebration effect, and one sound.
- Ridgebound: migrate sprint dust/weather burst and a spatial cue.
- Ashfall: migrate one weapon impact/explosion and one positional sound while
  keeping game-specific light/shake orchestration.

Compare allocation counts, active limits, visual samples, and audio telemetry.

## Performance budget

- zero heap allocation for a pool hit after warm-up;
- constant-time free-slot acquisition; bounded priority scan on exhaustion;
- no per-frame name string allocation;
- update linear in active slots, not pool capacity where practical;
- explicit caps and drop counters under burst stress.

## Validation

Run Game3D effect/sound/audio immersion tests, Particles3D/Decal3D/audio unit
tests, quality/scope lifecycle, burst perf fixture, graphics3d label, surface
audits, disabled/no-audio variants, all platforms/backends, and full builds.

## Acceptance criteria

- Warmed cue playback allocates nothing and never leaks prior slot state.
- Pools are bounded with documented drop/eviction behavior and telemetry.
- Quality and scope changes are safe/idempotent.
- Current one-shot APIs remain compatible.
- Three demo spikes reduce custom pooling without absorbing game policy.
- Audio-disabled/software/backend behavior and all audits pass.

## Stop conditions

Stop if low-level objects cannot be reset safely, if pooling conflicts with
global audio voice ownership, or if a generic cue would require game callbacks.
Add the minimal reset prerequisite or narrow supported cue types.

## Handoff evidence

Provide lifecycle/reset field inventory, before/after allocation counts,
overflow traces, quality/scope tests, demo spike results, surface diff, and ADR.

