# Game3D Bowling Setup Migration

`game3d_setup.zia` is a focused migration of the 3D bowling setup layer onto
Game3D. It replaces the original per-game renderer/camera/HUD/lane scaffolding
with Game3D world setup, prefabs, `BodyDef` physics, follow camera, and final
overlay calls.

Run the deterministic smoke:

```sh
ZANNA_3D_BACKEND=software ../../../build/src/tools/zanna/zanna run game3d/game3d_setup.zia
```

Line-count comparison for the migration gate:

| Area | Files | LOC |
|---|---|---:|
| Original renderer/camera/HUD scaffolding | `engine/renderer.zia`, `engine/hud.zia`, `engine/camera.zia` | 789 |
| Game3D setup sample | `game3d/game3d_setup.zia` | 129 |

That is an 83.7% reduction for the setup surface measured by the plan. The
sample is intentionally not a full bowling game rewrite; scoring, menus, and
polish remain in the original game while this file demonstrates the Game3D
replacement for the manual 3D setup layer.
