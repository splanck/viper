# Phase 3 - Scene Data Foundation

## 1. Summary and Objective

Provide the runtime data model that the visual scene editor can safely read and write. The authoritative implementation plan is `misc/plans/game/scene-system.md`; this file records the IDE-facing contract and the runtime gates ViperIDE must verify before depending on scene editing.

The IDE must consume `Viper.Game.Scene`, not extend the old load-only `Viper.Game.LevelData` path.

## 2. Scope

In the runtime scene plan:

- `Viper.Game.Scene` using the current `LoadJson` / `LoadFile` /
  `SaveFile` / `ToJson` surface.
- Optional compatibility aliases `Load`, `FromString`, and `Save` only if the
  runtime plan chooses to add them.
- Canonical JSON round-trip.
- Scene-owned normalized data.
- Optional `BuildTilemap()` render copy. The returned `Tilemap` is not the save
  source of truth.
- Live indexed object, property, layer, and tile mutators that update
  scene-owned data.
- `DiagnosticRecords()` structured diagnostics plus compatibility
  `Diagnostics()` strings and `LastError()`.
- Zia and BASIC compile smoke coverage.

In ViperIDE during this phase:

- No UI work beyond adapting file-kind recognition if the final extension decision changes.

Out:

- Visual scene editor UI.
- Scene viewport widget.
- Game example migration beyond the runtime plan's dogfood target.

## 3. Runtime Contract to Verify Before IDE Integration

The current tree already exposes the intended `Viper.Game.Scene` surface, including canonical v1 `.scene` JSON, non-trapping diagnostics, typed properties, atomic save, asset descriptors, and `BuildTilemap()`. Treat the sections below as integration gates: Phase 5 should verify the runtime behavior in the target build configuration before using it, and should not reintroduce legacy `LevelData` or `Tilemap` ownership shortcuts.

### 3.1 Scene, Not LevelData

The ViperIDE roadmap must not ask for `LevelData` writers or mutators. `LevelData` may remain as a legacy loader or compatibility wrapper later. New editor work depends on:

- `Viper.Game.Scene.LoadFile`
- `Viper.Game.Scene.LoadJson`
- `Scene.SaveFile`
- `Scene.ToJson`
- `Scene.Width`, `Scene.Height`, `Scene.TileWidth`, and `Scene.TileHeight`
- scene-owned layer/tile mutators
- indexed scene-owned object mutators
- typed scene and object property getters/setters
- `Scene.DiagnosticRecords`, `Scene.Diagnostics`, `Scene.LastError`, and
  `Scene.HasErrors`

If `Load`, `FromString`, or `Save` aliases are added later, ViperIDE may use
them only after they are registered and covered by smoke tests.

### 3.2 Tile Edit Ownership

The scene-system plan correctly states that `Tilemap` is not the serialization source of truth. Before Phase 5 tile tools start, Phase 3 must define and implement one of:

- Scene tile/layer APIs such as `Scene.SetTile(layer, x, y, tileId)`, `GetTile`, `FillRect`, `SetLayerVisible`, `SetLayerName`, or
- `Scene.BuildTilemap()` as a render copy plus clear docs that edits to the copy are not saveable, or
- A live Tilemap extension that preserves scene-owned asset metadata and round-trips through `Scene.SaveFile`.

Do not ship a brush tool that edits only `Viper.Graphics.Tilemap` and then claims save support.

### 3.3 Tile ID Semantics

Use the canonical convention from `misc/plans/game/scene-system.md`:

- `0` means empty/not drawn.
- `N > 0` means tileset frame `N - 1`.

This convention must be documented in runtime docs and used by the palette,
brush, renderer, serializer, and tests.

### 3.4 Extensions and Source Format

Use `.scene` as the canonical scene source extension before the IDE UI ships:

- `.scene`
- `.level` and `.json` may remain open/import compatibility formats.

Examples and Save As should prefer `.scene`. `DocumentKind` in Phase 0 must
match this decision.

### 3.5 Asset Paths

The scene loader should expose structured asset descriptors but should not silently guess IDE asset roots. `Scene.AssetPaths()` is acceptable only as a compatibility helper. The IDE needs enough data to resolve:

- project-root relative assets
- mounted assets via `Viper.IO.Assets`
- missing assets
- packaged assets later

The actual resolver binding is `Viper.Assets.Resolver.Resolve`; mounted asset loading still goes through `Viper.IO.Assets`.

### 3.6 Diagnostics

`Scene.LoadFile(path)` must not trap on ordinary bad scene files. Before Phase 5, expose:

- `Scene.HasErrors() -> bool`
- `Scene.LastError() -> str`
- `Scene.DiagnosticRecords() -> structured diagnostic records with
  code/severity/path/line/column/message`
- `Scene.Diagnostics() -> compatibility strings derived from diagnostics`

The editor must be able to show a useful load error without crashing or hiding the source.

## 4. IDE Acceptance Criteria

Phase 5 may begin only when:

- Scene load/save round-trip tests pass for canonical v1 and current legacy input.
- Scene mutators needed by the first editing tool exist.
- Tile ID convention is final.
- Scene diagnostics are available.
- The schema fixture matrix in `misc/plans/game/scene-system.md` passes.
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
- Legacy unversioned scene input normalizes to canonical v1 output.

## 6. Manual Verification for IDE Consumers

After runtime work lands, create a small sample scene and verify from a Zia probe:

- Load scene.
- Read tilemap dimensions.
- Mutate one tile through scene-owned APIs.
- Add one object.
- Save to a new file.
- Reload and confirm both edits persist.
