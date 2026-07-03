# Validation and Rollout Plan

This file defines the gates for implementing the runtime fixes, demo fixes,
rename, and 3D menu. Run the smallest relevant gate after each batch and the
full gate before proposing final changes.

## Baseline Commands

Fast checks:

```sh
build/src/tools/viper/viper check examples/games/game3d-showcase --diagnostic-format=json
build/src/tools/viper/viper check examples/games/3dscene --diagnostic-format=json
```

After directory rename, replace `game3d-showcase` with `ridgebound`.

Smoke probes:

```sh
build/src/tools/viper/viper run examples/games/game3d-showcase/smoke_probe.zia
build/src/tools/viper/viper run examples/games/3dscene/smoke_probe.zia
```

Targeted CTest:

```sh
ctest --test-dir build -R '^(zia_smoke_game3d_showcase|zia_smoke_game3d_showcase_software|zia_smoke_game3d_showcase_tree_fallback_materials|zia_smoke_game3d_showcase_tree_materials)$' --output-on-failure
ctest --test-dir build -R '^g3d_test_game3d_character_controller_probe$' --output-on-failure
```

After rename, update CTest names according to the final test labels.

## Runtime Validation

### Character Controller

Run/build relevant targets:

```sh
cmake --build build --target test_rt_game3d test_rt_physics3d -j 6
ctest --test-dir build -R '^(test_rt_game3d|test_rt_physics3d|g3d_test_game3d_character_controller_probe)$' --output-on-failure
```

Required new assertions:

- No-input airborne character moves downward.
- No-input airborne character becomes grounded.
- Jump moves upward.
- W + Shift planar movement remains full horizontal intent before sprint
  multiplier.

### FOV API Additions

If `HorizontalFov` property is added:

```sh
cmake --build build --target test_rt_canvas3d test_graphics3d_abi_surface -j 6
ctest --test-dir build -R '^(test_rt_canvas3d|test_graphics3d_abi_surface)$' --output-on-failure
```

Required assertions:

- Runtime ABI surface includes getter/setter.
- Horizontal-to-vertical math round trips.
- Orthographic camera ignores horizontal FOV set.
- Docs snippets compile if added.

### Vignette API Additions

If a public vignette preset/helper is added:

```sh
cmake --build build --target test_rt_canvas3d test_rt_postfx3d_snapshot test_graphics3d_abi_surface -j 6
ctest --test-dir build -R '^(test_rt_canvas3d|test_rt_postfx3d_snapshot|test_graphics3d_abi_surface)$' --output-on-failure
```

Required assertions:

- Chain snapshot preserves the new vignette helper effect.
- Defaults match docs.
- Existing `AddVignette` behavior remains unchanged.

## Demo Validation

### Ridgebound Startup State Probe

Enhance the smoke probe or add a dedicated probe:

Checks:

- Build world.
- Run at least 120 fixed-ish frames with no input.
- Read player feet Y and camera Y.
- Fail if player feet Y rises above initial feet Y by more than tolerance.
- Fail if camera Y rises unbounded.
- Fail if diagnostics summary is nonempty.

Suggested tolerance:

- Player feet Y may shift slightly due terrain collision resolution.
- Allow `+0.25` max above initial feet Y for early settling.
- Require no monotonic runaway after several frames.

### Ridgebound Visual Probe

Keep existing structural checks, then add:

- Corner luminance guard:
  - Corners should not be fully black.
  - Threshold should be loose enough for intended vignette.
- Title/menu frame guard after menu is added:
  - Detect title color/text region.
  - Detect selected pylon/light region.
  - Detect no HUD/minimap leakage on title screen unless intentionally drawn.

### 3DScene Visual Probe

Add corner-specific checks:

- Capture final frame.
- Compute average luminance in:
  - Top-left corner block.
  - Top-right corner block.
  - Bottom-left corner block.
  - Bottom-right corner block.
  - Center block.
- Fail if all corners are near black while center is bright.

Keep thresholds backend-tolerant:

- Use ratios rather than exact colors.
- Avoid exact pixel positions over animated particles.

## Rename Validation

Before rename:

```sh
rg -n "game3d-showcase|game3d_showcase|Game3DShowcase|Showcase[A-Z]|Game3D Open World Showcase" examples/games/game3d-showcase
```

After symbol rename:

```sh
rg -n "Game3DShowcase|Showcase[A-Z]|Game3D Open World Showcase" examples/games/game3d-showcase
```

After path rename:

