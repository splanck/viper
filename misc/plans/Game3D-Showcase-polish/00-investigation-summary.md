# Investigation Summary

This document records the current diagnosis that motivates the plan bundle.
It is intentionally source-referenced so a later implementation pass can verify
whether the same code is still present before editing.

## Issue A: Game3D-Showcase Player Flies Away

### Symptom

After startup, the player rises/floats away and the camera follows the player.
The camera behavior is downstream of the player state; it is not the primary
cause.

### Root Cause

The demo now uses `Game3D.CharacterController3D` for the player:

- `examples/games/game3d-showcase/playerctl.zia:40` spawns the player entity at
  `sy + 1.2`.
- `PLAYER_CAPSULE_HEIGHT` is `1.8`, so the expected capsule-center height for
  feet on the terrain is `sy + 0.9`. The demo intentionally starts roughly
  `0.3` units airborne so the controller can settle onto the terrain.
- `examples/games/game3d-showcase/playerctl.zia:43` creates the
  `CharacterController3D`.
- `examples/games/game3d-showcase/playerctl.zia:68` calls `controller.Update`.

The runtime controller then applies gravity with the wrong sign:

- `src/runtime/graphics/3d/rt_game3d_controllers.c:429` adds
  `gravity * dt` while airborne.
- `src/runtime/graphics/3d/rt_game3d_controllers.c:437` passes that positive
  Y velocity to `Character3D.Move`.
- `src/runtime/graphics/3d/physics/rt_physics3d_character.c:550` converts
  positive velocity Y into positive displacement Y.

Positive world Y is upward throughout the renderer and physics code. Therefore
the controller accelerates upward when it should accelerate downward.

### Why It Shows Up Now

The sign bug predates the latest demo changes, but the showcase migration made
it obvious:

- The player now starts slightly airborne by design.
- The first `CharacterController3D.Update` sees an airborne state.
- Instead of falling/settling, the capsule accelerates upward.
- `examples/games/game3d-showcase/game.zia:155` re-runs camera follow after the
  player update, so the camera accurately tracks the bad position.

### Secondary Contributing Factor

`Input3D.MoveAxis` currently mixes horizontal movement with vertical controls:

- `src/runtime/graphics/3d/rt_game3d_input.c:186` adds positive Y for Space.
- `src/runtime/graphics/3d/rt_game3d_input.c:188` through `:191` subtracts Y
  for Shift/Ctrl.
- `src/runtime/graphics/3d/rt_game3d_input.c:193` normalizes the 3D axis.

For a walking character controller, Shift is sprint, not vertical descent.
When W + Shift are held, the normalized Z magnitude is reduced before sprint
speed is applied. That is not the startup flyaway root cause, but it weakens the
controller behavior and should be addressed in the same runtime pass.

## Issue B: Game3D-Showcase Mild Edge/Fisheye Impression

### Symptom

The showcase has visible edge/corner darkening and some peripheral stretching,
especially around foreground trees and terrain.

### Findings

The showcase is not using the most severe FOV mistake. It creates the world with
a horizontal-FOV constructor:

- `examples/games/game3d-showcase/game.zia:53` calls
  `World3D.WithHorizontalCamera`.
- `examples/games/game3d-showcase/camctl.zia:85` updates the camera with
  `Camera3D.SetHorizontalFov`.
- `src/runtime/graphics/3d/render/rt_camera3d.c:195` through `:203` converts
  horizontal FOV to the vertical FOV used by the projection matrix.

At 16:9:

- `72` horizontal FOV is about `44.5` vertical FOV.
- `80` sprint horizontal FOV is about `51.3` vertical FOV.

Those values are not extreme, but the camera is close to the player, can zoom
down to `CAMERA_DISTANCE_MIN`, and uses a visible vignette:

- `examples/games/game3d-showcase/config.zia:15` sets `CAMERA_FOV = 72.0`.
- `examples/games/game3d-showcase/config.zia:239` sets
  `CAMERA_FOV_SPRINT = 80.0`.
- `examples/games/game3d-showcase/config.zia:229` sets
  `POSTFX_VIGNETTE_RADIUS = 0.92`.
- `examples/games/game3d-showcase/config.zia:230` sets
  `POSTFX_VIGNETTE_SOFT = 0.12`.
- `examples/games/game3d-showcase/postfx.zia:29` adds the vignette.

### Runtime Vignette Behavior

The CPU path:

- `src/runtime/graphics/3d/render/rt_postfx3d.c:816` applies vignette.
- `src/runtime/graphics/3d/render/rt_postfx3d.c:830` starts darkening when
  `dist > radius`.
- `src/runtime/graphics/3d/render/rt_postfx3d.c:831` reaches black over
  `softness`.

The GPU paths apply equivalent radial math:

- Metal: `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m:3004`.
- D3D11: `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shaders.inc:941`.
- OpenGL: `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_shaders.inc:919`.

Game3D-Showcase is therefore not broken in the same way as 3DScene, but the
current tuning can still read as a lens artifact.

## Issue C: 3DScene Severe Corner/Fisheye Impression

### Symptom

The 3DScene demo shows a strong dark oval/corner artifact that reads as a bad
fisheye effect around the corners.

### Root Cause

This demo combines a wide vertical FOV with a hard vignette.

Camera:

- `examples/games/3dscene/game.zia:103` uses `Camera3D.New`.
- `examples/games/3dscene/config.zia:14` sets `CAMERA_FOV = 60.0`.
- `Camera3D.New` treats this value as vertical FOV.

At 16:9, `60` vertical FOV implies roughly:

- `91.5` horizontal FOV.
- `99.3` diagonal FOV.

Post-FX:

- `examples/games/3dscene/game.zia:244` adds
  `PostFX3D.AddVignette(postfx, 0.84, 0.10)`.

The GPU shader computes `length(uv - 0.5) * 1.41421356`, so the extreme corners
are normalized near `1.0`. With radius `0.84` and softness `0.10`, the vignette
reaches full black before the corners. That creates the visible black oval seen
in the smoke capture.

### Important Distinction

3DScene has a concrete tuning/API-use problem:

- It likely wanted a human-authored horizontal-ish camera aperture but passed
  `60` into a vertical-FOV constructor.
- It uses aggressive vignette parameters.

Game3D-Showcase is using the horizontal-FOV API correctly but still needs softer
game-facing tuning.

## Existing Test Gap

The current probes pass while the diagnosed problems remain possible:

- `examples/games/game3d-showcase/smoke_probe.zia` checks frame composition,
  rough luminance, HUD overlay, and diagnostics, but not player/camera state.
- `examples/games/3dscene/smoke_probe.zia` saves a screenshot and reports success
  if save/flip works, but does not detect black corners.

The validation plan must add state assertions and image heuristics that target
these exact regressions.

