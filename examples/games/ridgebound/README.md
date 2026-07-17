# Ridgebound

Ridgebound is a compact open-world Game3D demo about reconnecting five named
beacon sites around a mountain lake. The release pass prioritizes dependable
walking, a legible route network, a clear objective, and restrained outdoor
presentation over unrelated showcase systems.

## Run

From the repository root:

```sh
zanna run examples/games/ridgebound/
```

The demo opens on a scenic title view. Start a run, follow the dirt trails and
dry lake causeways, aim at a beacon, and hold `E` to link it. Once all five are
online, return to the central sanctuary.

## Controls

- `WASD` or arrows: move
- Mouse: look; click the game view to capture the pointer
- Wheel: orbit-camera zoom
- `Shift`: sprint while stamina remains
- `Space`: jump (ordinary routes do not require it)
- `E`: hold while aiming at a beacon to link it
- `V`: toggle orbit and first-person views
- `H`: show the controls card
- Right mouse: cycle Performance, Balanced, and Cinematic quality
- `F11`: toggle fullscreen
- `Ctrl`: toggle diagnostics
- `Esc`: pause, resume, or go back

## Release Design

- One connected, vegetation-cleared trail network joins spawn to every beacon.
  Broad causeways remain above the lake and every arrival site has a level pad.
- Character motion uses the production capsule controller with a 0.48 m step,
  a 52-degree slope limit, camera-relative input, and explicit surface swimming
  if the player leaves a causeway.
- The 15-minute day cycle starts in clear morning light. First dusk is 342
  seconds into active gameplay; title and pause screens do not consume world
  time. Deep night retains moonlight, cool ambient fill, and a player-local
  visibility light.
- The five sites have distinct names, landmark silhouettes, compass directions,
  distances, activation pulses, light ramps, spatial chimes, and network feedback.
- A backpacked procedural explorer, subdued PBR palette, sparse imported-maple
  forest, bounded lake, low-bloom post-processing, compact HUD, and responsive
  menus replace the former debug-shape presentation.
- Survival meters and the physics-toy pile are opt-in. They are disabled in the
  default route-focused demo; ambient sentinels remain enabled.

## Quality Presets

All tiers retain the authored exposure and color direction. They scale the
expensive work instead of changing the game:

- Performance: short vegetation LOD, reduced weather emission, 768 px/one-cascade
  shadows, lower water reflection quality, and no occlusion pass.
- Balanced: medium LOD, weather, water, and two-cascade shadows.
- Cinematic: full vegetation and weather distances, 2048 px/three-cascade
  shadows, full water reflection, SSAO where supported, and the complete safe
  post-processing chain.

At 960x540 the release smoke gate budgets a warmed Cinematic frame at 50 ms on
GPU backends and 250 ms on the deterministic software fallback.

## Validation

The standalone gates use an existing Zanna executable. They never configure or
rebuild Zanna and never invoke CTest.

```sh
ZANNA_BIN=build/src/tools/zanna/zanna \
  examples/games/ridgebound/run_probes.sh
```

On Windows:

```bat
set ZANNA_BIN=build\src\tools\zanna\zanna.exe
examples\games\ridgebound\run_probes.cmd
```

The runner first checks the whole project, then executes:

- `topology_probe.zia`: dry route grades, submerged lake coverage, level pads,
  beacon count, and initial compass target.
- `traversal_probe.zia`: sustained real `W` input down every route branch and
  one off-route swim with no jump input, bounding progress, stalls, support,
  ground separation, and water entry.
- `state_probe.zia`: title/pause clocks, delayed dusk, calm opening weather,
  deterministic restart/quit-to-title resets, and quality-tier application.
- `smoke_probe.zia`: full and compact title/gameplay captures, compact
  pause/options/controls states, day/night luminance, HUD/minimap completeness,
  bright-blob coverage, diagnostics, and the warmed Cinematic frame budget.

The smoke probe writes visual evidence to `/tmp/ridgebound_title.png`,
`/tmp/ridgebound_title_compact.png`, `/tmp/ridgebound_current.png`,
`/tmp/ridgebound_night.png`, `/tmp/ridgebound_compact.png`, and compact
pause/options/controls captures on POSIX hosts.
See [IMPROVEMENT_AUDIT.md](IMPROVEMENT_AUDIT.md) for the 20-point implementation
matrix and measured release evidence.

## File Map

- `main.zia`: project entry point and packaged smoke path
- `game.zia`: lifecycle, state machine, orchestration, and quality application
- `terrain.zia`: mountain basin, lake floor, routes, pads, splat map, and grass
- `playerctl.zia`: character controller, stamina, gait, surface audio, and swimming
- `camctl.zia`: orbit/first-person rigs, damping, and collision-safe camera boom
- `worldsim.zia`: named beacons, landmarks, scanner, lighting, clock, and objective
- `water_sky.zia`: bounded water, shoreline, four sky keyframes, and reeds
- `forest.zia`: deterministic route-cleared imported/procedural maple forest
- `assets.zia`: shared meshes and restrained PBR material palette
- `audio.zia`: procedural surface steps, water, beacon, victory, and ambience audio
- `postfx.zia`: quality-aware post-processing, sprint dust, and scorch decals
- `weather.zia`: slow clear-to-storm cycle with quality-scaled rain and snow
- `hud.zia`, `minimap.zia`, `menu3d.zia`: responsive navigation and front end
- `critters.zia`, `survival.zia`: optional secondary simulation layers
- `config.zia`: centralized tuning and release budgets
- `*_probe.zia`: standalone release acceptance gates

The optional `MapleTree_1.fbx` is resolved from source, repository, and packaged
locations. If it is unavailable, Ridgebound plants its dependency-free
procedural fallback trees instead.
