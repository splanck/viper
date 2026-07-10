# XENOSCAPE Commercial Release Upgrade

Status: complete; scoped release validation passed locally  
Owner: Viper examples/games/xenoscape  
Normative game sources: `examples/games/xenoscape/`  
Baseline project version: `0.1.0`  
Current release version: `1.0.0`

## 1. Summary and objective

Upgrade XENOSCAPE from a broad engine showcase into a cohesive, commercial-
quality, zero-dependency, cross-platform action Metroidvania. The completed game
must preserve every existing feature while making each advertised mechanic
functional, discoverable, persistent, and testable. The ten-area campaign,
enemies, bosses, abilities, lore, achievements, procedural fallbacks, and
release packaging all remain in scope.

The production strategy is deliberately depth-first:

1. Repair gameplay contracts and establish deterministic probes.
2. Build one gold-standard slice from the title screen through the Spore Mother.
3. Roll the proven systems and quality bar across the remaining campaign.
4. Finish accessibility, meta modes, packaging, and cross-platform release gates.

## 2. Product pillars

1. **Expressive traversal.** Every unlocked movement ability changes navigation,
   combat, and the way previously visited rooms can be read.
2. **Readable alien combat.** Enemy intent, vulnerability, damage, status, and
   rewards are visually and mechanically unambiguous.
3. **A world worth revisiting.** Regions contain memorable landmarks, shortcuts,
   secrets, evolving objectives, and reasons to return after new abilities.

The visual identity is atmospheric science fiction with luminous alien color,
clear silhouettes, restrained pixel texture, and high-contrast interface text.
The title-screen atmosphere is the reference tone. Cheerful prototype colors
may remain as accents but must not override the unified art direction.

## 3. Scope

### 3.1 In scope

- All 28 approved upgrade recommendations.
- Game-local Zia modules, first-party assets, manifests, probes, docs, and CTests.
- Three save slots, settings persistence, save migration, and corrupt-save
  recovery.
- Keyboard and gamepad parity on macOS, Windows, and Linux.
- Procedural visual/audio fallbacks for headless probes and missing optional
  first-party assets.
- Deterministic VM/native behavior for all pure gameplay and persistence logic.

### 3.2 Out of scope unless separately specified by ADR

- New IL syntax, opcodes, verifier rules, or changes to the IL reference.
- New runtime C ABI functions or cross-layer dependencies.
- External libraries, downloaded packages, telemetry, online accounts, or
  network leaderboards.
- Mod support or a public level-editor format in the first release.

If a required feature cannot be implemented with the existing runtime surface,
an ADR must be accepted before modifying `src/il/runtime/runtime.def`, the C ABI,
or normative runtime documentation.

## 4. Feature-toggle policy

Player-facing core mechanics do not ship behind toggles. A mechanic lands only
when its positive, negative, edge, persistence, and render-safe probes pass.

Temporary migration toggles are allowed in `xenoscape.runtime.json` only for
campaign graph and asset rollout. They must use these keys and defaults:

| Key | Type | Migration default | Release default |
|---|---:|---:|---:|
| `campaign.worldGraphEnabled` | Boolean | `false` | `true` |
| `presentation.authoredArtEnabled` | Boolean | `false` | `true` |
| `presentation.adaptiveMusicEnabled` | Boolean | `false` | `true` |

The procedural art and audio paths remain permanent fallbacks, not deleted
legacy code. The final package smoke probe must validate both authored and
fallback paths.

## 5. Exact gameplay contracts

### 5.1 Ability ownership and unlock order

`AbilityManager` is the single source of truth for the acquired bitmask.
`Player` receives a synchronized mask before processing input but does not
persist a second authoritative campaign value.

