# 20 — Game: Act II (Habitat Arcology, Hydroponics Caverns, Storm Terraces) + The Redoubt Hub

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · **2-session chunk**
> (session A: hub + L4; session B: L5 + L6 + act probes). Prereqs: Act I systems proven,
> 03-shadows (E9 point shadows), 05-renderer (E20 monitors, E23 portal clipping),
> 25-meta (economy live for the hub).

## 1. The Redoubt (hub, `world/hub_redoubt.zia`, ~8 min per visit, 3 visits)

Sealed maintenance bay under the arcology. **Design goals**: emotional counterweight (warm
lantern light vs the cold campaign LUTs), meta-systems home, zero combat.
- Spaces: common room (NPCs Mara/Doc/the Kid — dialogue beats per act via 23-dialogue),
  **workbench** (upgrade UI, 25-meta), armory wall (loadout + weapon inspect showcase),
  mission table (level select, medals, par times), radio-log shelf (collected lore playback),
  **shooting range** (30 m tunnel, dummy targets, DPS readout — tuning sandbox shipped to
  players), bunk (save point + act transition).
- Tech: one PVS zone set, 14 lights (2 point-shadowed lanterns E9), monitors playing L4
  security feeds (E20 — foreshadowing), NPC idle node-anims + LookAt player.
- Hub state machine slots into `game.zia` HUB state; visits gate act transitions; fast-travel
  back from level-select confirm.

## 2. L4 — Habitat Arcology (dense interior assault, 18 min)

- **Look**: the clustered-lighting showcase — 64 registered lights across 9 PVS zones
  (portal-clipped, E23), emergency strips vs skylight shafts (E39 shafts through atrium
  glass), LUT `l4_hazard` (desaturated + red accents), live **security monitors** (E20 RTs,
  4 monitor walls showing other zones' cameras — technical flex with gameplay use: watch
  patrols through them).
- **Flow**: tram arrival cinematic → atrium reveal (vista: 4-story interior, shaft light) →
  zone-by-zone push: habitation ring (Husk/Sapper packs in corridors — Scattergun spaces) →
  medbay detour (Doc's supply objective, optional, rewards armor cells) → security wing
  (monitor puzzle: route past a Marauder using camera views; or fight him — ammo tax) →
  hydro-lift junction **WARDEN mini-boss** (17 §1 preview arena) → executive deck (Vanguard
  squads, glass floors over the atrium — vertigo sightlines) → HELIX server antechamber
  (story beat: first direct HELIX dialogue, the offer) → lift to caverns.
- Enemies: full ground roster minus Wraith/Stalker emphasis; Vanguard native here.
- **Showcase**: PVS + clustered budgets (F3 telemetry rows recorded in the probe), monitors,
  interior audio reverb zones (24), the mini-boss. Perf: worst zone ≤ Balanced budget with
  monitors live (monitor RTs at 256², 15 Hz refresh — config).

## 3. L5 — Hydroponics Caverns (vertical hunt, 16 min)

- **Look**: THE beauty level — bioluminescent fungal caverns, 12 point-shadow lights (E9)
  in rotation via zone manager, god-ray shafts from surface cracks (sun shafts E39 where the
  sky pierces; interior "shafts" are fog+spot cones), water pools (Water3D reflections +
  SSR), spore particles (soft, drifting), LUT `l5_biolume` (teal-magenta). Dark zones 0.35
  everywhere — stealth native, flashlight tradeoff active.
- **Flow**: descent shaft (controlled fall teach) → grove chambers (Mites + Stalkers in the
  canopy roots, vertical link ambushes) → the **Wraith introduction**: scripted first-kill
  scare (it takes a scavenger Husk in front of you) → hunt inversion loop: three pump
  stations to restart (interact-holds) while the Wraith patrols by sound — decoy/EMP/loud-
  pin counterplay (16 §Wraith); each pump raises water level opening the next path (kinematic
  water plane steps + nav rebuilds) → flooded gallery swim-wade (slow zone, tension) →
  Wraith showdown arena (pinned-cloak fight among light-shaft pillars) → breach to the
  terraces → checkpoint.
- Enemies: Mites, Stalker, Wraith (star), Husk dressing. Weapons: + Rivet Driver (found in a
  mining cache — its glow-pin light matters here).
- **Showcase**: point shadows, dark-zone stealth, the Wraith mechanic, verticality via links.

## 4. L6 — Storm Terraces (holdout defense, 15 min)

- **Look**: the weather showcase — rolling storm cells (lightning flash = 150 ms directional
  light spikes + thunder delay by distance, rain-to-ash mix particles with wind vector
  driving stretch E41), LUT `l6_storm` (blue-gray, high contrast), wind-audio bed with gust
  ducking (02 E8).
- **Flow**: terrace ascent under drone harass (wind pushes grenades/projectiles — config
  lateral force on ballistics, telegraphed by streaking particles) → beacon platform
  **holdout**: 3-phase defense of Mara's uplink (director set-piece: barricade kit placement
  phase (place 6 of 10 barricade props — destructibles that enemies chew through + nav
  rebuilds E28), wave phases with called lanes (audio bark + lane lightning), between-wave
  30 s repair/replace windows) → storm peak: lightning strikes charge player's EMP for free
  (systemic flourish: stand near rods) → final wave with Marauder pair + Shrike strafes
  (turret pods unlock, 17 §2 foreshadow) → uplink fires, act cinematic (evac window opens).
- Enemies: everything ground + Shrike (uncontrollable flyby attacker). 
- **Showcase**: weather system, destructible/nav loop at scale, director set-piece authoring.

## 5. Act II probes

Hub: economy round-trip (buy → save → reload → applied), range DPS readout math golden,
dialogue-beat gating per act. L4: PVS telemetry probe (camera in zone 1 → zones 5+ culled;
draws drop ≥ 40 % vs clipping-off), monitor RT refresh cadence + zero-alloc loop, mini-boss
phase probe. L5: pump-sequence state machine, water-step nav rebuilds, Wraith decloak rules
under scripted noise plans, point-shadow budget rotation (never > zone budget, no visible
pop in probe telemetry). L6: wave/lane scripts deterministic, barricade placement validity,
wind-force ballistic offsets golden, lightning-audio delay math. All levels: render probes ×2
backends, full-path bot runs, teardown leak checks, VM==native, `-O0/-O2`.

## 6. Verification gate

Act II hands-on run (one sitting) on Metal fullscreen; L5 look pass is the **beauty gate**:
banner screenshot must land the 27 §3 `l5_biolume` mood board (side-by-side review);
L4 monitor wall live at budget; L6 storm at 60 FPS Balanced through the densest wave.
Probes + full build green.
