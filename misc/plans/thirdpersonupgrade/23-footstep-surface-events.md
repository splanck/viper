# Plan 23 — `Footsteps3D`: Surface-Driven Footstep Audio/VFX/Decals

## 1. Objective & scope

The immersion detail whose absence reads as "indie": footsteps that sound like the ground being walked on. Bind an entity + animator, fire on animation events (or an automatic foot-bone fallback), raycast the ground, look up the surface tag (plan 20), and play the configured per-surface sound/VFX/decal. One shared surface-table object serves every character.

**In scope:** (a) `SurfaceTable3D` (per-surface audio sets/VFX preset/decal); (b) `Footsteps3D` binding component; (c) anim-event and auto-detect triggers; (d) optional `reportSound` hook (plan 22 hearing).
**Out of scope:** cloth/armor foley layers (games add via the same anim events), IK foot planting (separate concern; plan-14-adjacent future), surface *particles* beyond the existing `Effects3D` presets.

**Zero external dependencies — absolute.** Depends on plan 20 (surface tags).

## 2. Current state (verified anchors)

- **Anim events exist end-to-end:** `AnimController3D.AddEvent(state, time, name)` (`rt_animcontroller3d.h` event API) captured per frame through `Animator3D.eventCount()/eventName(i)` (`game3d.md` §Animator3D) — the trigger source.
- **Bone poses:** `Animator3D.GetBoneMatrix/FindBone` (`game3d.md` §Animator3D) — the auto-detect fallback reads foot-bone heights.
- **Ground query:** `rt_world3d_raycast` down from the foot (`rt_physics3d.h:165`); surface tag from plan 20's `PhysicsHit3D.SurfaceType`; character ground shortcut `Character3D.GetGroundSurfaceType` (plan 20/03).
- **Playback targets:** `Sound3D.playAt(clip, pos)` with world attenuation defaults (`game3d.md` §Sound3D); `Effects3D.Dust(world, pos)`-style presets and `Decal3D` fading projected quads (`game3d.md` §Effects3D; `rt_decal3d.h`).
- **Variance precedent:** `Viper.Audio.SoundBank` (round-robin/random clip sets, `audio.md` §SoundBank) — per-surface clip sets reuse it.
- **Determinism:** clip-variant selection must not use wall-clock randomness — a per-component LCG seeded at bind (deterministic replays stay identical).

## 3. Design

### 3.1 `SurfaceTable3D` (shared data object)

New C in `rt_game3d_interact.c`'s sibling `rt_game3d_footsteps.c`:

- `New()`; per-surface rows keyed by surface id (plan 20 registry): `setAudio(surfaceId, soundBank)` (or `setAudioClips(surfaceId, seq)` building an internal bank), `setVfx(surfaceId, kind)` (enum over the `Effects3D` presets: None/Dust/Splash-style), `setDecal(surfaceId, pixels, size, lifetime)`, `setLoudness(surfaceId, f64)` (hearing stimulus scale).
- Row 0 (untyped surface) = default row; unset lookups fall back to it; fully unset ⇒ silent no-op.
- Tables are data: games load clip names from their own config; ids resolved by name via `Surfaces.IdOf` at table-build time (session-scoped ids, plan 20 rule).

### 3.2 `Footsteps3D` (per-entity binding)

`New(entity, table)`; requires `entity.anim`; `Entity3D.attachFootsteps` slot; ticked in the world step after animations:

- **Event mode (primary):** consumes animator events named `footstep` / `footstep_l` / `footstep_r` (configurable prefix `setEventPrefix`); each firing triggers one step at the corresponding foot bone's world position (bone names via `setFeetBones(left, right)`; unset ⇒ entity origin).
- **Auto mode (fallback, `setAutoDetect(true)`):** for rigs without authored events — per-step, a foot bone's height (relative to entity origin) crossing below a threshold with downward velocity triggers a step; hysteresis prevents double-fires. Explicitly a fallback; docs push authored events.
- **Per step fired:** downward raycast (0.5 m, `groundMask`) from the foot point → surface id (miss ⇒ character-ground shortcut when available ⇒ default row); play: bank clip via `playAt` (volume scaled by `setVolumeScale`, and by speed: `min(1, |entityVelocity|/runSpeedRef)`), VFX preset at hit point, decal at hit point/normal, and optional `World3D.reportSound(pos, loudness, tagFootstep)` when `setReportHearing(true)` (plan 22 stimulus — sneaking games get guard hearing for free).

