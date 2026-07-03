# Game3D-Showcase Polish Plan Bundle

Status: implemented. The runtime controller fix, ADR, demo rename, menu
front-end, FOV/post-FX retuning, docs, build scripts, CMake smoke targets, and
validation work from this plan bundle have been applied. The original plan files
remain as the investigation and rollout record for the former
`examples/games/game3d-showcase` demo.

This bundle captures the runtime fixes, demo bug fixes, rename, menu redesign,
and validation work used to turn the former `examples/games/game3d-showcase`
runtime showcase into the playable `examples/games/ridgebound` sample.

Recommended game name: **Ridgebound**.

Recommended public title: **Ridgebound: The Five Beacons**.

The name is intentionally tied to the existing objective copy (`Ridge beacons`)
and the terrain/forest/beacon fantasy already in the demo. It avoids the
generic "showcase" label while staying close enough to the current content that
the rename does not require a new story, world, or art pass.

## File Map

- `00-investigation-summary.md` - Current findings and root causes for the
  player flyaway, Game3D-Showcase edge artifact, and 3DScene corner artifact.
- `01-runtime-fixes-and-api-additions.md` - Runtime bug fixes and optional public
  API additions, with ADR and compatibility notes.
- `02-demo-bugfix-and-polish-plan.md` - Demo-level changes for Ridgebound and
  3DScene after the runtime fixes land.
- `03-rename-to-ridgebound.md` - Full rename/migration plan for paths, classes,
  tests, docs, scripts, screenshots, and release notes.
- `04-3d-menu-system-plan.md` - Detailed 3D-looking title/menu system plan for
  Ridgebound.
- `05-validation-and-rollout.md` - Build, test, smoke, visual, and review gates.

## Scope

This plan covers:

- Fixing the player launching upward at startup.
- Reducing perspective/vignette edge artifacts in Game3D-Showcase.
- Fixing the severe 3DScene dark-corner/fisheye impression.
- Adding runtime affordances where the demo exposed weak APIs.
- Renaming the playable demo to a game-like title.
- Adding a 3D-looking title/menu front end without adding dependencies.
- Expanding smoke tests so these regressions are caught automatically.

This plan does not cover:

- Adding external models, shaders, libraries, or downloaded assets.
- Reworking the whole Game3D API.
- Replacing the existing terrain/water/forest/beacon gameplay loop.
- Renaming the separate `examples/3d/game3d_showcase/` integration probe unless
  a later pass explicitly decides to do so.

## High-Level Execution Order

1. Runtime correctness first: fix `CharacterController3D` gravity and add tests.
2. Runtime ergonomics second: decide which API additions merit ADRs and stage
   them separately from the bug fix.
3. Demo bug fixes: stabilize player spawn/feet semantics, retune FOV/post-FX,
   and fix 3DScene FOV/vignette.
4. Rename the demo in user-facing code and docs, then directory/build/test paths.
5. Add the 3D-looking menu system behind a small game-state front end.
6. Run the validation matrix in `05-validation-and-rollout.md`.

## Completed Validation

- `VIPER_SKIP_CLEAN=1 ./scripts/build_viper_mac.sh`
  - Full incremental build completed.
  - CTest passed: 1787 passed, 0 failed.
  - Platform policy lint passed.
  - Runtime surface audit passed.
  - Cross-platform smoke slice passed.
  - Install completed into `build/install`.
- Focused checks completed before the full run:
  - `build/install/bin/viper check examples/games/ridgebound --diagnostic-format=json`
  - `build/install/bin/viper check examples/games/3dscene --diagnostic-format=json`
  - `build/install/bin/viper build examples/games/ridgebound -o /tmp/ridgebound.il`
  - `build/install/bin/viper run examples/games/ridgebound/smoke_probe.zia`
  - `build/install/bin/viper run examples/games/3dscene/smoke_probe.zia`
  - `ctest --test-dir build -R '^(test_rt_game3d|zia_smoke_3dscene|test_tools_asset_compiler)$' --output-on-failure`
  - `ctest --test-dir build -R '^(zia_smoke_ridgebound|zia_smoke_ridgebound_software|zia_smoke_ridgebound_tree_fallback_materials|zia_smoke_ridgebound_tree_materials)$' --output-on-failure`

## Baseline Facts

- These checks passed during the original investigation before the implementation:
  - `build/src/tools/viper/viper check examples/games/game3d-showcase --diagnostic-format=json`
  - `build/src/tools/viper/viper check examples/games/3dscene --diagnostic-format=json`
  - `build/src/tools/viper/viper run examples/games/game3d-showcase/smoke_probe.zia`
  - `build/src/tools/viper/viper run examples/games/3dscene/smoke_probe.zia`
- Existing smoke probes only validate gross rendering success. They do not catch
  player Y runaway or severe corner darkening.
- The worktree had unrelated bytecode/IL files modified before this plan bundle
  was created. The plan work should not revert or modify those files.

## Repo Rules That Matter

- Use the build scripts for full builds.
- Runtime public surface additions require ADR coverage when they alter the C ABI
  or public runtime API.
- Keep zero external dependencies.
- Keep cross-platform behavior for macOS, Windows, and Linux.
- For demo path/script changes, run the platform-policy lint when the touched
  files include build scripts or cross-platform path logic.
