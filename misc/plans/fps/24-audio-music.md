# 24 — Game: Audio & Music — Mix, Spatial Rules, Occlusion, Music States, Synth Bank

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 02-audio (E5/E6 pitch, E7 occlusion, E8 ducking), 11 (event bus). Consumes CC0
> sound packs (26) with a **complete procedural synth fallback** so the game is fully voiced
> with zero downloads (ridgebound lineage: every cue synthesizable).

## 1. Mix architecture (`audio/mix.zia`)

Groups (Audio.RegisterGroup): `master ▸ music, sfx ▸ weapons, world, enemies, ambient, ui`.
Options sliders map 1:1 (23 §3). DSP per group: `ambient` gets storm lowpass automation
(L6), `world` gets **interior reverb** per zone class (manifest `reverb: "corridor|hall|
cavern|exterior"` → GroupAddReverb presets swapped on zone change with 0.5 s crossfade),
`weapons` dry. Ducking (E8): weapons→music −35 % (attack 30 ms release 400 ms),
dialogue→sfx −20 %, boss-stinger→all-sfx −50 % momentary. Master limiter headroom convention:
mix levels authored so 12 simultaneous sources stay < 0 dBFS (probe: max-sample scan on a
worst-case burst render).

## 2. Spatialization rules

`SoundListener3D.BindCamera` once (world init); every world-space cue via `Sound3D.PlayAt`/
`PlayAttached` (enemy loops attached to entities); `SpatialAudio3D.SyncBindings` each fixed
tick post-physics (landmine rule). Attenuation bands (config): weapon shots ref 8 m max 120 m;
foley ref 2 m max 25 m; ambient emitters ref 4 m max 40 m. Doppler default ×1 (drones/Shrike
×1.4 flavor). **Occlusion** (E7): per active 3D voice ≤ 12 tracked, LOS ray to listener every
4th tick (staggered) → `set_Occlusion` 0/0.55/0.85 (open/soft cover/wall — two-ray test:
center + offset) — smoothed engine-side; distance muffle adds lowpass beyond 60 m.

## 3. Sound design sheets (implemented as data tables + synth recipes)

- **Weapons** (per 13/14 roster): layered shot = body (synth: tuned noise burst + pitched
  sweep) + mechanism (click/clack) + tail (space-dependent: interior tail routes through
  reverb group); pitch jitter ±8 % (E5); distant variant (> 40 m plays crack-only) — the
  classic near/far split; reload stage foley per weapon; dry-fire click; Lance heat loop
  (E6 pitch = f(heat)).
- **Enemies**: per-archetype voice = servo bed loop (attached, occludable) + tell vocalises
  (Sapper beep ramp E5, Wraith whisper-static, Stalker skitter, spin-ups) + death burst;
  all synthesized from a shared machine-timbre kit (`synth_bank.zia` §5) so packs are
  optional polish.
- **World**: footsteps (surface × stance matrix, 12 §1), ambience beds per level (wind band,
  interior hum, cavern drip — looping synth textures + one-shot spot sounds on a Poisson
  timer), weather (rain graupel, thunder with distance delay 22 §6, wind gusts driving E8
  duck on ambience), physics foley (impact intensity → pitched clank tables, barrel rolls),
  UI (blips, confirms, medal chime — one family, pitch-laddered).

## 4. Music (`audio/music.zia`)

State machine driven by event bus + director: `EXPLORE → SUSPENSE (awareness > 35 anywhere)
→ COMBAT (alert) → RESOLVE (post-combat 8 s) → EXPLORE`, plus authored overrides (holdout
waves, boss suites, hub theme, title, credits). Transitions via `Music.CrossfadeTo` (2–4 s;
stingers on COMBAT entry + boss phase hits as one-shot sfx layered over the bed). Tracks:
**generative-first** — `MusicGen`/`Synth` pad-arp beds per level family (wastes/colony/core
motifs documented as note tables — dark fourths motif for HELIX, rising fifths for Candela),
with optional OGG drop-ins per state (26 packs or user-supplied) selected when present
(`Assets.Exists` probe at load). Boss suites: 3-movement structures phase-synced (17).

## 5. Synth fallback bank (`audio/synth_bank.zia`)

Every cue id in the game resolves through one registry: `cue(id) → SoundBank clip if loaded,
else synth recipe render` (rendered once at load into cached clips — not per-play). Recipes =
parameterized Synth calls (documented per §3 sheets). Headless/audio-less hosts: `Audio.Init`
fail → `ready=false` early-outs (landmine rule), probes still assert cue *requests* via the
event log (audio logic testable without a device).

## 6. Probes

Cue coverage: every `AUDIO_*` id in config has a recipe (registry completeness scan);
occlusion staggering ≤ 12 rays/tick; state machine transition table legality over a scripted
combat arc; ducking envelopes within spec on rendered buffers; limiter headroom scan;
near/far weapon split distances; reverb zone swap continuity (no click: sample-delta bound at
swap); determinism of generative beds (seeded) VM==native.

## 7. Verification gate

Headless probes green → monitored listen pass (headphones, Metal): walk L2 graybox —
interior/exterior transition, occluded turret behind wall reads muffled, combat music enters
on alert and resolves; weapon layers distinct per 27 §2 audio rows. Full build green.
