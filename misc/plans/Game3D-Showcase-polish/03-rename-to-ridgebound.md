# Rename Plan: Game3D-Showcase to Ridgebound

Recommended name: **Ridgebound**.

Recommended full title: **Ridgebound: The Five Beacons**.

This plan intentionally separates user-facing rename work from path/build/test
rename work. Do the rename in two stages so breakage is easy to isolate.

## Naming Decision

### Why Ridgebound

- It sounds like a game, not a tech demo.
- It matches current content: ridges, forests, beacons, terrain traversal.
- It is short enough for window titles, build labels, and docs.
- It does not imply content the demo does not have.

### Alternatives Considered

- `Beaconfall` - more dramatic, but suggests falling/destruction not present.
- `Aether Ridge` - atmospheric, but more generic.
- `Ridgewalkers` - implies party/characters not present.
- `The Five Beacons` - good subtitle, less distinctive as a project name.
- `Skybreak Ridge` - strong, but implies sky fracture/story beats not present.

Use `Ridgebound` for directories/classes and `Ridgebound: The Five Beacons` for
player-facing title/menu copy.

Trademark/name availability is not vetted by this plan.

## Rename Stage 1: User-Facing Rename Only

Goal: make the demo feel like a game without moving files yet.

Change:

- `config.WINDOW_TITLE`
  - From: `Game3D Open World Showcase`
  - To: `Ridgebound: The Five Beacons`
- HUD title text:
  - Remove `GAME3D OPEN WORLD`.
  - Use `RIDGEBOUND` or title art from the new menu.
- Objective text:
  - From: `Ridge beacons`
  - To: `Beacons linked`
  - Supporting copy: `Restore the ridge network.`
- README title:
  - From: `Game3D Open World Showcase`
  - To: `Ridgebound`
  - First paragraph should say it is a playable Game3D sample.
- Smoke/probe window titles:
  - Use `Ridgebound Smoke`, `Ridgebound Tree Material Probe`, etc.

Do not move paths in this stage.

Acceptance:

- `viper check examples/games/game3d-showcase --diagnostic-format=json`
- Existing smoke probe passes.
- No code path changes except strings and copy.

## Rename Stage 2: Code Symbol Rename

Goal: remove `Showcase` from game-owned class names without moving files.

Recommended class renames:

| Current | New |
|---------|-----|
| `Game3DShowcase` | `RidgeboundGame` |
| `ShowcasePlayer` | `RidgeboundPlayer` |
| `ShowcaseCamera` | `RidgeboundCamera` |
| `ShowcaseTerrain` | `RidgeboundTerrain` |
| `ShowcaseAssets` | `RidgeboundAssets` |
| `ShowcaseWaterSky` | `RidgeboundWaterSky` |
| `ShowcaseForest` | `RidgeboundForest` |
| `ShowcaseCritters` | `RidgeboundCritters` |
| `ShowcaseAudio` | `RidgeboundAudio` |
| `ShowcaseEffects` | `RidgeboundEffects` |
| `ShowcaseWeather` | `RidgeboundWeather` |
| `ShowcaseMinimap` | `RidgeboundMinimap` |
| `ShowcaseHud` | `RidgeboundHud` |
| `ShowcaseWorldSim` | `RidgeboundWorldSim` |

Function/local rename candidates:

- `createShowcaseHeightmap` -> `createRidgeboundHeightmap`
- `sampleShowcaseHeight` -> `sampleRidgeboundHeight`
- `assertShowcaseFrame` -> `assertRidgeboundFrame`

Do not rename generic runtime concepts or separate `examples/3d/game3d_showcase`
symbols in this stage.

Acceptance:

- `rg -n "Game3DShowcase|Showcase[A-Z]|showcase frame|Game3D Showcase" examples/games/game3d-showcase`
  should return only intentionally retained compatibility comments, if any.
- `viper check examples/games/game3d-showcase --diagnostic-format=json`
- Smoke, tree material probe, fallback material probe pass.

## Rename Stage 3: Directory and Project Rename

Goal: move the demo from a showcase path to a game path.

