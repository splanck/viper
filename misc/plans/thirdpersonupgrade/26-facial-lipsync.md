# Plan 26 — `LipSync3D` Amplitude Visemes + Blink/Gaze Layer

## 1. Objective & scope

Make conversations look alive without an animation team: amplitude-envelope lip sync (voice level → jaw/mouth morph weights), a procedural blink layer, and conversational gaze via the existing LookAt IK. This is deliberately the pragmatic tier — envelope-driven mouths plus blinks and eye contact cover the "characters feel dead" problem; phoneme-accurate visemes are out.

**In scope:** (a) per-voice output-level tap in the mixer; (b) `LipSync3D` driving `MorphTarget3D` weights (or a jaw bone) from a playing voice; (c) procedural blink; (d) gaze binding sugar over LookAt IK.
**Out of scope:** phoneme analysis/text-to-viseme timing, facial mocap import (morphs already import via glTF), emotion poses (games layer additive states).

**Zero external dependencies — absolute.** RMS envelope is basic DSP.

## 2. Current state (verified anchors)

- **No voice-level query exists:** the mixer exposes no per-voice output level (verify `vaud` surface at write time); plan 24 adds per-voice occlusion taps — the level tap rides the same per-voice slot work (coordinate landing order: 24's mixer refactor first, or this plan carries the slot if built first).
- **Morph weights are live-settable:** `MorphTarget3D.set_weight_by_name` (`rt_morphtarget3d.h`), with normal/tangent deltas and packed-delta payloads consumed by all backends (`rendering3d.md` §MorphTarget3D). glTF morph import works incl. async path (`game3d.md` §Assets3D).
- **LookAt IK exists with weights:** `rt_ik_solver3d_look_at` + `set_weight/set_target` (`rt_iksolver3d.h`), applied via `AnimController3D.SetIKSolver`/`Animator3D.setIKSolver` (`game3d.md` §Animator3D — "look-at bones… driven from gameplay").
- **Bone alternative:** rigs without mouth morphs can drive a jaw bone — additive layer or direct bone pose via the controller's pose hooks (plan 07's override precedent); v1 supports a single-bone jaw rotate.
- **Dialogue integration point:** plan 25's `sayVoiced` knows the active voice id + speaker entity — one call wires lip sync per line.
- **Determinism:** blink randomness must be seeded (LCG per component, plan-23 convention).

## 3. Design

### 3.1 Mixer level tap