```sh
rg -n "examples/games/game3d-showcase|game3d-showcase|Game3D Open World Showcase|Game3DShowcase" .
```

Expected remaining matches:

- Historical release notes.
- Separate `examples/3d/game3d_showcase`.
- Intentional "formerly known as" compatibility text, if kept.

Build scripts:

```sh
./scripts/lint_platform_policy.sh
```

Run demo build script for the host platform:

```sh
VIPER_SKIP_TESTS=1 ./scripts/build_viper_mac.sh
./scripts/build_demos_mac.sh
```

Use Linux/Windows equivalents on those platforms or CI.

## Menu Validation

Manual checks:

- Launch shows title/menu first.
- Mouse is not captured at title.
- Keyboard navigation changes selected item.
- Start transitions to gameplay.
- Pause releases mouse and shows pause menu.
- Resume recaptures only when gameplay resumes or user clicks.
- Options toggles affect runtime state.
- Controls screen is readable.

Screenshot checks:

- 1280x720 title frame.
- 960x540 title frame.
- 1280x720 gameplay frame.
- 960x540 gameplay frame.

Look for:

- No overlapping text.
- No black-corner oval.
- No blank canvas.
- No HUD/minimap on title unless intended.
- Selected menu item clearly visible.

## Full Local Validation Before Final Proposal

Run after implementation reaches a final state:

```sh
git diff --check
build/src/tools/viper/viper check examples/games/ridgebound --diagnostic-format=json
build/src/tools/viper/viper check examples/games/3dscene --diagnostic-format=json
build/src/tools/viper/viper run examples/games/ridgebound/smoke_probe.zia
build/src/tools/viper/viper run examples/games/3dscene/smoke_probe.zia
ctest --test-dir build -R '^(test_rt_game3d|test_rt_physics3d|test_rt_canvas3d|test_graphics3d_abi_surface)$' --output-on-failure
ctest --test-dir build -R '^(zia_smoke_ridgebound|zia_smoke_ridgebound_software|zia_smoke_ridgebound_tree_fallback_materials|zia_smoke_ridgebound_tree_materials)$' --output-on-failure
./scripts/lint_platform_policy.sh
```

Then run the required full build script for the host platform:

```sh
VIPER_SKIP_CLEAN=1 ./scripts/build_viper_mac.sh
```

Use Linux/Windows scripts where appropriate.

## CI and Cross-Platform Notes

Runtime fixes:

- Must compile under macOS, Linux, Windows.
- Avoid raw platform checks.
- Public runtime additions need stubs in graphics-disabled builds if applicable.

Demo path rename:

- Update `.cmd` and shell scripts together.
- Avoid symlinks.
- Use portable relative paths.

Graphics differences:

- Corner luminance tests must be broad enough for Metal/D3D11/OpenGL/software.
- Do not use exact screenshots for Ridgebound until art/camera/menu is frozen.

## ADR Checklist

Required ADRs if implemented:

- `Camera3D.HorizontalFov` property or any public camera API addition.
- `Input3D.PlanarMoveAxis` or any public input API addition.
- `PostFX3D.AddSubtleVignette` or any public post-FX API addition.

Not required:

- Character gravity sign bug fix with no public API change.
- Internal Game3D planar movement helper.
- Demo rename.
- Demo menu implementation.
- Demo FOV/vignette tuning.

## Rollout Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Gravity fix breaks existing probes that assumed old sign | Low | Medium | Update tests to assert physically correct behavior; inspect first-person probe |
| Planar movement changes sprint feel | Medium | Low | Manual play and test W+Shift speed |
| Directory rename misses docs/scripts | High | Medium | Use `rg` checklist and build demo scripts |
| Vignette visual thresholds flake across backends | Medium | Medium | Use loose ratio checks and keep structural smoke checks |
| 3D menu slows startup | Medium | Medium | Reuse existing world/assets; avoid new heavy procedural work per frame |
| Menu input conflicts with mouse capture | Medium | High | State-gate capture/release; test title, start, pause, resume |

## Definition of Done

The entire polish effort is done when:

- Ridgebound starts at a title/menu screen.
- Start enters gameplay without player flyaway.
- The camera and post-FX no longer read as bad fisheye in default gameplay.
- 3DScene no longer has black oval corners.
- Runtime controller tests catch the gravity sign regression.
- Demo smoke tests catch player runaway and severe corner darkening.
- The demo is renamed consistently to Ridgebound in active docs/scripts/tests.
- No external dependencies or platform-specific hacks are introduced.