| Ability | Flag | Acquisition | Required behavior |
|---|---:|---|---|
| Wall Jump | 1 | starting suit module | wall slide and angled jump-away |
| Double Jump | 2 | Spore Mother | one refunded aerial jump per landing/wall jump |
| Dash | 4 | Crystal Wyrm | 160 ms burst, 480 ms cooldown, damage immunity |
| Charge Shot | 8 | Underground Lake shrine | tiers at 320/640 ms; 2/3 damage; full tier breaks walls |
| Ground Pound | 16 | Magma Core | downward slam, breakable floors, radial enemy stagger |
| Grapple | 32 | Overgrowth shrine | select valid anchor, pull/swing, release with retained momentum |

Locked abilities must not activate through keyboard, gamepad, load, checkpoint,
or animation state. Re-granting an owned ability is idempotent.

### 5.2 Traversal tuning

- Simulation target: 60 Hz with millisecond delta clamped by `DT_MAX`.
- Jump buffer: 128 ms; coyote time: 96 ms.
- Corner correction: at most 6 pixels horizontally during upward collision.
- Wall-stick grace: 80 ms before a wall slide begins.
- Grapple acquisition radius: 384 pixels; maximum rope length: 448 pixels.
- Grapple release preserves tangential velocity, clamped to 1.5 times dash speed.
- Camera horizontal look-ahead: up to 160 pixels at full run speed.
- Camera vertical intent: up to 96 pixels while holding up/down for 250 ms.

### 5.3 Combat contract

- Normal shot damage: 1.
- Partial charge damage: 2.
- Full charge damage: 3 and destructible power 2.
- Stomp damage: 10, except explicitly immune environmental hazards.
- Enemy-specific score, contact damage, projectile immunity, death effect, and
  status effect are queried before an enemy pool slot is released.
- Mirror Slime reflects player projectiles and cannot take projectile damage.
- Piston Trap ignores all player damage.
- Ice Wraith contact applies `ICE_FREEZE_DURATION_MS` after damage resolution.
- Boss phase changes clear nearby hostile projectiles and provide at least
  500 ms of readable transition time.
- Hit stop defaults: 35 ms normal kill, 60 ms charged kill, 100 ms boss phase.

### 5.4 Environmental interaction contract

| Mechanic | Exact behavior |
|---|---|
| Ice | ground deceleration is 25% of normal; no visual-only implementation |
| Quicksand | horizontal speed is 55%; jump impulse is 80%; escaping is always possible |
| Crumble | shakes after 350 ms contact, disappears for 2400 ms, then regenerates |
| Conveyor | adds signed 200 centipixel velocity per baseline frame, clamped safely |
| Steam vent | launches once per contact entry; 300 ms retrigger cooldown |
| Destructible | 2 durability; partial charge removes 1, full charge removes 2 |
| One-way | solid only while descending from above; down+jump drops through for 180 ms |
| Switch | toggles one authored channel and persists within the current save slot |
| Locked door | consumes no key; opens permanently after the matching keycard is owned |
| Save station | heals, checkpoints, autosaves, and visibly confirms success/failure |
| Ability shrine | grants one authored ability and becomes inactive permanently |
| Teleporter | transfers only between discovered pads; both endpoints remain usable |
| Lore terminal | uses an authored lore id; never derives content from tile coordinates |

### 5.5 Death and checkpoint loop

- Campaign mode has no finite lives counter.
- Death respawns at the latest activated save station or area entrance.
- Permanent abilities, max HP, gems, keys, lore, bestiary, upgrades, discovered
  map rooms, switches, doors, and shortcuts persist.
- Current combat-room enemies, temporary power-ups, and unbanked room progress
  reset.
- Explorer mode loses no gems. Standard loses 10% unbanked gems, recoverable at
  the death marker. Veteran loses 20%, recoverable once.
- Retry input becomes available no later than 800 ms after the death animation.

## 6. Campaign and room graph

The existing world-map node graph is authoritative for regional connections:

