# Plan 09 — Capability-Resolved Quality Profile System

## Outcome

Turn a requested Performance/Balanced/Cinematic level into one immutable,
capability-resolved `QualityProfile3D` shared by renderer, environment,
effects/audio budgets, and game-specific systems. Preserve authored post-FX and
keep current `Canvas3D.SetQuality` and `Game3D.Quality.Apply` compatible.

## Problem statement

Viper already has two useful quality layers:

- `Canvas3D.SetQuality` installs backend-safe post-FX profiles and exposes
  requested/active/fallback status;
- `Game3D.Quality.Apply` sets world quality, frustum culling, and backend-safe
  shadows/cascades.

Real games need a wider decision. Ridgebound separately updates Canvas3D,
authored post-FX, terrain, water, weather, simulation, and occlusion. Ashfall
sets shadow tier, authored post-FX, render scale, light budget, and effect
counts. Bowling has its own two-tier shadow/post-FX settings. The duplicated
numbers drift and backend fallbacks are not visible to every subsystem.

## Dependencies

- plan 05 environment registry;
- plan 08 asset policy only for optional profile/config loading; built-in
  profiles must not require files;
- plan 11 will consume effect/audio budget fields but may land later;
- current Canvas3D quality-profile docs/tests;
- API register and risk R9.

## ADR and API

Required: new public immutable class/result, extension of existing Quality
semantics, and a cross-subsystem policy contract. The ADR must define requested
versus active tier, capability fallback, authored override behavior, field
defaults/ranges, and compatibility wrappers.

## Scope

In scope:

- deterministic resolve from world/backend capabilities plus requested level;
- immutable renderer/environment/gameplay budget fields;
- explicit apply of runtime-owned fields;
- profile comparison/generation and quality-change-only work;
- current helper compatibility;
- menu persistence recipe and demo adoption spikes.

Out of scope:

- automatic dynamic-resolution controller based on frame time;
- overriding every authored post-FX chain;
- game-specific enemy count/difficulty changes;
- loading arbitrary external presets in v1;
- making unsupported GPU features look supported.

## Primary source owners

- `rt_game3d_presets.c` current `Quality.Apply`;
- Canvas3D quality/post-FX/render-scale/shadow capability paths;
- new focused Game3D quality-profile implementation;
- environment/effect internal apply hooks;
- defs/class ID/CMake, docs and quality fixtures;
- Ridgebound/Ashfall/bowling quality modules for spikes.

## Design contract

`Quality.Resolve(world, requested)` returns a non-null immutable decision:

- `RequestedLevel`: sanitized caller request;
- `ActiveLevel`: highest coherent tier after backend capability fallback;
- `Fallback` and a stable diagnostic reason;
- render values: render scale, post-FX level, shadow quality/cascades, light
  budget, occlusion;
- content values: particle scale, decal budget, vegetation density, terrain LOD
  bias, water quality, animation update scale.

Resolution does not mutate the world. `Quality.ApplyProfile` applies only fields
owned by the runtime. Game-specific systems read the same object and apply
their own equivalent settings. This avoids callbacks and keeps authored policy
visible.

Authored post-FX rule:

- a profile can describe a recommended post-FX level;
- `ApplyProfile` must not replace an explicitly authored PostFX3D chain unless
  the caller opts into the standard chain;
- existing `Canvas3D.SetQuality` retains its documented standard-chain behavior;
- `Quality.Apply` compatibility behavior is documented and tested before any
  refactor.

## Implementation sequence

### Phase 0 — Inventory quality controls

Build a table of current knobs, ranges, capabilities, and owners:

- Canvas quality requested/active/fallback;
- post-FX features and software fallbacks;
- render scale capability;
- shadows, quality taps, cascades, bias;
- frustum/occlusion/cluster/light budgets;
- terrain LOD, water reflection/refraction/quality, vegetation density;
- particle rate/count and decal limits;
- animation LOD/update rate;
- streamed-world budgets if appropriate.

Mark knobs that do not exist. A profile field may be informational for game
code, but runtime Apply must not pretend to change an unsupported subsystem.

### Phase 1 — Freeze built-in policy table

