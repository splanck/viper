# 02 — Engine: Voice Pitch, 3D Source Pitch, Occlusion Filter, Ducking

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track E · 1 session.
> Eliminates constraint #16: no pitch/playback-rate anywhere in `Viper.Sound`
> (`PlayEx` is volume+pan only — runtime.def:3471 `"i64(obj,i64,i64)"`), no per-voice
> occlusion filtering. Adds the mix polish an FPS needs: shot variation, beam heat ramps,
> muffled-through-walls audio, combat ducking.

## 0. TL;DR

Add a per-voice **resample ratio (pitch)** to the software mixer, expose it on `Voice`,
`Sound.PlayEx2`, and `SoundSource3D`; add a **per-voice one-pole lowpass** driven by a 0–1
occlusion amount (game supplies LOS raycast results; runtime smooths); add **group ducking**
(weapons duck ambient/music) as a small mixer-side envelope. Doppler machinery already
resamples per-voice for 3D sources — E5 generalizes that path rather than inventing a new one.

## 1. Current state (verified anchors)

- `Viper.Sound.Sound.PlayEx` = `(volume, pan)` — runtime.def:3471 (RT_FUNC
  `"i64(obj<Viper.Sound.Sound>,i64,i64)"`), method surface runtime.def:9606; `PlayExGroup`
  adds a group id (:3460). `Voice` = Stop/SetVolume/SetPan/IsPlaying (:9616-9622 region).
  **No pitch anywhere.**
- 3D audio: `SoundSource3D` has Doppler (`get_DopplerFactor` — runtime.def ~14246) and
  distance attenuation; implementation `src/runtime/audio/rt_sound3d.c:454-555` computes
  per-voice playback-rate adjustments for Doppler — i.e. **variable-rate mixing already
  exists internally**; it is just not exposed as user pitch.
- Mix groups with per-group DSP exist: `Audio.RegisterGroup`, `SetGroupVolume`,
  `GroupAddLowpass/Highpass/Peaking/Delay/Reverb` (runtime.def:9591-9595 region) — the biquad
  filter code to reuse for per-voice lowpass already ships.
- Codecs in-tree (WAV/OGG/MP3, `rt_vorbis.c`, `rt_mp3.c`); `Music.CrossfadeTo` exists.
- `Synth.Sweep/Tone/Noise` exists (procedural fallback bank relies on it).

## 2. New API surface

```text
Viper.Sound.Voice.set_Pitch(f64)                    void(i64,f64)   — 0.25..4.0, clamps
Viper.Sound.Voice.get_Pitch                         f64(i64)
Viper.Sound.Voice.SetLowpass(f64)                   void(i64,f64)   — cutoff Hz; <=0 disables
Viper.Sound.Voice.SetOcclusion(f64)                 void(i64,f64)   — 0=open..1=fully occluded (maps to cutoff+gain curve, 80ms smoothed)
Viper.Sound.Sound.PlayEx2(i64,i64,f64)              i64(obj,i64,i64,f64) — volume, pan, pitch
Viper.Graphics3D.SoundSource3D.set_Pitch(f64)       void(obj,f64)   — multiplies with Doppler
Viper.Graphics3D.SoundSource3D.get_Pitch            f64(obj)
Viper.Graphics3D.SoundSource3D.set_Occlusion(f64)   void(obj,f64)
Viper.Sound.Audio.SetGroupDucking(str,str,f64,f64,f64) void(str,str,f64,f64,f64)
    — (triggerGroup, targetGroup, amount 0..1, attackSec, releaseSec): while any voice in
      triggerGroup is audible, targetGroup gain is scaled toward (1-amount).
```
Notes: `Voice` handles are i64 ids (existing convention). Pitch semantics: playback-rate
multiplier (1.0 = normal); interacts multiplicatively with Doppler on 3D sources. All setters
are safe on finished voices (no-op) — matching existing `Voice.SetVolume` behavior.

## 3. Implementation

1. **Mixer resampler (E5 core)** — `src/runtime/audio/` mixer voice struct gains
   `pitch` (f64, default 1.0) + fractional read cursor; render loop switches the per-voice
   sample fetch to linear interpolation over the fractional cursor (the Doppler path in
   `rt_sound3d.c` already advances a rate-scaled cursor — unify: 3D voices compute
   `effective_rate = pitch * doppler`). Keep integer fast path when `pitch == 1.0` exactly.
   Looping voices wrap the fractional cursor; streaming (Music) is **out of scope** for pitch
   (documented — Music keeps rate 1.0).
2. **Per-voice lowpass (E7)** — reuse the group biquad code as a per-voice one-pole/biquad slot;
   `SetOcclusion(x)` maps x→cutoff on a perceptual curve (22 kHz→800 Hz) + −6 dB·x gain, with an
   80 ms exponential smoother evaluated in the mixer (prevents zipper noise when the game's
   LOS raycast flips). `SetLowpass` sets cutoff directly for scoped/underwater effects.
3. **3D passthrough (E6)** — `rt_soundsource3d` stores user pitch/occlusion; `SyncBindings`/
   update path multiplies into the voice each frame (occlusion smoothing lives voice-side).
4. **Ducking (E8)** — mixer tracks per-group audibility (any voice gain > −40 dB); target-group
   scale follows attack/release envelope. One rule per (trigger,target) pair; re-register
   replaces.
5. **Surface + docs** — runtime.def entries (§2), completeness check, `docs/viperlib/audio.md`
   + `rendering3d.md` 3D-audio section, ADR (runtime ABI additions).

## 4. Files

`src/runtime/audio/rt_audio.c` (mixer core; exact file per current split — locate voice render
loop), `src/runtime/audio/rt_sound3d.c`, `rt_soundsource3d.*`, `src/il/runtime/runtime.def`,
`docs/viperlib/audio.md`, tests: `src/tests/unit/` new `test_rt_audio_pitch.cpp` +
extend `RTSound3DTests`.

## 5. Tests

1. Pitch 2.0 on a 440 Hz synth tone → rendered buffer dominant period halves (autocorrelation
   check on the mixed output; deterministic synth input).
2. Pitch 1.0 bit-identical to pre-change golden render (fast-path regression).
3. Occlusion 0→1 step: output RMS above 2 kHz falls ≥ 18 dB within 120 ms, no discontinuity
   > 0.05 sample-to-sample (zipper guard).
4. 3D source: pitch × Doppler compose (moving source at pitch 1.5 vs static at 1.5 — ratio
   matches Doppler-only golden).
5. Ducking: weapons-group burst ducks music group by `amount` within attack window; releases.
6. Finished-voice setters are no-ops (no crash, return cleanly).

## 6. Verification gate

Unit tests above fail-before/pass-after → `ctest -R 'audio|sound' --output-on-failure` →
runtime completeness + surface audit → full no-skip build → headphone spot-check on macOS
(pitch sweep clean, no aliasing artifacts worse than linear-interp expectation — document
linear interpolation as the quality tier; cubic upgrade noted as future work). Game consumers:
13-weapons (per-shot ±8 % pitch), 16-enemies (Sapper beep ramp), 24-audio (occlusion driving,
ducking rules, Helix Lance heat loop).
