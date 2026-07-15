# Demo Bugfix and Polish Plan

This plan covers demo-level changes after the runtime fixes are understood. It
includes both the renamed Game3D demo, referred to here as Ridgebound, and the
separate 3DScene demo because the investigation found a concrete 3DScene issue.

## Workstream D1: Stabilize Ridgebound Player Startup

### Current State

The player is spawned at:

- `examples/games/game3d-showcase/playerctl.zia:40`

Current value:

```zia
entity.SetPosition(sx, sy + 1.2, sz);
```

With capsule height `1.8`, feet-on-ground center is `sy + 0.9`. The current
spawn is about `0.3` units above ground.

### Preferred Fix After Runtime Gravity Is Correct

Use explicit capsule-center semantics:

```zia
var centerY = sy + config.PLAYER_CAPSULE_HEIGHT * 0.5 + 0.05;
entity.SetPosition(sx, centerY, sz);
```

Why:

- Expresses the intended center-vs-feet conversion.
- Keeps a small clearance without relying on a magic `1.2`.
- Reduces first-frame penetration risk.
- Keeps cached `py = sy` as feet/ground Y.

### Additional Guard

After controller creation, consider immediately reading `character.Position` and
syncing `px/py/pz` from the controller rather than duplicating expected values.
If this is done, preserve the existing public meaning:

- `getY()` returns feet/ground-relative Y.
- Draw/camera code expects feet Y.

### Acceptance Criteria

- With no input over the first 120 frames, player feet Y remains near terrain
  height and does not rise without a jump.
- Camera target remains near the player and does not climb away.
- The player body still renders above the terrain in third-person mode.
- Footstep and sprint dust still use feet Y.

## Workstream D2: Retune Ridgebound Camera FOV

### Current State

- `CAMERA_FOV = 72.0` horizontal.
- `CAMERA_FOV_SPRINT = 80.0` horizontal.
- `World3D.WithHorizontalCamera` is used.
- `Camera3D.SetHorizontalFov` is used during sprint kick.

This is conceptually correct, but a little broad once combined with close
third-person framing and corner vignette.

### Recommendation

Retune for a game camera:

- Resting horizontal FOV: `64.0` to `68.0`.
- Sprint horizontal FOV: `72.0` to `74.0`.
- Keep `SetHorizontalFov`, not vertical `Fov`.

Suggested first pass:

```zia
var CAMERA_FOV: Float = 66.0;
var CAMERA_FOV_SPRINT: Float = 74.0;
```

At 16:9:

- 66 horizontal is about 39.2 vertical.
- 74 horizontal is about 45.9 vertical.

This should reduce foreground edge stretching while preserving a useful sprint
kick.

### Follow Camera Position

Do not make camera-distance changes in the same commit as FOV changes unless
manual play shows the framing is still too close. FOV and distance interact; if
both change together, visual regressions are harder to attribute.

If distance changes are needed later:

- Increase `CAMERA_DISTANCE_MIN` slightly before changing default distance.
- Keep `CAMERA_LOOK_AHEAD` modest so the player stays readable.

## Workstream D3: Retune Ridgebound Post-FX

### Current State

Ridgebound uses:

- `POSTFX_VIGNETTE_RADIUS = 0.92`.
- `POSTFX_VIGNETTE_SOFT = 0.12`.

This is less severe than 3DScene but still visible at the corners.

### Recommendation

Use a soft gameplay vignette:

```zia
var POSTFX_VIGNETTE_RADIUS: Float = 0.96;
var POSTFX_VIGNETTE_SOFT: Float = 0.28;
```

Alternative if the world becomes too flat:

```zia
var POSTFX_VIGNETTE_RADIUS: Float = 0.94;
var POSTFX_VIGNETTE_SOFT: Float = 0.24;
```

Avoid `radius + softness < 1.0` for normal gameplay unless deliberate black
corners are desired.

### Acceptance Criteria

- Corners are not full black in the default gameplay frame.
- HUD/minimap remain readable.
- Distant terrain remains visible enough for navigation.
- Software and GPU backends have similar visible edge strength.

## Workstream D4: Fix 3DScene Camera and Vignette

