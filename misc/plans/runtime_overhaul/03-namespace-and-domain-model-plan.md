# Namespace And Domain Model Plan

## Problem

The runtime API has grown across several layers without a visible ownership
model. Users see multiple plausible places for the same concept:

- `Viper.Input.Keyboard` and `Viper.Game3D.Keys`
- `Viper.Graphics`, `Viper.Graphics2D`, `Viper.Game`, and `Viper.Game2D`
- `Viper.Graphics3D` and `Viper.Game3D`
- `GLTF`/`FBX` loaders, `SceneAsset`, and `Game3D.Assets3D`

The fix is not to flatten everything. The fix is to define the layering.

## Target Namespace Ownership

| Namespace | Owns | Does Not Own |
|---|---|---|
| `Viper.Input` | Input devices, key/button/gamepad constants, input mappings | Game-specific action semantics |
| `Viper.Graphics` | Shared graphics primitives, immediate 2D canvas, pixels, colors, fonts, 2D textures | Game state, 3D retained scene |
| `Viper.Graphics2D` | Retained 2D scene graph, tilemaps, 2D render systems | Gameplay controllers |
| `Viper.Game` | Game loop helpers, 2D gameplay systems, UI overlays | Low-level graphics or input constants |
| `Viper.Game2D` | 2D document/level/scene persistence | Renderer internals |
| `Viper.Graphics3D` | 3D rendering, scene graph, assets, materials, physics, animation primitives | Gameplay entities/controllers |
| `Viper.Game3D` | High-level world/entity/controller/prefab conveniences | Duplicated key constants or file-format loaders |
| `Viper.GUI` | Desktop widgets, app chrome, code editor widgets | Game HUD widgets |

## Input Decision

Decision: key code constants move to one canonical enum-like surface:

```text
Viper.Input.Key.Unknown
Viper.Input.Key.A
Viper.Input.Key.F1
Viper.Input.Key.LeftShift
```

`Viper.Input.Keyboard` keeps state/query behavior:

```text
Keyboard.IsDown(Key.A)
Keyboard.WasPressed(Key.Space)
Keyboard.GetText()
Keyboard.EnableTextInput()
```

`Viper.Game3D.Keys` is removed from the public canonical API. Game3D examples use
`Viper.Input.Key`.

Implementation notes:

- Add `Viper.Input.Key` first.
- Update Game3D runtime code and docs to consume it.
- Remove or hide `Game3D.Keys`.
- Consider moving key constants out of `Keyboard` after call sites are migrated,
  or keep `Keyboard.KeyA` only as a temporary pre-alpha bridge.

## 3D Asset Decision

Decision: `Viper.Graphics3D.SceneAsset` is the canonical loaded 3D asset type.
Format-specific loaders are import helpers. `Viper.Game3D.Assets3D` is a high
level convenience layer that converts canonical assets into world/entity/prefab
objects.

Target shape:

```text
Viper.Graphics3D.SceneAsset.Load(path)
Viper.Graphics3D.SceneAsset.LoadAsset(assetPath)
Viper.Graphics3D.SceneAsset.FromGltf(path)
Viper.Graphics3D.SceneAsset.FromFbx(path)
Viper.Graphics3D.SceneAsset.LoadAnimation(path)

Viper.Game3D.Prefab.Load(path)
Viper.Game3D.Prefab.FromSceneAsset(asset)
Viper.Game3D.World3D.Spawn(prefab)
```

Rename pressure:

- `Assets3D.LoadEntity` is too high-level and hides whether it loads a model,
  scene asset, prefab, or runtime entity.
- `LoadTemplate` should become `LoadPrefab` if the concept is a reusable entity
  template.
- File-format classes (`GLTF`, `FBX`) can stay as expert entry points, but docs
  should route ordinary users through `SceneAsset`.

## 2D Boundary Decision

Decision:

- `Viper.Graphics.Canvas` remains the immediate 2D drawing surface.
- `Viper.Graphics.Pixels`, `Color`, `Texture2D`, fonts, and image codecs remain
  under `Viper.Graphics`.
- `Viper.Graphics2D` owns retained 2D scene graph, tilemap, renderer, material,
  shader, and batching concepts.
- `Viper.Game` owns gameplay helpers such as timers, tweens, particles,
  pathfinding, physics helpers, and game UI.
- `Viper.Game2D` owns scene/level document formats and game-oriented persistence.

Work item: make docs show this layering explicitly and avoid cross-linking that
implies interchangeable ownership.

## GUI/Game UI Boundary

Decision:

- `Viper.GUI` is desktop/application UI.
- `Viper.Game.UI` is in-canvas HUD/game UI.
- Do not duplicate widget names unless the interaction model is genuinely
  different.

Work item:

- Review `Viper.Game.UI.HudTextInput`, `HudDropdown`, `GameButton`, `Panel`,
  `MenuList`, and `Modal` against GUI equivalents.
- Keep game UI if it draws into canvas/game loop and uses game input.
- Move shared logic into internal helpers, not public duplicated APIs.

## Domain Ownership Audit

Add a script or runtime surface audit that rejects:

- `Viper.Game3D.Keys.*`
- new input constants outside `Viper.Input.Key`
- new 3D file-format loader names outside `Viper.Graphics3D`
- new public `*3D` rendering primitives under `Viper.Game3D`
- new public game controllers under `Viper.Graphics3D`

Allowlist deliberate bridges such as `Game3D.World3D.Spawn`.

## Migration Slices

1. Add `Viper.Input.Key`.
2. Update docs/examples/tests for input keys.
3. Hide or remove `Viper.Game3D.Keys`.
4. Rename `Assets3D.LoadTemplate*` toward `Prefab`.
5. Route ordinary 3D asset docs through `SceneAsset`.
6. Add namespace ownership audit.
7. Split large docs pages by abstraction layer.

## Acceptance Criteria

- A new user can answer "where do I get a key code?" from namespace names alone.
- 3D asset docs have one primary loading path.
- Game3D no longer carries duplicated input constants.
- Public API diff shows fewer duplicated concept names, not more aliases.

