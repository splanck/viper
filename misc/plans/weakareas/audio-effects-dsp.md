# Audio Consolidation + Effects/DSP Layer

**Status:** Completed and locally verified (2026-06-20)
**Area:** `src/runtime/audio/`, `src/lib/audio/`, runtime bindings, tests, docs
**Effort:** M
**Roadmap fit:** v0.2.x hardening (audio is the thinnest non-trivial runtime module; the
graphics/audio split is an architectural wart)

## Problem

Two related issues:

1. **Audio is split across the tree.** Spatial/3D audio lives under
   `src/runtime/graphics/3d/`, not `src/runtime/audio/`:
   - `graphics/3d/audio/rt_sound3d.c`, `rt_sound3d.h`, `rt_sound3d_objects.c`,
     `rt_soundsource3d.h`, `rt_soundlistener3d.h`
   - `graphics/3d/rt_game3d_audio.c`
   - `graphics/3d/scene/rt_scene3d_spatial.c`
   This is a layering wart (audio domain code under the graphics subsystem) and hurts
   discoverability, and it couples audio to the 3D-backend build gating.
2. **No effects/DSP layer.** A grep of `audio/` finds zero reverb/delay/EQ/general
   filter (only a private one-pole lowpass inside `rt_musicgen.c`). Dry-only playback is
   a real gap for a game platform.

## Completion notes

- Spatial-audio implementation files moved from `src/runtime/graphics/3d/audio/`
  to `src/runtime/audio/`. `rt_sound3d.c` now compiles as an audio source without
  `VIPER_ENABLE_GRAPHICS`; `rt_sound3d_objects.c` remains graphics-gated because
  the object wrappers bind to `SceneNode3D` and `Camera3D`.
- `rt_game3d_audio.c` and `rt_scene3d_spatial.c` remain thin graphics-side
  scene/world integration shims that call the audio-owned spatial API.
- The former `rt_game3d_diagnostics.h` dependency from `rt_sound3d.c` was replaced
  with `rt_audio_diagnostics.{h,c}`. The existing Game3D diagnostics surface reads
  and resets that audio-owned counter for compatibility.
- Mix-group effects are implemented in `rt_audio_fx.{h,c}` and attached through
  the existing integer mix-group model. ViperAUD now supports one group bus
  processor per group so effects process the group sum once before master mixing.
- Public functions added in `runtime.def`:
  `GroupAddLowpass`, `GroupAddHighpass`, `GroupAddPeaking`, `GroupAddDelay`,
  `GroupAddReverb`, `GroupSetFxBypass`, `GroupRemoveFx`, and `GroupClearFx`.
- Focused verification passed:
  `test_vaud_core_fixes`, `test_rt_sound3d_contract`,
  `test_rt_sound3d_objects`, `test_rt_game3d_diagnostics`,
  `test_rt_audio_integration`, `test_rt_audio_fx`,
  `test_rt_audio_surface_link`, `test_rt_graphics_surface_link`,
  `test_runtime_name_map`, `test_runtime_classes_catalog`, and
  `test_runtime_surface_audit`.
- Graphics-disabled build verification passed with
  `VIPER_BUILD_DIR=build-nographics VIPER_EXTRA_CMAKE_ARGS='-DVIPER_GRAPHICS_MODE=OFF -DVIPER_BUILD_TESTING=OFF'`
  after adding the missing Canvas3D backend metric stubs and cleaning stale
  warning-as-error blockers in rebuilt toolchain targets.

---

## Part 1 — Consolidate all audio under `audio/`

### Design
Move the audio-domain state and DSP/positioning math into `src/runtime/audio/` and invert
any residual coupling so that **audio never depends on graphics**; instead the 3D scene
calls *into* the audio API with plain coordinates or common math values. Keep thin
graphics-side shims only where a scene object needs to push transform updates into audio.

Bonus: per the macOS backend build note (only Metal+SW+`*_shared` 3D TUs compile on
mac), moving audio TUs out of the 3D-backend source group **decouples spatial audio from
3D-backend build gating** — it will compile on every platform with the audio lib.

### Steps
1. **Move** `graphics/3d/audio/*` → `src/runtime/audio/` (keep filenames:
   `rt_sound3d.*`, `rt_sound3d_objects.c`, `rt_soundsource3d.h`, `rt_soundlistener3d.h`).
2. **Audit + move** `graphics/3d/rt_game3d_audio.c` and
   `graphics/3d/scene/rt_scene3d_spatial.c`. If either references 3D scene/transform
   types, keep only a thin scene-side call that passes positions to the moved audio API;
   the DSP/positioning math goes to `audio/`.
3. Replace `#include "rt_game3d_diagnostics.h"` with an audio/core diagnostics header (or
   relocate the few diagnostics counters used). Confirm no remaining `graphics/` includes
   in the moved files.
4. Audit Vec3 usage. Prefer a common `Viper.Math.Vec3`/runtime-math helper if one exists;
   otherwise pass raw coordinates across the graphics/audio boundary and keep graphics
   math out of `audio/`.
5. **CMake:** remove the moved TUs from the graphics/3d source list; add them to the
   audio library source list. Confirm headless and 3D-disabled builds still link.
6. **`runtime.def`:** keep existing `Viper.Audio.SpatialAudio3D.*` names stable. For
   `Viper.Graphics3D.SoundListener3D` / `SoundSource3D`, add `Viper.Sound.*` aliases or
   facades only if demos/tests prove the public type should move; never break existing
   code in this cleanup.
7. Run `./scripts/lint_platform_policy.sh` (the move touches `rt_platform.h` users).

---

## Part 2 — Effects / DSP chain