Define explicit values for all three levels and backend fallbacks. Use current
Canvas/Game3D values as the compatibility baseline. Include:

- bounds and finite validation;
- software caps;
- Metal/D3D11/OpenGL capability differences;
- whether unsupported one feature lowers ActiveLevel or only sets a per-feature
  fallback. Prefer active tier plus detailed reason/field clamping rather than
  collapsing all quality due to one optional effect.

Add policy-table unit tests before wiring apply behavior.

### Phase 2 — ADR and immutable object

1. Approve fields/names and fallback semantics.
2. Append class ID and implement read-only object with world/backend capability
   snapshot and a profile generation/hash for equality.
3. Make Resolve allocation acceptable only on explicit settings change; return
   the same cached profile for identical world capability generation/request if
   safe.
4. Define result validity after backend/context loss or resize; caller resolves
   again when capability generation changes.

### Phase 3 — Runtime apply

Apply in a stable order:

1. validate profile belongs to a compatible live backend/world;
2. set world quality metadata;
3. set render scale if supported, recording fallback otherwise;
4. set culling/light/shadow policy;
5. apply standard post-FX only when explicitly selected;
6. apply environment terrain/water/vegetation policy through plan 05;
7. apply EffectRegistry budgets when plan 11 exists;
8. leave game-owned fields for callers.

Apply is idempotent. Reapplying an equal profile must not rebuild post-FX,
textures, terrain, pools, or environment objects.

### Phase 4 — Compatibility wrappers

Refactor `Game3D.Quality.Apply` to Resolve + compatible Apply only after tests
prove the same visible behavior. Keep `Canvas3D.SetQuality` independent and
document when it replaces the post-FX chain. Do not create recursive apply calls.

### Phase 5 — Tests

Required tests:

- exact built-in values for three tiers;
- invalid request sanitization;
- capability matrix and detailed fallbacks;
- software conservative values;
- authored post-FX preserved by non-standard apply;
- explicit standard post-FX selection behaves like Canvas SetQuality;
- idempotent repeated apply with stable object/resource counts;
- environment/effect fields apply once;
- backend/context capability change requires/causes re-resolve safely;
- profile from one world rejected on incompatible world if necessary;
- menu save/load of requested tier resolves again rather than persisting active
  hardware result;
- no per-frame allocation/work;
- current quality fixtures remain green.

### Phase 6 — Demo adoption spikes

- Bowling maps its two UI choices to Performance/Cinematic and preserves its
  authored look.
- Ridgebound replaces repeated tier constants with one profile while each
  subsystem reads its field.
- Ashfall reads render scale/light/effect/shadow fields while preserving its
  authored post-FX and gameplay-neutral quality behavior.

Compare captures and subsystem settings before/after. Difficulty/entity AI
counts must not change with visual quality unless already intentionally tied.

### Phase 7 — Docs and diagnostics

Document requested versus active/fallback, standard versus authored post-FX,
per-field policy, platform differences, and a settings-menu recipe. Display
fallback reason through existing debug/quality surfaces where possible.

## Performance budget

- Resolve/Apply occurs on settings/capability change, not per frame;
- equal apply performs no resource reconstruction;
- immutable profile reads are constant-time;
- environment/effect apply work is bounded by registered resources and only on
  change.

## Validation

Run Canvas quality profiles, Game3D presets, post-FX parity, render-scale,
shadow, environment, effects, demo quality probes, graphics3d label, surface
audits, disabled graphics, all backends, and full platform builds.

## Acceptance criteria

- All three games can derive their quality decisions from one profile.
- Authored post-FX is not silently replaced.
- Current Quality.Apply/Canvas SetQuality compatibility tests pass.
- Fallbacks are explicit, deterministic, and capability-correct.
- Reapply is idempotent and off the per-frame hot path.
- Backend captures and surface/docs gates pass.

## Stop conditions

Stop if a single ActiveLevel cannot express partial capability fallback without
lying, if apply requires game callbacks, or if preserving authored post-FX
would break current public behavior. Refine the result/apply split through ADR.

## Handoff evidence

Provide knob inventory, policy table, capability matrix, idempotence resource
counts, authored-look captures, demo spike settings, surface diff, and ADR.

