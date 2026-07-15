# Plan 24 — Audio Immersion: Reverb Zones, Geometry Occlusion, Ambient Beds, Dialogue Ducking

## 1. Objective & scope

Four coordinated audio features that turn "sounds play" into "the world sounds inhabited": **reverb zones** (cave echo begins at the cave mouth), **geometry occlusion** (muffled sources behind walls), **ambient beds** (per-zone loops with crossfade), and **auto-ducking** (music dips under dialogue). All build on the existing mix-group insert chains, crossfade machinery, and spatial-audio voice model.

**In scope:** (a) `ReverbZone3D` volumes driving a reverb-group wet blend; (b) per-voice occlusion (raycast → lowpass/gain automation); (c) `AmbientBed3D` zone loops; (d) `Audio.DuckGroup` sidechain-style ducking + dialogue-tagged voices.
**Out of scope:** HRTF, per-voice pitch (time-scale audio), sound propagation/portals (straight-line occlusion only), audio middleware-style graphs.

**Zero external dependencies — absolute.** Biquad/gain DSP already exists in the mixer.

## 2. Current state (verified anchors)

- **Mix groups + inserts:** `Audio.RegisterGroup`, `GroupAddReverb/GroupAddLowpass` (roomSize/damping/wet; cutoff/q), bypass/remove/clear; "mixer process path performs no per-block allocations" (`audio.md` §Mix Group Effects) — zones/ducking mutate *parameters* of pre-allocated inserts, never allocate in the audio thread.
- **Spatial voices:** `SpatialAudio3D.PlayAt/UpdateVoice` compute attenuation/pan per voice; `SoundSource3D`/`SoundListener3D` bind nodes/cameras and sync via the world step (`audio.md` §Spatial Audio; `game3d.md` §Sound3D). **No per-voice filter exists** — occlusion needs a per-voice one-pole/biquad tap in `vaud_mixer.c` (group lowpass proves the DSP; per-voice is a new slot).
- **Crossfade:** `Music.CrossfadeTo` with independent concurrent fades (`audio.md` §Music Crossfading) — beds reuse the fade clock pattern (not Music itself; beds are looping `Sound` voices).
- **Raycast:** `rt_world3d_raycast` with masks (`rt_physics3d.h:165`); budget precedents (plan 22 perception budget).
- **World audio step:** `Sound3D` syncs bindings after physics (`game3d.md` §Sound3D) — zone/occlusion evaluation slots there.
- **Threading:** parameter handoff to the mixer must be lock-light (the mixer's existing param reads are atomic-style snapshots; follow `vaud_internal.h` conventions at write time).

## 3. Design

### 3.1 `ReverbZone3D`

`New(min, max)` AABB (sphere variant later) + `setReverb(roomSize, damping, wet)` + `priority`. World-registered; each step, the **listener** position selects the highest-priority containing zone; the world lazily creates one dedicated `"g3d_reverb"` mix group with a single pre-allocated reverb insert, and eases its parameters toward the active zone (or wet→0 outside all zones) over `blendSeconds` (default 0.5). Sources opt in per voice: `Sound3D` positional playback routes to the reverb group by default (`setReverbRouting(false)` to opt out; UI/2D voices untouched).

### 3.2 Geometry occlusion

`Sound3D.setOcclusion(enabled, mask, maxLowpassHz, gainDb)`: for each live world-bound positional source, budgeted round-robin raycasts listener→source (default 8 rays/step, `setOcclusionBudget`); blocked ⇒ ease the voice's occlusion params toward `(lowpass maxLowpassHz, gain −gainDb)`, clear ⇒ ease back (0.15 s ease — no clicks). Requires the new **per-voice occlusion tap** in the mixer: one one-pole lowpass + gain per voice, bypassed at zero cost when inactive (pre-allocated with the voice pool, zero-alloc rule preserved).

### 3.3 `AmbientBed3D`

`New(world)`; `addZone(min, max, clip, volume)` (+ `setDefault(clip, volume)` for outside-all-zones); each step, listener zone selection crossfades the active bed loop in/out over `crossfadeSeconds` (default 2). Beds are looping voices on a dedicated `"g3d_ambience"` group (so zone reverb and ducking compose). Zone entries are data; overlapping zones resolve by priority then index.

### 3.4 Ducking

`Audio.DuckGroup(targetGroup, amountDb, attackMs, releaseMs)` installs a duck rule keyed by *trigger tag*: any playing voice tagged as a trigger (default tag: dialogue) attenuates the target group with attack/release envelope. `Sound3D.playDialogue(clip)` = `play2D` + trigger tag + routing to a `"g3d_dialogue"` group; plan 25 calls it. Rules are few (≤8), evaluated in the mixer's group pre-mix with a shared envelope — no per-block allocation.

## 4. Implementation steps

1. Mixer: per-voice occlusion tap (one-pole + gain, pooled, bypassed default) + duck-rule evaluation in the group premix; C mixer unit tests (`src/lib/audio/tests/` pattern) — filter response + envelope timing.
2. `ReverbZone3D` + listener selection + eased group-reverb params; Zia-visible behavior test via voice/group state inspection.
3. Occlusion raycast loop + budget + easing (world side).
4. `AmbientBed3D` zones + crossfade.
5. `DuckGroup` + `playDialogue` + trigger tags.
6. runtime.def + audits + ADR + docs (`audio.md` new sections + `game3d.md` §Sound3D expansion).
7. Zia probe `g3d_test_game3d_audio_immersion_probe`: walk into a cave zone (reverb wet ramps), source behind wall (voice lowpass state), zone bed crossfade, dialogue ducks music — all via state/telemetry asserts, deterministic stepping.

## 5. Public API changes (runtime.def)

```
"Viper.Graphics3D.ReverbZone3D": New(min,max); setReverb(f64,f64,f64) fluent; Priority prop
World3D/Sound3D: addReverbZone(zone), setReverbRouting(i1), setReverbBlendSeconds(f64),
    setOcclusion(i1,i64,f64,f64), setOcclusionBudget(i64),
    playDialogue(clip)->i64
"Viper.Game3D.AmbientBed3D": New(world); addZone(min,max,clip,f64) fluent; setDefault(clip,f64);
    crossfadeSeconds prop
Viper.Audio.Mixer: DuckGroup(i64,f64,f64,f64)->i64, RemoveDuck(i64),
    SetVoiceTag(i64,i64) (trigger tagging escape hatch)
```

Leaves `ReverbZone3D`/`AmbientBed3D` unique. Mixer changes live in `src/lib/audio/` (its own test suite); world-side in `rt_game3d_audio.c` (existing file). ADR `00xx-audio-immersion.md`.

## 6. Tests

- **Occlusion tap (mixer unit):** white-noise voice with tap at 1200 Hz ⇒ spectrum above cutoff attenuated ≥ 12 dB (existing mixer test math); bypass ⇒ bit-identical passthrough (fail-before: no tap).
- **Duck envelope:** trigger voice on ⇒ target group gain reaches amount within attack ±1 block; release symmetric.
- **Zone selection:** listener crossing into a zone ramps group reverb wet monotonically over blendSeconds; leaving ramps to 0; overlapping zones honor priority.
- **Occlusion behavior:** wall between listener and source ⇒ voice occlusion state active within budget rounds; removing wall eases back (state asserts via a test-only voice inspection hook).
- **Beds:** zone crossing swaps loops with equal-power overlap (both voices live mid-fade, one after).
- **No-alloc:** mixer process path allocation counters flat during all of the above (existing zero-alloc test pattern in `test_vaud_core_fixes.c` lane).

## 7. Verification gates

Full build + ctest incl. the audio lib suite (`src/lib/audio/tests/`); platform lint (mixer touches per-platform audio backends only via existing interfaces); `-L graphics3d`; `-L slow`; surface audits. Cross-platform smoke (`run_cross_platform_smoke.sh`) since `src/lib/audio/` has per-OS backends.

## 8. Risks & constraints

- **Audio-thread discipline is the whole risk:** all four features mutate pre-allocated parameter slots from the game thread with the mixer's established snapshot pattern — no locks in the process path, no allocation, verified by the no-alloc test.
- **One reverb group** (listener-based) is deliberate v1 — per-source zone routing (source in cave, listener outside) is v2; document the limitation.
- **Occlusion is binary-target eased**, not distance-through-wall — cheap and convincing; note it.
- Duck rules cap at 8; registering more traps early (mirrors slot-budget conventions).