### Design
A reusable, real-time-safe effect set attached as an **insert chain on a mix group**
(groups are integer IDs, matching the existing API). Effects process the group bus in
the software mix path (in `rt_audio.c`) before the master sum.

```c
typedef enum { RT_FX_BIQUAD, RT_FX_DELAY, RT_FX_REVERB } rt_fx_kind;
typedef struct rt_audio_fx { rt_fx_kind kind; void *state; struct rt_audio_fx *next;
                             int bypass; float wet, dry; } rt_audio_fx;
```
- **Biquad** (`rt_biquad`): RBJ coefficients computed at set-time; LP/HP/BP/notch/peaking/shelf.
- **Delay** (`rt_delay`): preallocated circular buffer (≤ ~2 s at mix rate) + feedback.
- **Reverb** (`rt_reverb`): Freeverb (8 combs + 4 allpasses/channel); all buffers
  preallocated at create time.
- **Real-time safety:** no allocation in the process path; denormal/FTZ guard on feedback.

### Steps
1. `src/runtime/audio/rt_audio_fx.{h,c}` + `rt_biquad.{h,c}`, `rt_delay.{h,c}`,
   `rt_reverb.{h,c}`.
2. Add an `rt_audio_fx *fx_head` to the mix-group state; free on teardown.
3. Walk the chain in the group-mix loop of `rt_audio.c`.
4. Register the API (below); add trapping stubs under `#ifndef VIPER_ENABLE_AUDIO`.
5. `./scripts/check_runtime_completeness.sh`.

### API surface (`runtime.def`) — corrected to the real namespace + integer-group model
```
RT_FUNC(SndGroupAddLowpass,  rt_snd_group_add_lowpass,  "Viper.Audio.Mixer.GroupAddLowpass",  "i64(i64,f64,f64)")     // (group, cutoffHz, q) -> fxId
RT_FUNC(SndGroupAddHighpass, rt_snd_group_add_highpass, "Viper.Audio.Mixer.GroupAddHighpass", "i64(i64,f64,f64)")
RT_FUNC(SndGroupAddPeaking,  rt_snd_group_add_peaking,  "Viper.Audio.Mixer.GroupAddPeaking",  "i64(i64,f64,f64,f64)") // (group, freq, q, gainDb)
RT_FUNC(SndGroupAddDelay,    rt_snd_group_add_delay,    "Viper.Audio.Mixer.GroupAddDelay",    "i64(i64,f64,f64,f64)") // (group, ms, feedback, wet)
RT_FUNC(SndGroupAddReverb,   rt_snd_group_add_reverb,   "Viper.Audio.Mixer.GroupAddReverb",   "i64(i64,f64,f64,f64)") // (group, roomSize, damping, wet)
RT_FUNC(SndGroupFxBypass,    rt_snd_group_fx_bypass,    "Viper.Audio.Mixer.GroupSetFxBypass", "void(i64,i64,i1)")     // (group, fxId, bypass)
RT_FUNC(SndGroupRemoveFx,    rt_snd_group_remove_fx,    "Viper.Audio.Mixer.GroupRemoveFx",    "void(i64,i64)")
RT_FUNC(SndGroupClearFx,     rt_snd_group_clear_fx,     "Viper.Audio.Mixer.GroupClearFx",     "void(i64)")
```

## Tests (`src/tests/runtime/`)

**Migration (Part 1):**
- `RTSound3DTests.cpp` + `test_rt_sound3d_objects.cpp`: update include paths to the new
  `audio/` location; assertions unchanged; confirm still registered and green.
- Add a build assertion (or local script smoke) that spatial audio compiles with **3D graphics
  disabled** (proves decoupling).
- `check_runtime_completeness.sh` + a name-resolution test confirming existing
  `Viper.Audio.SpatialAudio3D.*` names remain valid and any new aliases resolve.

**Effects (Part 2) — offline render, no device:**
- Biquad LP: 1 kHz + 8 kHz input → high band RMS drops after a 2 kHz lowpass.
- Delay: impulse → second impulse at exactly `round(ms/1000*rate)` samples.
- Reverb: impulse → monotonic decaying tail; RT60 within tolerance of roomSize.
- Bypass/remove/clear: identity when bypassed; clean teardown (allocation counters).
- VM↔native determinism on the offline render (pure float math).

## Documentation

- **Remove/redirect** the spatial-audio sections in `docs/graphics3d-guide.md` and
  `docs/graphics3d-architecture.md` (they now point to the audio docs).
- **Expand** the audio reference in `docs/viperlib/` (audio/sound section): document the
  relocated spatial-audio implementation under existing `Viper.Sound.*` names, the new
  effects chain, the integer
  mix-group model, and a worked example (e.g. low-pass on the "sfx" group for occlusion,
  reverb on "ambience").
- Update `docs/testing.md` for the moved test locations.
- Update `docs/codemap/` (or `codemap.md`) so the runtime map shows audio consolidated
  under `audio/`.
- One concise line in the next release notes (audio consolidated + effects added).

## Cross-platform

- Migration: pure file moves; the effects are pure C float DSP (no platform code →
  identical on mac/win/linux, preserves determinism). Re-run `lint_platform_policy.sh`.

## Risks / open questions

- **Real-time allocation:** size delay/reverb buffers at create-time from the mix rate;
  never allocate in the process path.
- **`rt_scene3d_spatial.c` coupling:** if it genuinely needs scene types, leave a thin
  scene-side shim in graphics that calls the moved audio API — but the audio math itself
  must live in `audio/`.
- **Namespace re-home:** prefer implementation relocation over public-name churn. Keep
  aliases/back-compat for any externally referenced sound3d names and verify no demos or
  examples break.