3DScene is a separate demo, but it shares the same camera/post-FX confusion and
should be fixed in the same polish effort.

### Current State

- `examples/games/3dscene/game.zia:103` uses `Camera3D.New`.
- `examples/games/3dscene/config.zia:14` sets `CAMERA_FOV = 60.0`.
- This means 60 vertical FOV, about 91.5 horizontal FOV at 16:9.
- `examples/games/3dscene/game.zia:244` uses vignette `0.84, 0.10`,
  producing black corners.

### Option A: Keep Vertical FOV, Lower It

Use:

```zia
var CAMERA_FOV: Float = 45.0;
```

Pros:

- Minimal code change.
- Preserves `Camera3D.New` usage.

Cons:

- Demo authors may still read the FOV as a horizontal value later.

### Option B: Switch to Horizontal FOV

Use:

```zia
camera = Camera3D.WithHorizontalFov(config.CAMERA_FOV, aspect, config.CAMERA_NEAR, config.CAMERA_FAR);
```

Then set:

```zia
var CAMERA_FOV: Float = 64.0;
```

Pros:

- Aligns authoring with game-camera intuition.
- Prevents future vertical/horizontal confusion.

Cons:

- Requires updating docs/comments to say the config is horizontal.

Recommendation: Option B, especially if the runtime adds a `HorizontalFov`
property.

### Vignette Fix

Change:

```zia
PostFX3D.AddVignette(postfx, 0.84, 0.10);
```

To either:

```zia
PostFX3D.AddVignette(postfx, 0.96, 0.28);
```

Or remove the vignette entirely if the demo is meant to show reflective geometry
and procedural sky without a lens effect.

Recommendation: use `0.96, 0.28` first.

### Smoke Probe Addition

Enhance `examples/games/3dscene/smoke_probe.zia`:

- Load final screenshot.
- Sample all four corners and a center region.
- Fail if average corner luminance is near zero while center is bright.
- Keep thresholds loose to avoid backend brittleness.

Suggested heuristic:

- `cornerAvg >= centerAvg * 0.18`, or
- absolute `cornerAvg >= 12`, whichever is more stable.

## Workstream D5: Ridgebound Player/Camera Smoke Probe

Enhance `examples/games/game3d-showcase/smoke_probe.zia` after rename:

State checks:

- Add accessors if needed:
  - Player feet Y.
  - Camera Y.
  - Maybe player X/Z for no-input drift.
- Run setup and simulate a fixed number of frames with no input.
- Assert:
  - Player feet Y does not increase by more than a small tolerance.
  - Player feet Y remains within terrain bounds.
  - Camera Y does not increase unbounded.
  - Diagnostics summary remains empty.

Visual checks:

- Keep existing HUD and scene-structure checks.
- Add corner-luminance guard similar to 3DScene, but with lower strictness
  because Ridgebound intentionally has some vignette.

Implementation caution:

- Do not expose player internals only for tests unless there is a clean demo
  accessor. It is acceptable for the demo class to expose a small
  `debugPlayerY()` or `captureStateSummary()` method if kept local to the demo.

## Workstream D6: Gameplay Polish Around the Existing Objective

These are demo-level polish items that can happen after correctness:

- Replace "Ridge beacons: 0 / 5" with game title/objective language:
  - "Beacons linked: 0 / 5"
  - "Restore the ridge network."
- Make the first objective clearer from spawn:
  - A visible beacon light column.
  - A subtle compass/minimap ping.
  - Reticle text only when aimed at a target.
- Keep controls out of the first-viewport HUD once the 3D menu exists.
  - Move controls to pause/help menu.
  - Keep a short first-run hint if needed.
- Use camera shake sparingly after beacon activation; avoid compounding with
  perceived lens artifacts.

## Workstream D7: Documentation Cleanup

Update after rename:

- Demo README file map and controls.
- `examples/README.md`.
- Root `README.md`.
- Release notes only if this is included in an active release branch.
- `misc/video/runbook.md` and presentation references.
- Any docs that still describe the demo as just a runtime showcase.

Keep the README framed as a playable mini-game that also exercises the runtime,
not a runtime test harness.

