# XENOSCAPE: The Descent

XENOSCAPE is a complete ten-region science-fiction action Metroidvania and a
release-quality Viper/Zia game example. It includes a nonlinear world graph,
six traversal abilities, persistent profiles, an evolving shipwreck hub,
regional bosses, accessibility assists, contextual tutorials, performance
ranks, Time Trials, Boss Rush, New Game+, authored key art, adaptive procedural
music, and permanent deterministic art/audio fallbacks.

## Run from source

Build Viper with the platform script from the repository root, then run:

```sh
./build/src/tools/viper/viper run examples/games/xenoscape
```

To build the standalone project:

```sh
./build/src/tools/viper/viper build examples/games/xenoscape
```

The game targets 1280x720 at 60 Hz. Keyboard and gamepad actions share the
runtime action-binding source, so prompts reflect the bindings that are
actually active.

## Release features

- Three explicit profile slots with New/Load/Delete/Overwrite confirmation,
  versioned saves, corruption backup, checkpoints, and clean-exit autosave.
- Ten regions, eighty named rooms, twelve bidirectional routes, stable lore and
  interaction ids, two secrets per region, landmarks, mastery routes, and
  regional combat climaxes.
- Wall Jump, Double Jump, Dash, Charge Shot, Ground Pound, and Grapple, plus
  ice, quicksand, crumble, conveyor, steam, destructible, one-way, switch,
  keyed-door, shrine, save-station, teleporter, and lore interactions.
- Shipwreck Map, Workbench, Archive, Simulator, and Launch stations; four
  permanent three-tier upgrade branches; Explorer/Standard/Veteran profiles.
- Per-region D/C/B/A/S ranks, unlocked Time Trials, four-boss Boss Rush, and
  New Game+ with 135% enemy HP and 150% encounter density.
- Authored title key art and region-specific procedural environments/music,
  with missing-asset fallbacks exercised by automated tests.

## Documentation

- [Player and controls guide](docs/player-guide.md)
- [Accessibility and difficulty](docs/accessibility.md)
- [Profiles, saves, and recovery](docs/saves-and-profiles.md)
- [Developer architecture](docs/architecture.md)
- [Testing and release validation](docs/testing.md)
- [Version 1.0 release notes](docs/release-notes.md)
- [Credits and licenses](docs/credits.md)
- [Asset provenance](assets/README.md)

XENOSCAPE and its bundled first-party assets are distributed under GNU GPL v3
as part of the Viper project.
