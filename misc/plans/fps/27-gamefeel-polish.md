# 27 — Game: Game Feel & Polish — Feedback Stack, Art Direction Bible, Motion Standards, Performance Polish

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions (P17 owns
> the concentrated pass; the standards here bind every earlier doc from day one).
> This is the "Unity-level polish" contract made checkable: named standards, numeric caps,
> scored checklists — not vibes.

## 1. Hit-feedback stack (every damage event composes from this menu, with hard caps)

Layers (per event, config-driven): hitmarker tick (90 ms) + kill tier; target hit-flash
(material emissive pulse 60 ms — never albedo swap); impact FX by surface (13 §4); damage
numbers (toggle); micro-hitstop **only** on melee/takedown/boss-phase hits (40 ms sim-local
slowmo ×0.3 — implemented as dt scale on the target's anim/FX, NOT the fixed clock —
determinism preserved); camera shake via the single shake manager (12 §2) with **global caps**:
amplitude ≤ 0.8°, stacked events sum-clamped, user scale 0–150 %, photosensitivity mode halves
flashes; controller rumble tiers (E2 Vibrate: fire 0.1, hit 0.25, explosion 0.5, 120 ms caps);
audio confirm tiers (24). **Anti-fatigue rules**: no full-screen flashes > 80 ms, no shake
during ADS > 0.3°, vignette pulses ≤ 2/s.

## 2. Feel checklists (scored at gates; a row failing twice becomes a tuning task)

- **Weapons (per weapon)**: distinct silhouette + sound at 5 m and 40 m; recoil readable and
  masterable (pattern replay overlay in range); reload cancel windows honored; dry-fire and
  low-ammo tells; first-shot feel (P3 sign-off question: "is the pistol fun against a wall?").
- **Movement**: slide chains feel earned; landing never floaty (dip + thud within 50 ms);
  coyote saves feel fair not mushy; strafe air-control consistent.
- **Enemies (per archetype)**: nameable from silhouette at 30 m; every attack telegraphed
  (tell → strike gap per 16 tables); death reads within 200 ms (no ambiguity about kills).
- **UI**: every interactive element responds within 1 frame (hover/press states); nothing
  pops (all per §4); text legible at 80 % HUD scale on 1366×768.
- **Audio**: gunfight at 12 sources stays legible (priority audible: player weapon > tells >
  deaths > foley).

## 3. Art direction bible (binds 19–21 level looks + 26 materials)

- **Palette law**: each level = 1 dominant hue family + 1 accent (L1 amber/teal-sky,
  L2 teal/warm-security, L3 bone-noon/dust, L4 desat/red-hazard, L5 teal-magenta biolume,
  L6 blue-gray/lightning-white, L7 sodium-orange/black, L8 cold-dawn→gold, L9 clinical
  cyan/warm-damage). Enemy eyes/status = always warm-hostile (red-amber) — read against every
  level palette (colorblind-checked variants).
- **Value discipline**: player path brighter than surroundings (wayfinding by light, never
  arrows in-world); combat spaces value-separate cover silhouettes from walls (contrast
  ≥ documented ratio in lookdev checks).
- **Silhouette rules**: skyline landmarks per outdoor level (crash column, arcology, spire)
  visible from any main-path point; enemy silhouettes distinct at black-fill test (16 gate).
- **LUT workflow** (E38): identity strip → grade in any editor over lookdev screenshot →
  strip PNG committed `assets_gen/luts/` — per-level + damage/photo variants.
- **Lookdev gallery**: `misc/plans/fps/lookdev/` — one target board per level (composed at
  P10/P12/P14 gates from in-engine screenshots + notes); the L5 board is the flagship
  (20 §6 beauty gate).
- **Lighting moods**: key ratios per level family (outdoor sun:ambient ≈ 8:1 dusk, 4:1 noon;
  interior pools 12:1 against dark runs; caverns lit-by-emissives ≤ 20 lux equivalent floor).

## 4. UI/camera motion standards (every animated value in ui/ + camera feel layers)

Eases: UI in `cubic-out 140 ms`, out `cubic-in 100 ms`, emphasis `spring ζ0.85 220 ms`;
camera feel springs per 12 §2; nothing linear except progress bars; stagger list items 24 ms;
toasts slide+fade never bounce. One shared `mathutil.ease*/spring` set — probes assert no
ad-hoc lerps in ui/ (grep-probe for raw `+ (target-cur)*k` patterns — style gate).

## 5. First-run experience & tutorialization

Cold boot → studio/engine card (2 s, skippable) → title (live backdrop) → New Campaign →
difficulty (with honest descriptions) → intro cinematic (45 s, skippable) → L1 contextual
teaches (12 §3 prompts once-each, suppressed after use; all re-viewable in pause "controls"
card). No tutorial level — L1 IS it (19 §2 beat design). Menu-to-gameplay ≤ 3 inputs on
Continue path.

## 6. Performance polish (targets asserted in 28 §4)

Loading: cold level ≤ 6 s (M-class, packed VPA), hub ≤ 2 s; hitch budget: no tick > 8 ms over
budget during streaming/spawns (frame-time histogram probe rows); memory: entity pools flat
(11 §7), texture residency within tier budgets; startup-to-title ≤ 4 s. Quality tiers
(finalized here): Performance (SW-floor set: FXAA, no GPU-only extras, particles ×0.25,
shadows 1024/4-slot, monitors 128²@8 Hz, skinned cap 8), Balanced (default: full postfx-lite —
bloom/SSAO/shafts, particles ×1, shadows 2048/8, monitors 256²@15 Hz), Cinematic (+ SSR/DOF-
in-photo/motion-blur-in-sequences, particles ×1.5, shadows 2048/8+point-cubes, monitors
256²@30 Hz). Auto-pick on first run: backend probe + 3 s micro-bench in the title backdrop.

## 7. The concentrated P17 pass (session plan)

1. Feel-checklist full sweep (§2) across weapons/movement/enemies — fix list triaged live.
2. Per-level LUT + lighting-mood pass against boards (§3) with photo-mode captures.
3. UI motion audit (§4 grep-probe + hand pass), HUD scale/colorblind matrix screenshots.
4. Feedback-stack cap audit (§1) — worst-case brawl recording reviewed frame-by-frame.
5. First-run walkthrough ×3 personas (KB/M, pad, accessibility-heavy settings).
6. Perf polish sweep (§6 histograms per level; fix top-3 hitch sources).

## 8. Verification gate

Checklists scored ≥ pass on all rows (failing rows have filed tuning tasks + rerun);
lookdev boards approved; motion grep-probe clean; perf histograms within budgets on Metal +
software floor; the "someone who has never seen Viper" test (00-vision §2.5) run with one
outside-context player note-taken. Full build green.
