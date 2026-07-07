# 23 — Game: UI — HUD, Menus, Options, Dialogue, Photo Mode, Loading

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · **2-session chunk**
> (session A: HUD + feedback + dialogue; session B: menus/options/photo/loading).
> Prereqs: 07 (E42 AA text + 9-slice), 12-core-loop (actions), 25-meta (settings model).
> Design language: **diegetic-flavored suit UI** — thin cyan-amber lines, hex accents, chunky
> readable numerals (the AA-scaled bitmap font IS the aesthetic), everything animated with
> the 27 §4 motion standards (120–180 ms eases, no pops).

## 1. HUD (`ui/hud.zia`) — all Canvas3D overlay, drawn post-EndScene

Layout (1080p reference, proportional scaling by min(w,h)):
- **Bottom-left vitals**: shield bar (cyan, segments) over hull bar (amber) + armor-cell pips;
  damage flash + regen shimmer states; low-hull heartbeat vignette (feedback §3).
- **Bottom-right weapon block**: weapon name + mag "24 | 96" (AA text 2.5×, precomputed
  strings on change), fire-mode glyph, grenade type + count, heat ring (Lance), charge ring
  (Rail) around the reticle instead when ADS.
- **Center reticle**: per-weapon dynamic crosshair (spread-driven gap, 13 §1), hitmarkers
  (X white / amber head / red kill with 90 ms tick), ADS swaps to weapon-specific sight
  (Rail scope mask + range ticks via clip-rect ring).
- **Top-center objective strip**: current objective text + marker distance; Tab expands the
  log panel (9-slice E42).
- **Top-right compass ribbon**: bearings + objective diamonds + alert-state pips (zone CALM/
  ALERT/LOCKDOWN icon), no minimap (compass keeps eyes center — deliberate).
- **Detection petals** (stealth): arc segments around reticle filling toward the loudest
  filling AI's bearing (15 §1 honest-senses rule).
- **Contextual**: interact prompts with hold-progress ring, pickup toasts (queue ≤ 3),
  subtitle band (dialogue §4), boss health banner (17), wave counter (L6/arena),
  salvage counter blip on collect.
- Scaling/accessibility: HUD scale 80–130 %, colorblind palettes (three preset swaps —
  config tables, dataviz-safe hues), flash-intensity scale, damage numbers toggle.

## 2. Feedback layer (`ui/feedback.zia`)

Directional damage petals (8-way, intensity by amount), low-hull vignette + desat ramp
(never below 85 % saturation — readability), pickup/unlock toasts, medal/secret chimes with
banner slide, kill-confirm audio tiers (13 §4), objective-complete stamp animation,
death screen (cause line + tip rotation + fast reload ≤ 2.5 s total).

## 3. Menus (`ui/menus.zia`) — hybrid: Game.UI widgets for lists/sliders + custom Canvas3D chrome

- **Title**: live 3D backdrop (L1 crash vista camera dolly loop, time-of-day cycling slowly;
  the *game* renders behind the menu — no static art), logo AA text 6× with subtle ash
  particles, menu: Continue / New Campaign (difficulty select + intro) / Mission Select
  (post-unlock: level cards + medals + par times) / Arena / Options / Credits / Quit.
- **Pause**: blur-free dim overlay (scene keeps rendering, sim frozen — photo-mode shares
  this freeze), resume/objectives/options/restart-checkpoint/quit-to-title (confirm).
- **Options** (all persisted instantly, bowling crash-safe pattern):
  - *Video*: display mode (fullscreen default/windowed/borderless where OS applies),
    resolution list (fullscreen), quality tier (Performance/Balanced/Cinematic + Custom
    exposing: shadow quality/budget, postfx set toggles, particle scale, vegetation density,
    monitor RT rate, FOV 70–110), FPS cap (off/60/120), diagnostics overlay toggle.
  - *Audio*: master/music/sfx/weapons/ambient/UI sliders (mix groups 1:1), subtitle
    size/background, mute-on-focus-loss.
  - *Controls*: sensitivity (hip/ADS separate), invert-Y, hold/toggle ADS+crouch, **full
    rebind table** (keyboard + mouse + pad via E2; conflict detection; reset row; the
    actions table from 12 §3 is the single source), pad deadzone/vibration sliders.
  - *Accessibility*: HUD scale, colorblind palette, screen-shake/flash scales, damage
    numbers, subtitle options duplicated here, hold-time multiplier.
- **Credits**: scroll over lookdev stills + music; CC0 asset attributions (26 CREDITS
  mirrored).

## 4. Dialogue & radio (`ui/dialogue.zia`)

Radio-bark system: queued lines (id → speaker, portrait glyph, duration, priority; interrupt
rules — boss/HELIX lines preempt chatter), subtitle band with speaker color + name, audio via
24 (voice = synth-formant placeholder or text-blip style — documented choice: **text-blips**
à la retro consoles, dodging TTS/VO entirely; per-speaker blip timbre), HELIX gets a
distinct glitch-blip + screen-edge chromatic flicker on speak (postfx-lite: vignette pulse).
Hub conversations: proximity-triggered beat lists per act (no dialogue trees — linear beats
with 2-choice flavor prompts at three story moments, choice logged to stats).

## 5. Photo mode (`ui/photo.zia`)

PLAYING sub-state (sim frozen): FreeFlyController camera (engine has it) with collision-free
roam ≤ 25 m from player, roll/FOV sliders, time-of-day scrub (outdoor levels), LUT select +
exposure offset, fog density, HUD hide, player-visible toggle (view-model off, body proxy
on), grid/thirds overlay, **capture** = `FinalizeFrame` + `CopyTo` (E21) reused buffer →
PNG via Pixels save to `Path.DataDir("ashfall")/photos/` with timestamp names; toast with
path. Probes: state freeze purity (zero sim ticks while open), capture determinism.

## 6. Loading screens

Full-screen level art card (lookdev stills as Pixels), objective preamble text, lore quote
rotation, progress bar from `AssetHandle3D.Progress` aggregation + staged-spawn fractions
(19 §1), min-display 1.2 s (no flash-loads), input-swallow until ready + "press any key"
on complete (lets players read).

## 7. Probes

HUD string precompute (zero per-frame formatting — allocation probe), reticle spread math
mirrors ballistics exactly (shared config path), detection petal honesty (only real sense
rows move it), rebind round-trip (rebind → save → reload → action fires on new key; conflict
rejected), options instant-persist (kill process simulation → settings survive), title
backdrop determinism (fixed-seed dolly), photo capture golden (fixed scene → stable PNG
hash), subtitle queue priority rules, loading min-time + progress monotonicity. VM==native.

## 8. Verification gate

Session A: HUD complete in arena (all states demoable via dev keys) + feel per 27 §4 motion
standards. Session B: full menu flow keyboard-and-pad navigable end-to-end (no mouse
required — pad-nav probe), options matrix spot-checked live, photo mode ships a beauty shot
of the arena. Probes + full build green.
