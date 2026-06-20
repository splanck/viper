# Audio Consolidation + Effects/DSP Layer

**Status:** Verified real (two parts: relocate all audio under `audio/`, add an effects chain)
**Area:** `src/runtime/audio/` (target), `src/runtime/graphics/3d/audio/` + others (source)
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

## Current state (verified)

- Core audio under `audio/`: `rt_audio.c` (playback + software mixing), `rt_mixgroup.h`
  (mix groups), decoders (`rt_mp3/ogg/vorbis`), `rt_synth.c`, `rt_playlist.c`,
  `rt_soundbank.c`.
- Public namespace is **`Viper.Sound.*`** (e.g. `Viper.Sound.Audio.Init/Update/
  RegisterGroup/SetGroupVolume`). **Mix groups are integer IDs** (`RegisterGroup → i64`,
  `SetGroupVolume(i64 group, i64 vol)`), not objects.
- `rt_sound3d.c` includes only `rt_game3d_diagnostics.h`, `rt_platform.h`, `<math.h>` —
  i.e. it is **almost dependency-free of 3D graphics types**, so the move is low-risk.
- Tests: `src/tests/runtime/RTSound3DTests.cpp`, `src/tests/unit/test_rt_sound3d_objects.cpp`.
- Docs referencing spatial audio: `docs/graphics3d-guide.md`,
  `docs/graphics3d-architecture.md`, `docs/testing.md`.

---

## Part 1 — Consolidate all audio under `audio/`

### Design
Move the audio-domain code into `src/runtime/audio/` and invert any residual coupling so
that **audio never depends on graphics**; instead the 3D scene calls *into* the audio
API with plain coordinates. Because `rt_sound3d` already operates on plain floats + math,
this is mostly a file move + include/namespace fixups.

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
4. **CMake:** remove the moved TUs from the graphics/3d source list; add them to the
   audio library source list. Confirm headless and 3D-disabled builds still link.
5. **`runtime.def`:** audit the current registration of the sound3d/source/listener
   functions; re-home canonical names under `Viper.Sound.*` (e.g.
   `Viper.Sound.Sound3D.*`, `Viper.Sound.Listener.*`) and add `RT_ALIAS` entries for the
   old canonical names to preserve back-compat. Run `./scripts/check_runtime_completeness.sh`.
6. Run `./scripts/lint_platform_policy.sh` (the move touches `rt_platform.h` users).

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
RT_FUNC(SndGroupAddLowpass,  rt_snd_group_add_lowpass,  "Viper.Sound.Audio.GroupAddLowpass",  "i64(i64,f64,f64)")     // (group, cutoffHz, q) -> fxId
RT_FUNC(SndGroupAddHighpass, rt_snd_group_add_highpass, "Viper.Sound.Audio.GroupAddHighpass", "i64(i64,f64,f64)")
RT_FUNC(SndGroupAddPeaking,  rt_snd_group_add_peaking,  "Viper.Sound.Audio.GroupAddPeaking",  "i64(i64,f64,f64,f64)") // (group, freq, q, gainDb)
RT_FUNC(SndGroupAddDelay,    rt_snd_group_add_delay,    "Viper.Sound.Audio.GroupAddDelay",    "i64(i64,f64,f64,f64)") // (group, ms, feedback, wet)
RT_FUNC(SndGroupAddReverb,   rt_snd_group_add_reverb,   "Viper.Sound.Audio.GroupAddReverb",   "i64(i64,f64,f64,f64)") // (group, roomSize, damping, wet)
RT_FUNC(SndGroupFxBypass,    rt_snd_group_fx_bypass,    "Viper.Sound.Audio.GroupSetFxBypass", "void(i64,i64,i1)")     // (group, fxId, bypass)
RT_FUNC(SndGroupRemoveFx,    rt_snd_group_remove_fx,    "Viper.Sound.Audio.GroupRemoveFx",    "void(i64,i64)")
RT_FUNC(SndGroupClearFx,     rt_snd_group_clear_fx,     "Viper.Sound.Audio.GroupClearFx",     "void(i64)")
```

## Tests (`src/tests/runtime/`)

**Migration (Part 1):**
- `RTSound3DTests.cpp` + `test_rt_sound3d_objects.cpp`: update include paths to the new
  `audio/` location; assertions unchanged; confirm still registered and green.
- Add a build assertion (or rely on CI smoke) that audio compiles with **3D graphics
  disabled** (proves decoupling).
- `check_runtime_completeness.sh` + a name-resolution test confirming both new
  `Viper.Sound.*` names and the `RT_ALIAS` back-compat names resolve.

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
  relocated 3D-audio API under `Viper.Sound.*`, the new effects chain, the integer
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
- **Namespace re-home:** keep `RT_ALIAS` back-compat for any externally-referenced
  sound3d names; verify no demos/examples break (grep `examples/`).
