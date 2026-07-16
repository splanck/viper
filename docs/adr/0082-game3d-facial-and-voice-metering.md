# ADR 0082: Game3D Facial Animation (LipSync3D) and Per-Voice Metering

Date: 2026-07-10

## Status

Accepted

## Context

Cutscenes and dialogue read as lifeless when speakers' faces are static.
Full viseme lip sync needs phoneme timing data Viper doesn't ship, but
amplitude-driven mouth movement plus blinks and gaze covers the
conversation-camera use case. The missing primitive was audible level: the
vaud mixer had no way to ask "how loud is this voice right now?"

## Decision

The vaud mixer gains opt-in per-voice RMS metering: when enabled for a voice,
both mixing paths accumulate the pre-gain normalized sum of squares for the
block and publish it as the voice level. Measuring *pre-attenuation* is
deliberate — distance/volume falloff must not close a speaking character's
mouth. Metering costs nothing when off (a flag check per voice per block).
Exposed as `Viper.Audio.Voice.EnableMetering(id)` / `GetLevel(id)`.

`LipSync3D` binds to an entity's `MorphTarget3D` plus up to four mouth shapes
with per-shape scales. `drive(voiceId)` samples the meter (x2.83 to map RMS
of full-scale sine near 1.0) and `driveLevel(level)` injects levels directly
(tests, network voice). An envelope follower (0.04 s attack / 0.12 s release)
smooths the signal and a soft-knee curve (`env^0.7`) keeps quiet speech
visible. Blinks use a seeded LCG (deterministic replay; VM/native identical)
and compose additive-max with lip sync when they share a shape. Gaze eases a
LookAt IK solver toward an entity or point through the existing IK
infrastructure. Everything ticks in `game3d_world_facial_tick`, after
animation and ragdoll sync so facial writes land on the final pose.

## Consequences

- Amplitude sync is honest about its fidelity: it reads well at conversation
  camera distance, which is the design target; close-up viseme accuracy is
  out of scope until phoneme data exists.
- Metering is a general mixer capability — music visualizers and ducking can
  build on it — not a lip-sync special case.
- Determinism holds: the blink LCG is seeded, and injected-level runs are
  byte-identical across replays (covered by test).
- Tests: `test_rt_game3d_dialogue_facial` (envelope attack/release, blink
  replay determinism); mixer metering exercised via `driveLevel`/`drive`
  paths.