Move:

- `examples/games/game3d-showcase/`
- To: `examples/games/ridgebound/`

Update project manifest:

- From: `project game3d_showcase`
- To: `project ridgebound`

Update asset paths:

- `TREE_MODEL_PATH_ALT`
  - From: `examples/games/game3d-showcase/MapleTree_1.fbx`
  - To: `examples/games/ridgebound/MapleTree_1.fbx`
- `TREE_MODEL_PATH_FROM_BIN`
  - From: `../games/game3d-showcase/MapleTree_1.fbx`
  - To: `../games/ridgebound/MapleTree_1.fbx`

Update temp/screenshot names:

- `/tmp/game3d_showcase_games_current.png`
- To: `/tmp/ridgebound_current.png`

Update build scripts:

- `scripts/build_demos_mac.sh`
- `scripts/build_demos_linux.sh`
- `scripts/build_demos_win.cmd`

Recommended demo label:

- From: `game3d-showcase`
- To: `ridgebound`

For one transition release, consider accepting the old label as an alias in the
build scripts if practical. Do not use filesystem symlinks; Windows support and
packaging make symlink compatibility a poor tradeoff.

Update tests:

- `src/tests/CMakeLists.txt`
  - `zia_smoke_game3d_showcase` -> `zia_smoke_ridgebound`
  - `zia_smoke_game3d_showcase_tree_materials` -> `zia_smoke_ridgebound_tree_materials`
  - `zia_smoke_game3d_showcase_tree_fallback_materials` -> `zia_smoke_ridgebound_tree_fallback_materials`
  - backend variants similarly.
- `src/tests/tools/AssetCompilerTests.cpp`
  - Update path and test name.

Be careful:

- `examples/3d/game3d_showcase/` is a separate integration sample. Do not rename
  it in this pass unless a later plan explicitly folds it into Ridgebound.
- CTest names for the separate integration sample (`g3d_game3d_showcase`) may
  remain unchanged if that path remains unchanged.

Update docs:

- `README.md`.
- `examples/README.md`.
- `misc/video/runbook.md`.
- `misc/video/script.md`.
- `misc/presentation/build_deck.py`.
- Any current release notes if this is not historical/changelog text.

Historical release notes should usually remain historically accurate. If they
mention `game3d-showcase` as past release content, leave them unless release
policy says otherwise.

Acceptance:

- `rg -n "examples/games/game3d-showcase|game3d-showcase|game3d_showcase|Game3D Open World Showcase|Game3DShowcase" .`
  returns only:
  - Historical release notes.
  - The separate `examples/3d/game3d_showcase` sample.
  - Explicit backwards-compatibility mentions.
- All build demo scripts include `ridgebound`.
- All CTest paths point to `examples/games/ridgebound`.
- Asset compiler tests point to the new path.

## Rename Stage 4: README Reframe

The README should stop leading with runtime API coverage and start with the
game premise.

Suggested README opening:

```markdown
# Ridgebound

Ridgebound is a playable open-world Game3D sample about restoring five beacon
sites across a forested mountain basin. It uses Viper's Game3D world layer for
movement, physics, audio, and effects, with direct Graphics3D rendering for
terrain, water, sky, vegetation, and the HUD.
```

Feature lists can still mention runtime coverage, but phrase it as implementation
detail after the game pitch.

## Rename Stage 5: Commit and Rollout Strategy

Recommended commits:

1. `docs(examples): rename Game3D showcase copy to Ridgebound`
2. `refactor(examples): rename Ridgebound demo symbols`
3. `refactor(examples): move Ridgebound demo path`
4. `test(examples): rename Ridgebound smoke tests`

If the team prefers fewer commits, combine stages 2 through 4, but keep Stage 1
separate from path moves.

## Rollback Strategy

If path rename causes broad fallout:

- Keep user-facing `Ridgebound` strings and symbol renames.
- Temporarily leave the directory as `examples/games/game3d-showcase`.
- File a follow-up to complete script/test/doc path migration.

Do not revert runtime fixes because the player flyaway is independent of the
rename.

