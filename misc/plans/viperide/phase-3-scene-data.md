# Phase 3 - Scene Data Foundation

## 1. Summary and Objective

Provide the runtime data model that the visual scene editor can safely read and write. The authoritative implementation plan is `misc/plans/game/scene-system.md`; this file records the IDE-facing contract and the corrections required before ViperIDE depends on it.

The IDE must consume `Viper.Game.Scene`, not extend the old load-only `Viper.Game.LevelData` path.

## 2. Scope

In the runtime scene plan:

- `Viper.Game.Scene` and `Viper.Game.SceneObject`.
- Load/from-string and save/to-json.
- Canonical JSON round-trip.
- Scene-owned normalized data.
- Cached read-only `Viper.Graphics.Tilemap` view.
- Live object, property, layer, and tile mutators that update scene-owned data.
- Structured diagnostics or last-error reporting.
- Zia and BASIC compile smoke coverage.

In ViperIDE during this phase:

- No UI work beyond adapting file-kind recognition if the final extension decision changes.

Out:

- Visual scene editor UI.
- Scene viewport widget.
- Game example migration beyond the runtime plan's dogfood target.

## 3. Required Corrections Before IDE Integration

### 3.1 Scene, Not LevelData

The ViperIDE roadmap must not ask for `LevelData` writers or mutators. `LevelData` may remain as a legacy loader or compatibility wrapper later. New editor work depends on:

- `Viper.Game.Scene.Load`
- `Viper.Game.Scene.FromString`
- `Scene.Save`
- `Scene.ToJson`
- `Scene.Tilemap`
- `Scene.ObjectCount`
- `Scene.Object(i)`
- scene-owned mutators

### 3.2 Tile Edit Ownership

The scene-system plan correctly states that the cached `Tilemap` is not the serialization source of truth. Before Phase 5 tile tools start, Phase 3 must define and implement one of:

- Scene tile/layer APIs such as `Scene.SetTile(layer, x, y, tileId)`, `GetTile`, `FillRect`, `SetLayerVisible`, `SetLayerName`, or
- A Tilemap extension that preserves scene-owned asset metadata and round-trips through `Scene.Save`.

Do not ship a brush tool that edits only `Viper.Graphics.Tilemap` and then claims save support.

### 3.3 Tile ID Semantics

Resolve the mismatch between:

- Scene schema rule: `0` empty, positive IDs are 1-based tileset indices.
- Tilemap docs: tiles are numbered starting at `0`, while `0` is also described as empty/transparent.

The chosen convention must be documented in runtime docs and used by the palette, brush, renderer, serializer, and tests.

### 3.4 Extensions and Source Format

Pick the scene source extensions before the IDE UI ships:

- `.scene`
- `.level`
- `.json`

The plan may support more than one, but one canonical extension should be used by examples and Save As. `DocumentKind` in Phase 0 must match this decision.

### 3.5 Asset Paths

The scene loader should expose asset descriptors but should not silently guess IDE asset roots. The IDE needs enough data to resolve:

- project-root relative assets
- mounted assets via `Viper.IO.Assets`
- missing assets
- packaged assets later

The actual runtime binding is `Viper.IO.Assets.Load`, not bare `Assets.Load`.

### 3.6 Diagnostics

`Scene.Load(path) -> null` is enough for a game fallback but not for an editor. Before Phase 5, expose at least one of:

- `Viper.Game.Scene.LastError() -> str`
- `Scene.LoadWithDiagnostics(path) -> result object`
- structured scene diagnostic records with path/line/column/message

The editor must be able to show a useful load error without crashing or hiding the source.

## 4. IDE Acceptance Criteria

Phase 5 may begin only when:

- Scene load/save round-trip tests pass.
- Scene mutators needed by the first editing tool exist.
- Tile ID convention is final.
- Scene diagnostics are available.
- Asset descriptor rules are documented.
- Zia and BASIC smoke tests compile scene API usage.

## 5. Tests Owned by the Runtime Plan

- Valid scene load.
- Malformed JSON.
- Missing file.
- Invalid dimensions.
- Tile-count mismatch.
- Tile ID edge cases.
- Object scalar getters and setters.
- Add/remove/reorder object.
- Tile/layer mutator persistence.
- Save through temp/rename or equivalent safe write behavior.
- Canonical round-trip.
- Last-error/diagnostic content.

## 6. Manual Verification for IDE Consumers

After runtime work lands, create a small sample scene and verify from a Zia probe:

- Load scene.
- Read tilemap dimensions.
- Mutate one tile through scene-owned APIs.
- Add one object.
- Save to a new file.
- Reload and confirm both edits persist.
