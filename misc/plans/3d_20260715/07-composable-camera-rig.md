# Plan 07 — Composable Camera Rig

## Outcome

Provide a `CameraRig3D` that separates a deterministic base camera pose from
additive presentation modifiers such as recoil, shake, bob, and FOV blending.
It must reuse current follow/orbit/third-person collision behavior, remain
independent of character input policy, and never mutate authoritative gameplay
entities.

## Problem statement

Viper has strong individual controllers: free-fly, orbit, follow, first-person,
third-person with collision/aim/target lock, rail camera, and timeline camera.
Games still build composition around them:

- Ridgebound owns a custom camera for traversal framing and environment feel;
- Ashfall owns first-person look, viewmodel response, recoil, bob, damage
  feedback, FOV, and menu/pause transitions;
- bowling owns aiming, follow, and replay camera states.

The gap is not another single controller. It is a stable base-pose plus modifier
pipeline usable across controllers and game states.

## Dependencies

- plan 03 fixed/render timing and interpolation alpha;
- plan 10 entity-aware collision queries where camera obstruction is needed;
- existing third-person/target-lock/rail/timeline ADRs and tests;
- API register and risk R7.

## ADR and API

Required: new class/C ABI, controller-slot integration, phase order, seeded
modifier semantics, and ownership. The ADR must decide whether CameraRig3D is a
recognized World3D camera controller or a service updated explicitly by
FrameDriver/GameBase3D. It must not be updated twice.

## Scope

In scope:

- first-person, follow/orbit, and third-person base pose modes sufficient for
  adoption spikes;
- target/yaw/pitch/distance/pivot/FOV state;
- collision-aware boom through shared third-person helpers;
- additive recoil, shake, bob, and FOV blend;
- snap/reset/pause/time-domain behavior;
- deterministic seeded noise;
- current controller compatibility and adoption spikes.

Out of scope:

- cinematic editing/sequencing already covered by Timeline3D/RailCamera3D;
- split-screen/multiple active world cameras;
- viewmodel rendering itself;
- game-specific aim assist;
- replacing TargetLock3D.

## Primary source owners

- `rt_game3d_controllers.c`, `rt_game3d_thirdperson.c`, rail/timeline code for
  shared helpers and lifecycle patterns;
- new focused camera-rig implementation/header;
- World3D controller validation/update/late-update dispatch;
- defs/class ID/CMake real and disabled variants;
- camera/controller unit tests, Zia fixtures, docs, and starters.

## Composition model

Each update produces:

```text
authoritative target pose
  -> base mode pose
  -> collision/occlusion correction
  -> deterministic smoothing/interpolation
  -> additive recoil
  -> additive bob
  -> additive seeded shake
  -> FOV blend
  -> write Camera3D only
```

Modifier order is a public deterministic contract. Base pose reads entity
state but never writes it. Collision correction uses World3D/Physics queries
and the same instant pull-in/smoothed release rule as current third-person
behavior unless the ADR documents otherwise.

Camera modifiers use a selected time domain:

- recoil return and gameplay bob normally scaled/fixed time;
- menu/cinematic FOV transitions may use unscaled time through an explicit
  option;
- shake uses an explicit seed and elapsed time, not global RNG.

## Implementation sequence

### Phase 0 — Characterize existing controllers

Capture deterministic camera traces for follow, orbit, first-person, and
third-person including collision pull-in, aim FOV, target lock, pause, and
teleport. Mark helpers that can be reused without changing public behavior.

### Phase 1 — Three game pose spikes

Using a script-only prototype, express:

- bowling aim/follow/replay switch with snap on state transition;
- Ridgebound third-person traversal camera with collision;
- Ashfall first-person look + recoil + bob + FOV blend.

Confirm one modifier pipeline covers all without game callbacks. Record any
game-specific policy that stays outside the rig.

### Phase 2 — ADR and base state

Define modes, target ownership/stale behavior, angle units/clamps, camera
projection ownership, smoothing, collision masks, update phase, one-rig-per-
world rules, and interaction with `World3D.SetCameraController`.

Prefer extracting current third-person boom/collision math into private shared
helpers used by both classes. Do not instantiate one public controller inside
another if that creates competing world slots or reference cycles.

### Phase 3 — Base modes

Implement and test one mode per commit:

1. first-person target/pivot plus yaw/pitch;
2. follow/orbit base with distance/pivot;
3. third-person collision-aware boom using shared query/helper;
4. optional target-lock delegation to existing TargetLock3D if required by
   adoption, not a duplicate target selector.

Mode switch preserves or resets yaw/pitch/distance according to explicit
methods. `Snap()` clears smoothing debt after teleport/scene load.

### Phase 4 — Modifier pipeline

Implement bounded channels:

- recoil impulses accumulate with caps and deterministic exponential/critical
  return;
- shake slots have amplitude/frequency/duration/seed and fixed maximum count;
- bob receives game-computed weight/frequency; the rig does not inspect
  footsteps or stamina;
- FOV target blends with validated range and duration.

Pool modifier slots; no per-event allocation after construction. Define
overflow priority and a dropped-modifier diagnostic counter.

### Phase 5 — Interpolation and state authority

Use FrameDriver interpolation only to sample visual target pose where current
World3D render interpolation supports it. Never write interpolated pose back to
entity/node/physics. Verify camera output is stable when multiple render frames
occur between fixed steps.

Define whether look intent is frame-rate or fixed-step sampled. Preferred:
accumulate frame input in app/input layer, consume at fixed update, and run
camera late update after simulation with interpolation alpha.

### Phase 6 — Registration and tests

Add public defs, ID, real/disabled symbols, CMake, docs, and audits. Tests:

- base modes match current controller traces where configured equivalently;
- wall collision/no clip and started-penetrating case;
- teleport then Snap has no long catch-up;
- recoil exact return curve and cap;
- two seeded shake runs are identical; different seeds differ;
- bob weight zero is a no-op;
- FOV blend/pause/time-domain behavior;
- target despawn/world destruction safety;
- mode switching and target lock compatibility;
- authoritative entity transforms unchanged with all modifiers enabled;
- no steady-state allocation and bounded modifier overflow;
- VM/native Zia surface use and disabled graphics.

### Phase 7 — Adoption spikes

Run the three script spikes against the real class. Compare camera traces and
fixed captures to original game cameras. Do not merge full game rewrites here.
Document retained game-side policies such as replay path selection, viewmodel
sway, or traversal-specific framing.

## Performance budget

- one configured obstruction query per camera update at most;
- no heap allocation per update/modifier hit after warm-up;
- bounded modifier slots and constant-time update per slot;
- no scene traversal beyond existing target/query work;
- no duplicate update when installed in World3D.

## Validation

Run camera controllers, third-person, target lock, timeline/rail, physics query,
FrameDriver determinism, graphics3d, surface audits, disabled graphics, all
backends, and full builds.

## Acceptance criteria

- All three game camera spikes fit the base+modifier model.
- Existing controllers retain their behavior and public surface.
- Collision, smoothing, recoil, bob, shake, and FOV compose deterministically.
- Modifiers never mutate authoritative entity/physics state.
- No double controller update, reference cycle, or steady-state allocation.
- Surface/docs/backend gates pass.

## Stop conditions

Stop if the rig must duplicate third-person collision code, own a character,
invoke per-frame script callbacks, or write target transforms. Extract a common
private helper or narrow the initial modes.

## Handoff evidence

Provide existing-controller trace comparisons, modifier-order/state table,
three adoption spike captures, authoritative-state proof, allocation/overflow
measurements, surface diff, and ADR.