1. Crash Site -> Fungal Caverns
2. Fungal Caverns -> Crystal Depths (Double Jump)
3. Crash Site -> Surface Ruins
4. Fungal Caverns -> Underground Lake
5. Crystal Depths -> Thermal Vents (Dash)
6. Surface Ruins -> Underground Lake
7. Underground Lake -> Thermal Vents
8. Thermal Vents -> Corrupted Ruins (Ground Pound)
9. Surface Ruins -> Overgrowth
10. Overgrowth -> Frozen Abyss (Charge Shot)
11. Frozen Abyss -> Corrupted Ruins (Ground Pound)
12. Corrupted Ruins -> The Core (Grapple)

Every connection is bidirectional after first traversal unless explicitly
collapsed by a reversible story event. The shipwreck hub is attached to Crash
Site and gains fast-travel access after the first major boss.

Each region must contain:

- 6-10 named rooms;
- one unmistakable landmark;
- one region-specific environmental mechanic;
- one traversal tutorial/test/mastery sequence;
- at least two optional secrets;
- at least one shortcut back toward an earlier room;
- one save station at a fair pacing boundary;
- at least two authored lore discoveries;
- one combat set piece or chase; and
- a boss, miniboss, or narrative climax.

## 7. Hub, economy, and loadout

The shipwreck hub exposes Map, Workbench, Archive, Simulator, and Launch console
stations. Hub art visibly changes after each major boss.

Gems are banked salvage. Upgrade branches and exact costs are:

| Branch | Tier 1 | Tier 2 | Tier 3 | Effect per tier |
|---|---:|---:|---:|---|
| Suit Integrity | 8 | 16 | 28 | +1 maximum HP |
| Blaster Capacitor | 10 | 20 | 32 | -8% charge time |
| Thruster Control | 10 | 20 | 32 | -8% dash cooldown |
| Scanner Range | 6 | 14 | 24 | +64 px secret/scan range |

Purchases are permanent, cannot exceed tier 3, never make required traversal
possible earlier than its authored ability, and fail without spending currency
when funds are insufficient.

## 8. Difficulty and accessibility

### 8.1 Difficulty profiles

| Profile | Incoming damage | Enemy HP | Detection/aggression | Gem loss |
|---|---:|---:|---:|---:|
| Explorer | 50% | 85% | 75% | 0% |
| Standard | 100% | 100% | 100% | 10% |
| Veteran | 150% | 125% | 125% | 20% |

Difficulty may be lowered at any save station. Raising it takes effect on the
next room transition. Achievements never require Veteran difficulty.

### 8.2 Persistent settings defaults

| Setting | Default | Range/choices |
|---|---:|---|
| Master volume | 80 | 0-100 |
| Music volume | 55 | 0-100 |
| SFX volume | 85 | 0-100 |
| Ambience volume | 70 | 0-100 |
| Fullscreen | off | on/off |
| UI scale | 100 | 100/125/150% |
| High contrast | off | on/off |
| Large text | off | on/off |
| Reduced motion | off | on/off |
| Reduced flashes | off | on/off |
| Screen shake | 100 | 0-100% |
| Rumble | on | on/off |
| Hold-to-charge | on | hold/toggle |
| Navigation hints | contextual | off/contextual/always |
| Damage assist | 100 | 25/50/75/100% |
| Game-speed assist | 100 | 75/90/100% |

All menus must be usable with keyboard and gamepad. Displayed glyphs come from
the active binding source and may not hard-code keys that disagree with input.

## 9. Save and profile contract

- Three player-selected slots.
- Save schema version starts at `2` for the commercial upgrade.
- New Game requires a slot and difficulty selection.
- Continue loads the most recently written valid slot and is disabled when none
  exists.
- Autosave occurs at save stations, ability acquisition, boss defeat, permanent
  purchase, regional first entry, and clean return to title.
- Writes use a temporary payload and replacement where the available runtime
  surface permits. The previous valid payload is retained as a backup.
- Slot deletion and overwrite require confirmation.
- A slot summary includes area, checkpoint, playtime, completion percentage,
  difficulty, max HP, abilities, gems, and last-write timestamp when available.

Required user-facing failure messages:

- Empty Continue: `No saved expedition is available.`
- Empty selected slot: `This expedition slot is empty.`
- Corrupt save recovered: `Save data was unreadable. A backup was preserved and defaults were restored.`
- Save write failure: `Progress could not be saved. Check storage permissions and try again.`
- Unsupported display mode: `That display mode is unavailable. The previous mode was restored.`
- Missing authored asset: no blocking dialog; use the procedural fallback and
  record the missing path in diagnostics.

## 10. Narrative and content delivery

- The opening is playable after a skippable 10-second maximum cinematic beat.
- Contextual tutorials appear only until acknowledged and are stored per profile.
- ARIA radio lines are short, subtitle-backed, independently volume controlled,
  and never block movement outside explicit cinematics.
- Every region has a primary objective and optional discovery count.
- Lore, bestiary, objectives, and map discoveries use stable authored ids.
- The normal ending requires The Architect's defeat. The true ending additionally
  requires all six abilities and 100% primary lore; it must be reachable.

## 11. Presentation and asset contract

- First-party authored sprites, tiles, portraits, icons, and UI textures live
  under `examples/games/xenoscape/assets/` with source/license notes.
- Assets are bundled by `viper.project`; no network or package download occurs.
- Procedural sprites remain as deterministic fallback/debug assets.
- All gameplay silhouettes remain readable at 1280x720 and UI scale 150%.
- Each biome has at least three background depth layers, foreground dressing,
  localized lighting, ambient motion, a palette grade, and one landmark.
- Animation minimums: 6-frame run, 3-frame turn, 4-frame landing, 4-frame shoot,
  charge loop, dash, grapple, pound, hurt, death, and acquisition poses.
- Reduced-motion disables nonessential parallax, large camera sweeps, logo bounce,
  and repeated screen shake. Reduced-flashes clamps full-screen flashes to alpha 64.

## 12. Audio contract

- Separate mixer intent for music, SFX, ambience, and radio/voice.
- One musical identity per biome plus title, hub, boss, victory, and game-over cues.
- Exploration/combat intensity transitions do not restart a track from zero.
- Footsteps vary by surface. Weapons, abilities, enemy tells, impacts, menus,
  checkpoints, secrets, and saves have distinct feedback.
- All shipped audio is first-party/procedural and zero-dependency.
- Missing audio never prevents play; synthesized fallbacks remain available.

## 13. Meta and postgame modes

- All 32 existing achievements remain and become reachable.
- Event hooks for bullets, jumps, dashes, pounds, lore completion, all abilities,
  pacifist completion, and true ending are mandatory.
- Per-region ranks use time, damage, discoveries, and optional objectives rather
  than cumulative campaign score alone.
- Time Trial unlocks per completed region.
- Boss Rush unlocks after the normal ending.
- New Game+ unlocks after the normal ending and retains upgrades/collectibles
  while applying denser encounters and stronger enemy scaling.

## 14. Test specification

Each gameplay feature lands with a deterministic probe registered in CTest.
The planned lanes are:

| CTest | Coverage |
|---|---|
| `zia_smoke_xenoscape` | one-frame construction/render baseline |
| `zia_xenoscape_level_validation` | all region structural invariants |
| `zia_xenoscape_progression` | save slots and progression metadata |
| `zia_xenoscape_mechanics` | abilities, damage, enemy traits, dynamic tiles |
| `zia_xenoscape_world` | graph reachability, gates, rooms, fast travel |
| `zia_xenoscape_meta` | economy, difficulty, scoring, NG+ |
| `zia_xenoscape_settings` | defaults, clamps, persistence, accessibility |
| `zia_xenoscape_campaign` | deterministic start-to-ending state path |
| `zia_xenoscape_render` | screenshot invariants and fallback assets |
| `zia_xenoscape_package` | packaged authored/fallback asset inventory |

Required Given/When/Then coverage for every feature:

- **Positive:** given all prerequisites, when activated, then the documented
  state change, feedback event, and persistence update occur.