## 4. Implementation steps

1. `SurfaceTable3D` rows + fallback resolution; C unit tests (lookup/fallback matrix).
2. `Footsteps3D` event-mode: animator event consumption + foot-bone positions + raycast + audio dispatch; unit test with a scripted controller and two surface floors (grass/stone rows → distinct clips chosen).
3. VFX + decal dispatch + loudness/hearing hook.
4. Auto-detect mode + hysteresis; test on a synthetic walk cycle.
5. Deterministic variant selection (seeded LCG) + replay test.
6. runtime.def + audits + ADR + docs (`game3d.md` §Footsteps under the audio/VFX section, with the grass/stone recipe).
7. Zia probe `g3d_test_game3d_footsteps_probe`: skinned agent walks grass→stone tile boundary; assert clip-source switch (via `Sound3D` voice inspection) + decal count; deterministic replay ×2.

## 5. Public API changes (runtime.def)

```
RT_CLASS_BEGIN("Viper.Game3D.SurfaceTable3D", Game3DSurfaceTable3D, "obj", Game3DSurfaceTableNew)
    RT_METHOD("setAudio","obj(obj,i64,obj)",…)          /* fluent; SoundBank */
    RT_METHOD("setVfx","obj(obj,i64,i64)",…)            /* Effects preset enum */
    RT_METHOD("setDecal","obj(obj,i64,obj,f64,f64)",…)  /* pixels, size, lifetime */
    RT_METHOD("setLoudness","obj(obj,i64,f64)",…)
RT_CLASS_END()
RT_CLASS_BEGIN("Viper.Game3D.Footsteps3D", Game3DFootsteps3D, "obj", Game3DFootstepsNew)  /* New(entity, table) */
    RT_METHOD("setFeetBones","obj(obj,str,str)",…) RT_METHOD("setEventPrefix","obj(obj,str)",…)
    RT_METHOD("setAutoDetect","obj(obj,i1)",…)     RT_METHOD("setGroundMask","obj(obj,i64)",…)
    RT_METHOD("setVolumeScale","obj(obj,f64)",…)   RT_METHOD("setReportHearing","obj(obj,i1)",…)
    RT_PROP("StepCount","i64",get)                  /* lifetime telemetry for tests */
RT_CLASS_END()
```

Plus `Entity3D.attachFootsteps`. Leaves unique; new file → source-health; ADR `00xx-game3d-footsteps.md` (or fold under a Tier-3 gameplay ADR with 21).

## 6. Tests

- **Surface switch (C unit):** two floor bodies tagged grass/stone — steps on each resolve the correct table row (clip handle assert) (fail-before: no API).
- **Fallback:** untagged floor uses row 0; empty table ⇒ no-op, no trap.
- **Event mode:** three `footstep` events in a state ⇒ exactly three steps at bone positions (position asserts).
- **Auto mode:** synthetic sine foot bone ⇒ one step per cycle, hysteresis blocks jitter double-fires.
- **Hearing:** `setReportHearing(true)` ⇒ plan-22 perceiver receives Heard with the footstep tag and loudness scaling.
- **Determinism:** variant sequence identical across replays; `StepCount` matches.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; determinism gate (ticks in stepSimulation); `-L slow`. Depends on plan 20 landing first (surface ids) — gate the merge order.

## 8. Risks & constraints

- **Voice pressure:** rapid steps across many NPCs can flood voices — per-component min-interval (default 0.12 s) and the mixer's existing voice eviction (`AudioVoicesEvicted` diagnostic) bound it; document tuning.
- **Auto-detect quality** varies with rigs — it's the fallback, not the feature; the docs lead with authored events.
- **Decal accumulation:** lifetimes default short (4 s); registry expiry already handles cleanup (`game3d.md` §Effects3D).
- Table shared across characters is the intended memory shape — per-character tables work but are called out as waste in docs.