`rt_sound_voice_get_level(voiceId) -> f64` — per-voice RMS over the last mixed block (pre-attenuation source level, so distance doesn't close the mouth), computed only for voices flagged `rt_sound_voice_enable_metering(voiceId, 1)` (zero cost otherwise; one float store per metered voice per block — no allocation). Game-thread read is a snapshot (mixer conventions).

### 3.2 `LipSync3D`

New C `src/runtime/graphics/3d/rt_game3d_facial.c`:

- `New(entity)` (requires `entity.anim` or a bound `MorphTarget3D`); target binding: `bindMouthShape(shapeName, weightScale)` — up to 4 shapes with per-shape scale (e.g. `jawOpen` 1.0, `mouthFunnel` 0.3); or `bindJawBone(boneName, axis, maxDegrees)`.
- `drive(voiceId)`: while the voice plays, each world step reads the level, smooths it (attack 0.04 s / release 0.12 s envelope follower — snappy open, soft close), applies `weight = curve(envelope)` (soft-knee gamma 0.7) to bound shapes/bone; on voice end, eases to 0 and auto-releases. `stop()` manual release.
- Ticked in the world step **after** animations, **before** the render sync (morph weights are consumed at draw; jaw bone writes use the post-anim override slot, ordered with IK/ragdoll per plan 07's fixed order: IK → ragdoll → facial).

### 3.3 Blink + gaze

- `setBlink(enabled, shapeName, minInterval, maxInterval)`: seeded-LCG intervals (default 2–6 s), 0.12 s close/open envelope; suppressed while a blink shape is being driven by lip sync bindings (never fight authored blends: additive-max composition).
- `setGaze(targetEntity | vec3 | null)`: sugar that owns a LookAt `IKSolver3D` on the entity's controller (creates/binds head bone by `bindHeadBone(name)`), eases IK weight 0→1 on set and back on clear — conversation eye contact in two calls. Plan 25 docs show `setGaze(player)` per line.

## 4. Implementation steps

1. Mixer metering slot + `get_level` + enable flag; mixer unit test (sine at known amplitude ⇒ RMS within tolerance; unmetered voice constant-cost assert).
2. `LipSync3D` envelope follower + morph binding; C test with a scripted level sequence (weight tracks envelope with attack/release timing).
3. Jaw-bone mode via the post-anim override slot.
4. Blink layer + seeded determinism test.
5. Gaze sugar over LookAt IK + head-bone binding.
6. Plan-25 wiring (`sayVoiced` auto-drives a bound `LipSync3D` if attached: `Entity3D.attachLipSync`).
7. runtime.def + audits + ADR + docs (`game3d.md` §Dialogue subsection + `rendering3d.md` cross-ref).
8. Zia probe `g3d_test_game3d_lipsync_probe`: voiced line on the skinned fixture with a mouth morph ⇒ weight nonzero during speech, zero after, blink fires deterministically; replay ×2 identical.

## 5. Public API changes (runtime.def)

```
Viper.Sound.Voice (existing class): RT_METHOD("EnableMetering","void(i64,i1)"), RT_METHOD("GetLevel","f64(i64)")
RT_CLASS_BEGIN("Viper.Game3D.LipSync3D", Game3DLipSync3D, "obj", Game3DLipSyncNew)   /* New(entity) */
    RT_METHOD("bindMouthShape","obj(obj,str,f64)",…)   RT_METHOD("bindJawBone","obj(obj,str,i64,f64)",…)
    RT_METHOD("bindHeadBone","obj(obj,str)",…)
    RT_METHOD("drive","void(obj,i64)",…) RT_METHOD("stop","void(obj)",…)
    RT_METHOD("setBlink","void(obj,i1,str,f64,f64)",…)
    RT_METHOD("setGaze","void(obj,obj)",…)              /* Entity3D | Vec3 | null */
    RT_PROP("driving","i1",get) RT_PROP("level","f64",get)
RT_CLASS_END()
```

Plus `Entity3D.attachLipSync`. Leaf `LipSync3D` unique. New file → source-health; ADR `00xx-game3d-facial.md` (mixer metering noted as audio-lib surface).

## 6. Tests

- **Metering (mixer unit):** 0.5-amplitude sine ⇒ RMS ≈ 0.353 ± 5%; silence ⇒ 0; metering off ⇒ level reads 0 and process cost unchanged (fail-before: no API).
- **Envelope:** step-function level ⇒ weight reaches 90% within attack window, decays within release window.
- **Composition:** lip sync + authored morph on a *different* shape coexist; same-shape blink suppression holds.
- **Jaw mode:** bone rotation proportional to envelope, clamped at maxDegrees, ordered after animation (pose assert with a moving base animation).
- **Gaze:** head bone converges toward target within ease time; clearing eases back to animation.
- **Determinism:** blink timeline identical across replays (seeded); driving from a deterministic-clip voice replays identically.

## 7. Verification gates

Full build + ctest incl. audio-lib suite; `-L graphics3d`; surface audits; determinism gate (facial ticks in stepSimulation); `-L slow`; cross-platform smoke for the mixer change.

## 8. Risks & constraints

- **Coordinate with plan 24's mixer work** — both add per-voice slots; land the slot infrastructure once (whichever plan goes first carries it; the other rebases).
- **Envelope quality ceiling:** amplitude sync reads as "puppet mouth" on close-ups — set expectations in docs (works at conversation camera distance; that's the target).
- **Override ordering** (IK → ragdoll → facial) is now a three-way contract — one shared test pins it.
- Metering reads source level pre-spatialization by design (distance shouldn't mute the mouth); documented.
