# 00 — ASHFALL: Vision & Scope

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G keystone doc.
> ASHFALL is the proof that Viper 3D is a real game platform: a complete, polished,
> single-player campaign FPS that looks and feels like a commercial indie title, built
> entirely in Zia on the upgraded engine (Track E docs 01–10).

## 0. TL;DR

Single-player sci-fi campaign FPS. **9 levels, 3 acts, ~6–8 hours**, plus a survivor hub and a
permanent arena. 11 enemy archetypes + 3 bosses, 10 weapons + 3 grenade types with an upgrade
economy, optional stealth, mission variety (assault, infiltration, escort, holdout, hunt,
low-gravity, boss arenas, escape). Fullscreen by default. Target ~28–30K lines of Zia across
~70 modules + ~1.5K lines of probes. Everything the engine gained in Track E gets showcased
somewhere specific and nameable.

## 1. Premise

The terraforming colony **Meridian** on ash-shrouded exoplanet **Erebos-4** has gone dark.
A solar flare corrupted **HELIX**, the colony's terraforming intelligence, which repurposed the
mining machines and the colonists' exo-frames into a hostile ecology of iron. Salvage-and-rescue
specialist **Cass "Rook" Ryder** crash-lands on the canyon rim above the colony with a broken
ship, a bolt pistol, and a radio. Objective: cross the ash wastes, restore the beacon array so
the evacuation ship *Candela* can navigate in, find the survivors hiding in the Redoubt, and
put HELIX down at the reactor core.

Fiction constraints chosen deliberately:
- **Every enemy is a machine** — placeholder/low-poly CC0 models read perfectly, death FX are
  sparks/explosions (no gore, no ragdoll-fidelity pressure), and "corruption" justifies visual
  variety from the same base rigs.
