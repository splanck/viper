# ASHFALL

A single-player sci-fi campaign FPS built entirely in Zia on the Viper engine.
Salvager **Rook Ryder** fights inward through nine levels of the ash-world
Erebos-4 against **HELIX**, a corrupted terraforming AI, and its all-machine
garrisons.

Everything here is from-scratch Zia — no external dependencies — and 100%
cross-platform (Metal / D3D11 / OpenGL, plus the software backend). The build
ships fully playable with procedural graybox art (the asset-optional contract);
downloaded models swap in over the same material/mesh factory.

## Running

```sh
# From a built Viper checkout:
build/src/tools/viper/viper run examples/games/ashfall/main.zia            # fullscreen
build/src/tools/viper/viper run examples/games/ashfall/main.zia -- --windowed
build/src/tools/viper/viper run examples/games/ashfall/main.zia -- --level 1
build/src/tools/viper/viper run examples/games/ashfall/main.zia -- --smoke  # headless self-check
```

Fullscreen is the default; F11 toggles. `--windowed` forces a window.

## Controls

| Action | Key / Mouse |
|--------|-------------|
| Move | WASD |
| Look | Mouse (raw relative) |
| Jump | Space (coyote-time + input buffer) |
| Crouch / slide | Left Ctrl (sprint + crouch = slide) |
| Sprint | Left Shift |
| Fire / ADS | LMB / RMB |
| Reload | R |
| Interact | E |
| Next / prev weapon | `]` / `[` |
| Grenade / cycle | G / Q |
| Melee | V |
| Diagnostics (F3) | F3 |
| Pause | Esc |

## What's inside

- **Fixed-timestep sim** (60 Hz) on a game-owned accumulator with render
  interpolation; deterministic (VM == native) so every system is headless-probe
  testable.
- **Player feel**: Character3D movement (accel/friction/air-control), jump with
  coyote-time + buffering, crouch-slide, stances that drive AI-audible noise, a
  sprung first-person camera with landing dip / sprint FOV / ADS / shake, and a
  procedural view-model motion stack (sway / bob / recoil).
- **10 weapons + 3 grenades + melee**, each a distinct mechanic: hitscan pistol,
  8-pellet scattergun, high-ROF SMG, ADS pulse rifle, penetrating rail, bouncing
  arc launcher, DoT rivet driver, ricochet shard caster, EMP projector, overheat
  beam lance. One event-driven damage pipeline feeds hitscan, projectiles, and
  occlusion-aware blasts, with locational (head/weakpoint/plate) multipliers.
- **11 enemy archetypes + 3 bosses** on a shared AI substrate: sight-cone + LOS
  perception, an awareness meter, event-bus hearing, an int FSM
  (idle/patrol/suspicious/searching/combat/stagger/disabled/dead), per-archetype
  behavior (swarm, kamikaze, ranged-cover, flyer, turret, shielded, flanker,
  cloak), and a squad director that caps engaged AIs and hands out attack tokens.
- **9 campaign levels + hub + a permanent arena**, built through a LevelBuilder
  that a JSON manifest loader also targets; objectives (reach / kill / activate /
  survive / clear / collect), wave progression, pickups, and level transitions.
- **Meta**: salvage economy + tiered workbench upgrades, per-level scoring +
  medals, three difficulties + NG+ scalars, run stats.
- **Presentation**: combat HUD (health/shield/armor, ammo, objective, detection,
  hitmarkers), particle FX, a post-FX chain, and a headless-safe audio mixer with
  a combat-intensity music state machine.

## Layout

```
main.zia game.zia config.zia
core/   bits mathutil rng events entity strings savegame devtools
player/ actions playerctl camera_rig viewmodel health
weapons/ weapon_defs targets ballistics projectiles weapon_base
ai/     enemy_defs ai_core
world/  level_base levels level_manager loader objectives pickups arena
ui/ hud   fx/ effects postfx   audio/ mix   meta/ economy scoring difficulty
assets/ fallbacks   probes/ (headless deterministic self-tests)
```

## Tests

Ten headless deterministic probes under `probes/` are registered in ctest
(`zia_smoke_ashfall_*`): core systems, movement course, weapons/damage, AI +
enemies + bosses, level tech + all 9 levels, JSON manifest round-trip, meta
systems, the full render pipeline, and a full-campaign playthrough. All run on
the software backend and are VM==native.

See `misc/plans/fps/` for the design docs and `CREDITS.md` for licensing.
