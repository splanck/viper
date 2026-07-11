# ADR 0095: Audio Immersion — Reverb Zones, Occlusion, Ambient Beds, Dialogue Ducking

Date: 2026-07-11

## Status

Accepted

## Context

Spatial audio attenuated and panned, but the world never *sounded* like a
place: no cave echo at the cave mouth, no muffling behind walls, no per-area
ambience, no music dip under speech. The mixer already grew the hard
primitives in earlier hardening passes — a per-voice occlusion tap
(one-pole lowpass + gain, smoothed ~80 ms) and sidechain duck rules
(`Audio.SetGroupDucking`, 8-rule cap) — so this plan is the world-side layer
that drives them deterministically from the Game3D step.

## Decision

- **`ReverbZone3D.New(min, max)`** (AABB, fluent `WithReverb(room, damping,
  wet)`, `Priority`) registers on `Sound3D.AddReverbZone`. Registration
  lazily creates one `"g3d_reverb"` mix group with a single reverb insert;
  each world step the listener position selects the highest-priority
  containing zone and the insert's parameters ease toward it (wet eases to 0
  outside all zones) over `SetReverbBlendSeconds` (default 0.5).
  `get_ReverbWet` exposes the eased wet for telemetry. A new
  `Audio.GroupSetReverb` updates a reverb insert **in place** (same clamps as
  add; delay lines kept) so sweeps are click- and allocation-free.
- **Routing:** `SoundSource3D` gained a `MixGroup` property applied at the
  next `play()` (never mutating a live voice — the mixer reads a voice's
  group during render). Once a reverb zone exists, `Sound3D.PlayAt` /
  `PlayAttached` route new positional voices to the reverb group;
  `SetReverbRouting(false)` opts out. 2D/UI voices are untouched.
- **Occlusion:** `Sound3D.SetOcclusion(enabled, mask, amount)` +
  `SetOcclusionBudget` (default 8) run budgeted round-robin
  listener-to-source raycasts in the step; a blocked source's occlusion
  target is set to `amount`, a clear one to 0, and the mixer's existing
  smoothing does the easing.
- **`AmbientBed3D.New(world)`** (fluent `AddZone(min, max, clip, volume)`
  up to 16, `SetDefault(clip, volume)`, `CrossfadeSeconds` default 2,
  `ActiveZone` telemetry): the listener's zone (first containing zone wins;
  -1 = default) selects a looping bed on the `"g3d_ambience"` group with an
  equal-power crossfade; the first selection snaps so worlds don't fade in
  from silence.
- **`Sound3D.PlayDialogue(clip)`** plays on a lazily-registered
  `"g3d_dialogue"` group; games pair it with
  `Audio.SetGroupDucking("g3d_dialogue", "music", ...)`.

## Consequences

- All evaluation is game-thread parameter writes; the mixer process path
  stays zero-allocation and lock-free (verified by the existing no-alloc and
  duck/occlusion unit tests in `src/lib/audio/tests/`).
- Deferred (recorded): per-source zone routing (one listener-based reverb
  group in v1 — a source in the cave heard from outside uses the listener's
  zone), sphere zones, distance-through-wall occlusion (binary target,
  eased), and bed zone priorities (first-added wins).
- Test: `g3d_test_game3d_audio_immersion_probe` — dry outside the zone,
  monotonic wet ramp entering, ease-back leaving, wall occludes / clear path
  releases, bed default vs zone selection, dialogue voice on the trigger
  group. VM == native.

## Links

- misc/plans/thirdpersonupgrade/24-audio-immersion.md
- src/runtime/graphics/3d/rt_game3d_audio.c, src/runtime/audio/rt_audio_fx.c
- ADR 0094 (AI), ADR 0086 (dialogue), audio.md §Mix Group Effects