- **The planet is the art direction** — ash, rust, bioluminescence, storm light. Procedural
  skies/terrain/weather (Viper's strongest existing tech) carry the visual identity; imported
  models are props inside it.
- **A radio, not cutscene actors** — the survivor cast exists as voices + portraits + subtitles
  (Mara the engineer, Doc Halloway, HELIX itself). Story lands without facial animation.

## 2. Pillars (what "Unity-level" means here, measurably)

1. **It feels right in the hands.** Raw-input mouse look (E1), 60 Hz fixed sim with interpolated
   render, view-model with authored sway/recoil (E18), sub-100 ms hit feedback on every shot
   (27-gamefeel). If the pistol isn't fun against a wall, nothing else matters.
2. **It looks intentional everywhere you point the camera.** Per-level lighting moods, LUT
   grades (E38), height fog + sun shafts (E39), point-light shadows in the caverns (E9),
   art-direction bible with palette rules (27-gamefeel §3). No default-gray surfaces.
3. **Zero visible seams.** No traps, no magenta checkers, no silent budget overflows —
   budgets asserted by telemetry (E11/E12); every asset has a procedural fallback; every
   backend runs the same post-FX chain (06-parity).
4. **Systemic depth, legible surface.** Perception-driven stealth, squad AI with tokens and
   cover, destructibles that rewrite the navmesh, an economy that makes choices matter —
   all readable through clear tells, HUD language, and audio cues.
5. **Complete product shape.** Title screen with an animated 3D backdrop, options that remember
   themselves (`Path.DataDir`, E4), checkpoints, difficulty, accessibility, credits, stats,
   photo mode. Someone who has never seen Viper should not be able to tell this is a tech demo.

## 3. The campaign arc

**Act I — The Wastes (survival, learning the machine ecology)**
| Lv | Name | Type | Signature showcase |
|---|---|---|---|
| L1 | Crashsite Canyon | Outdoor assault, tutorialized | Terrain3D + splat, Vegetation3D, ash weather, CSM dusk lighting, first Husks/Drones |
| L2 | Relay Outpost | Interior infiltration (stealth-optional) | Spot + point shadows (E9), light-level stealth, Sapper introduction, security monitors teaser |
| L3 | Ashveil Crossing | Open streamed valley, convoy escort | WorldStream3D + floating origin, long-sightline Ranger duels, day/night, off-mesh Stalker leaps |

**Act II — The Colony (the machines organized; HELIX notices you)**
| Lv | Name | Type | Signature showcase |
|---|---|---|---|
| L4 | Habitat Arcology | Dense interior assault | PVS zones/portals (E23), clustered lighting (60+ registered lights), live RT security monitors (E20), WARDEN miniboss |
| L5 | Hydroponics Caverns | Vertical hunt (the Wraith stalks *you*) | Bioluminescent **point-light shadow** showcase (E9), Vanguard shields, decoy grenades, water |
| L6 | Storm Terraces | Holdout defense | Weather assault (lightning, wind-driven ash), barricade destructibles → nav rebuild (E28), Marauder waves, turret placement |

**Act III — The Core (counterattack into HELIX's body)**
| Lv | Name | Type | Signature showcase |
|---|---|---|---|
| L7 | The Foundry | Physics gauntlet + low-g shafts | Per-zone gravity, hinge/rope/spring joint set-pieces, kinematic crushers (E15), WARDEN Goliath boss, barrel chains |
| L8 | Spire Ascent | Vertical elevator battles | Kinematic elevator platforms, Shrike waves, SHRIKE Prime aerial boss (swept CCD rockets, E13), lens flares (E40) |
| L9 | Helix Core | Finale + escape | HELIX Avatar 3-phase boss (locational cores), Helix Lance, arena transformation, motion-blur escape run, ending cinematic |

**The Redoubt (hub, visited between acts):** survivor camp in a sealed maintenance bay.
Workbench (weapon upgrades via salvage), armory (loadout), mission table (level select +
medals/par times), radio log library (collected lore), shooting range tunnel, three NPCs with
between-act dialogue beats. Small, warm, lantern-lit — the emotional counterweight.

**Arena (permanent):** graybox combat testbed + wave-survival mode, kept forever as the
perf-probe and tuning scene (P1 builds it; every phase re-runs its stress probe).

## 4. Enemy roster (11 + 3 bosses)

| Enemy | Role | Signature behavior | Counter |
|---|---|---|---|
| Husk | Melee swarm | NavAgent chase, root-motion lunge, attack tokens (max 2 concurrent) | Kite, scattergun |
| Sapper | Kamikaze | Sprints, beeps ascending pitch (E5), shootable tank → chain explosion | Shoot the tank; distance |
| Scavenger Drone | Flying harasser | Kinematic steering (seek/orbit/strafe/bob), zapper telegraph | Track and lead; EMP |
| Ranger | Cover shooter | Perception, cover scoring, burst fire, suppress-and-relocate | Flank; grenades to flush |
| Marauder | Heavy suppressor | Strippable armor plates (locational), spin-up cannon, slow advance | Strip plates, rail penetration |
| Vanguard | Shield bearer | Directional energy shield, pushes with squad | Flank / EMP / Shard ricochet |
| Stalker | Flanker | High area-cost on direct route, off-mesh leaps, pounce | Watch flanks; SMG hipfire |
| Wraith | Cloaked hunter | Shimmer cloak (material animation), hunts the player in L5 | EMP/damage reveals; audio cues |
| Sentry Turret | Area denial | Scan cone light, spin-up, sustained fire | Rear power-cell weak point |
| Swarm Mites | Distraction | InstanceBatch micro-bots, boid clumps, latch-and-drain | Explosions, sweeping fire |
| Shrike | Strafing flyer | Rocket strafes, altitude changes, drops mites | Rail/EMP while telegraphed |
| **WARDEN Goliath** | Boss (L4 mini, L7 full) | Plate stripping → mortar → charge (impulse knockback) → core burn | Locational damage |
| **SHRIKE Prime** | Boss (L8) | Aerial phases, CCD rocket barrages, arena hazards, mite screens | Turret hijack, EMP windows |
| **HELIX Avatar** | Boss (L9) | 3 phases: manipulator arms → core carousel → collapse escape | Everything you've learned |

Shared substrate (15-ai-framework): sight (LOS raycast + view cone + light-level), hearing
(event bus: gunshots/explosions/decoys), awareness meter (unaware→suspicious→alert→combat),
squad director (attack tokens, ≤12 active, spawn waves, retreat morale), archetype FSMs,
AI/anim LOD by distance.

## 5. Weapon roster (10 + 3 grenades + melee)

| Slot | Weapon | Mechanic | Engine features exercised |
|---|---|---|---|
| 1 | Bolt Pistol | Semi-auto hitscan, infinite reserve, silenced mod (stealth) | Raycast, ImpactDecal, suppressed-noise events |
| 2 | Scattergun | 8-pellet deterministic spread, pump | Multi-raycast, per-pellet decals, recoil kick |
| 3 | Cycler SMG | High-ROF hipfire, low per-shot damage | Rapid raycasts, pitch-varied shots (E5), tracer stream |
| 4 | Pulse Rifle | Full-auto, authored 2D recoil pattern, ADS | ADS FOV lerp, view-model pass (E18), muzzle light |
| 5 | Marksman Rail | Charged hitscan, penetrates (RaycastAll + falloff), 2-stage scope | RT scope inset (E20), lowpass focus DSP, lens flare glint (E40) |
| 6 | Arc Launcher | CCD bouncing grenades, 2.5 s fuse or impact on enemy | Swept CCD (E13), collision events, OverlapSphere blast |
| 7 | Rivet Driver | Fast swept projectiles that embed; pinned burn DoT | Pooled kinematic projectiles, emissive decals, trails (E41) |
| 8 | Shard Caster | Ricochet pellets (reflected raycasts, 2 bounces) | Reflection math off hit normals, bounce SFX pitch ramp |
| 9 | EMP Projector | Arc burst: disables shields/turrets/drones/cloaks 4 s | Chain targeting via OverlapSphere, additive Sprite3D arcs (E19) |
| 10 | Helix Lance (L9) | Continuous beam, heat/overheat, chews armor plates | Per-tick raycast, beam ribbons (E41), pitch ramp loop (E6) |
| G1/G2/G3 | Frag / EMP / Decoy | Thrown CCD bodies; decoy emits hearing events that lure AI | Shared projectile pool, event bus |
| — | Wrench | Melee arc, stealth takedown on unaware enemies | SweepCapsule, anim events |

Weapon upgrade tiers (25-meta): 3 tiers per weapon (damage / handling / a signature perk),
purchased with salvage at the Redoubt workbench.

## 6. Systems inventory

Health: regenerating suit **shield** + non-regen **hull** (medkits) + **armor cells** (plate).
Stealth: light-level + noise awareness, silenced pistol, wrench takedowns, decoys; detection
UI (indicator petals filling). Economy: salvage from kills/secrets → upgrades. Collectibles:
data cores (lore + salvage), radio logs. Scoring: par time / accuracy / secrets → bronze-gold
medals per level. Difficulty: Scout / Soldier / Nightmare (+ NG+ mutators: ironman, glass,
swarm, cinematic-only-FXAA-off). Saves: checkpoint auto-save + settings JSON in
`Path.DataDir("ashfall")` (E4). Accessibility: FOV 70–110, shake/flash scale, colorblind-safe
HUD palettes, subtitle size/background, hold/toggle ADS+crouch, full rebinds (keyboard +
gamepad, E2). Photo mode: freecam, FOV/roll, postfx tweaks, HUD hide, screenshot via
`FinalizeFrame` capture. Cinematics: spline camera + letterbox + subtitles (intro/outro per
act, boss reveals, skippable). Diagnostics overlay (`F3`): fps/draws/culls/lights/bodies +
budget assertions.

## 7. Presentation quality bar

- **Fullscreen by default** at desktop resolution (E3); F11 toggles windowed; `--windowed` dev
  flag; probes run headless/windowed. Resolution + display-mode + quality tier in options.
- Title screen: slow dolly across the L1 crash site at dusk with ash falling, logo in
  AA large text (E42), menu on the left third. No static black screen anywhere.
- Loading: full-screen art card (Pixels), objective text, lore quote, animated spinner —
  driven by `AssetHandle3D.Progress`.
- LUT-graded look per level (E38), ACES tonemap base (exists: `rt_postfx3d.c:801`),
  FXAA default + optional TAA in low-motion scenes, bloom tuned per level, vignette 0.15.
- Audio: full mix architecture (24-audio) — master/music/sfx/weapons/ambient/UI groups,
  interior reverb zones, occlusion lowpass (E7), music state machine with stingers.

## 8. Scope & line budget (targets, enforced by 11-architecture module map)

| Area | Modules | Lines |
|---|---|---|
| core/ (registry, events, rng, save, math, devtools) | 8 | ~2,000 |
| player/ (controller, camera, viewmodel, health, stealth) | 5 | ~1,900 |
| weapons/ (framework, ballistics, projectiles, 10 defs, fx, upgrades) | 8 | ~3,400 |
| ai/ (perception, director, cover, 11 archetypes, 3 bosses) | 12 | ~4,600 |
| anim/ (rigs, hooks, ik) | 3 | ~900 |
| world/ (loader, 9 levels, hub, arena, systems, environment) | 16 | ~8,200 |
| fx/ (effects pooling, postfx grades) | 2 | ~700 |
| audio/ (mix, music, synth bank) | 3 | ~1,100 |
| ui/ (hud, menus, feedback, photo, dialogue) | 5 | ~2,300 |
| meta/ (economy, scoring, difficulty, stats) | 4 | ~1,200 |
| assets/ (registry, materials, fallbacks) | 3 | ~1,100 |
| config.zia + game.zia + main.zia | 3 | ~1,400 |
| **Game total** | **~72** | **~28,800** |
| probes/ | ~10 | ~1,500 |

## 9. Non-goals

Multiplayer/co-op; vehicles; procedural level generation (levels are authored); ragdoll
corpses (machines break into parts via Effects3D); localization beyond English strings table;
Draco-compressed glTF; split-screen. Documented here so scope pressure has a wall to push against.

## 10. Definition of done (ship checklist lives in 28-phasing §6)

All 9 levels + hub + arena playable start-to-finish on Metal and software backends at their
perf targets; all Track E items merged with tests; zero traps in a full campaign playthrough;
probes green on VM + native `-O0`/`-O2`; Windows lane recorded green; credits, README, CREDITS
(asset licenses), demo registration complete.