- **Negative:** given a missing prerequisite, when activated, then no protected
  state changes and a nonblocking feedback result is produced.
- **Edge:** given minimum/maximum values, repeated input, frame spikes, full
  pools, missing assets, or corrupt saves, then execution remains deterministic
  and playable without a trap.

VM and native results must match for pure probes. Display probes use the software
or deterministic Canvas path where available and share the `viper_display`
resource lock when a window is required.

## 15. Workstreams and acceptance gates

### Phase A: mechanics truth pass

- [x] Register level, progression, and mechanics CTests.
- [x] Consolidate ability gating and implement charge damage.
- [x] Integrate enemy traits, scores, contact damage, status, and death FX.
- [x] Implement dynamic environmental tiles and interaction state.
- [x] Make lore/transitions truly modal.
- [x] Repair all currently unreachable achievement hooks.

Gate: every visible affordance in the existing campaign either functions or is
temporarily noninteractive without claiming behavior it does not have.

### Phase B: world and release foundation

- [x] Implement the regional graph, room ids, return exits, discovery, and map.
- [x] Implement the shipwreck hub, economy, objectives, and difficulty profiles.
- [x] Replace lives with save-station respawn and recoverable loss.
- [x] Add versioned profiles, autosave, settings, confirmations, and migration.

Gate: a deterministic profile can start, save, quit, continue, revisit a room,
buy one upgrade, die, respawn, and retain exactly the specified permanent state.

### Phase C: gold-standard slice

- [x] Release front end and profile flow.
- [x] Playable opening and Crash Site onboarding.
- [x] Re-authored Crash Site and Fungal Caverns rooms.
- [x] Final Spore Mother fight and Double Jump acquisition.
- [x] Hub return, archive, workbench, map, art, music, and accessibility.

Gate: title -> new profile -> Crash Site -> Fungal Caverns -> Spore Mother ->
hub return is shippable without placeholder UI, silent interactions, or debug
shortcuts.

### Phase D: campaign rollout

- [x] Re-author Crystal Depths through The Core using the gold-slice template.
- [x] Complete all bosses, traversal mastery, secrets, lore, and shortcuts.
- [x] Complete authored sprites, animation, environments, VFX, and adaptive audio.
- [x] Add Time Trial, Boss Rush, New Game+, ranks, and true ending.

Gate: all ten areas meet the per-region content contract and both endings are
reachable from clean profiles.

### Phase E: release closure

- [x] Complete responsive HUD/menu, dynamic glyphs, and all settings.
- [x] Validate keyboard/gamepad, VM/native, macOS packaging, and cross-platform
      source policy; retain Windows/Linux execution as repository CI gates.
- [x] Add render, performance, save-migration, and asset-manifest gates.
- [x] Update game-engine example docs, testing docs, controls, credits, and package metadata.
- [x] Set release defaults, disable debug by default, and retain only documented
      authored-art fallbacks for deterministic recovery testing.

Gate: full build scripts and all Xenoscape CTests pass with zero warnings; package
smokes resolve every mandatory asset from a non-source working directory.

## 16. Doxygen and source-header policy

Every new source file uses the full Viper source header. Every new class and
function receives Doxygen documentation describing purpose, parameters, return
value, invariants, ownership, state mutation, and failure behavior as applicable.
Modified legacy files receive the full header when first touched by this program.
Probe helpers are documented to the same standard as production helpers.

## 17. Documentation and ADR routing

- Game design and player documentation lives under
  `examples/games/xenoscape/docs/` once Phase B begins.
- CTest taxonomy changes update `docs/testing.md` when a new label or policy is
  introduced; registering more tests under existing labels needs no doc change.
- Existing runtime behavior may be cited from `docs/viperlib/`; do not edit
  runtime API docs merely to describe game-local behavior.
- Any runtime ABI/API requirement pauses its implementation until an ADR defines
  signatures, ownership, errors, cross-platform behavior, and tests.
