# Viper JSON Scene Documents (`Viper.Game.Scene`)

Status: revised 2026-05-22 to match the current Viper tree and close the gaps
found during review, and reconciled against source the same day: ownership
guidance corrected to the signature-derived convention, resource-limit hardening
sequenced into Phase 1, reference paths made absolute, and
`Viper.Game.SceneManager` disambiguated.

## 1. Objective

Provide a JSON-backed scene document that can be loaded, edited, saved, and
used from Zia and BASIC without reflection, source generation, or game-specific
JSON parsing.

This plan is no longer a greenfield design. The current project already exposes
`Viper.Game.Scene` through:

- `src/runtime/game/rt_scene_editor.h`
- `src/runtime/game/rt_scene_editor.cpp`
- `RTCLS_GameScene`
- `RT_GAME_SCENE_CLASS_ID`
- `rt_game_scene_*` C symbols

The plan below keeps that public direction and defines the missing work needed
to make JSON scenes safe for ViperIDE and useful for game code.

Note: `Viper.Game.Scene` (this scene *document* type) is distinct from
`Viper.Game.SceneManager` (the runtime multi-scene/transition manager already in
`runtime.def`). They share the `Viper.Game` prefix but are unrelated classes;
this plan covers only the document type.

## 2. Non-Goals

- Do not auto-instantiate arbitrary user Zia/BASIC classes by string name.
- Do not extend `Viper.Game.LevelData` with editor mutators.
- Do not make `Viper.Graphics.Tilemap` the scene serialization source of truth.
- Do not silently resolve asset paths during scene loading.
- Do not add `Viper.Game.SceneObject` until live-handle ownership and mutation
  semantics are fully specified and tested.

Game code should load a scene, inspect typed scene/object data, and call a
game-owned spawn adapter:

```zia
var scene = Viper.Game.Scene.LoadFile("levels/descent.scene");
if scene == null || scene.HasErrors() {
    return;
}

var i = 0;
while i < scene.ObjectCount() {
    var type = scene.ObjectType(i);
    if type == "enemy" {
        spawnEnemy(
            scene.ObjectId(i),
            scene.ObjectX(i),
            scene.ObjectY(i),
            scene.ObjectGetInt(i, "hp", 1));
    }
    i = i + 1;
}
```

## 3. Current Baseline

The landed API is an editable scene-document API:

- Construction and I/O:
  - `Viper.Game.Scene.New(width, height, tileWidth, tileHeight)`
  - `Viper.Game.Scene.LoadJson(text)`
  - `Viper.Game.Scene.LoadFile(path)`
  - `scene.ToJson()`
  - `scene.SaveFile(path)`
  - `scene.LastError()`
  - `scene.Diagnostics()`
- Dimensions:
  - `scene.Width`
  - `scene.Height`
  - `scene.TileWidth`
  - `scene.TileHeight`
- Layers and tiles:
  - `AddLayer`, `LayerCount`, `LayerName`, `SetLayerName`
  - `LayerVisible`, `SetLayerVisible`
  - `MoveLayer`, `RemoveLayer`
  - `GetTile`, `SetTile`, `FillTiles`
  - `SetLayerAsset`, `LayerAsset`
- Objects:
  - `AddObject`, `ObjectCount`, `RemoveObject`
  - `ObjectType`, `ObjectId`, `ObjectX`, `ObjectY`
  - `SetObjectPosition`
  - `SetObjectProperty`, `GetObjectProperty`, `DeleteObjectProperty`
- Scene properties and assets:
  - `SetProperty`, `GetProperty`, `DeleteProperty`
  - `AssetPaths`

Known baseline gaps:

- The implementation reads and writes a legacy unversioned top-level JSON shape.
- `LoadJson` calls `rt_json_parse` directly; malformed JSON can trap instead of
  returning diagnostics.
- `SaveFile` writes directly to the target with truncation.
- Object custom properties are string-only and are not persisted by `ToJson`.
- There is no typed property access for scene or object data.
- There is no `Tilemap` view/copy API.
- `AssetPaths` is heuristic and loses source context.
- Zia coverage is a narrow `Scene.New` smoke; BASIC coverage is missing.

## 4. Canonical Public API

Keep the current API names. They are already in docs and smoke tests.

Add compatibility aliases only if needed by examples:

- `Scene.Load(path)` -> `Scene.LoadFile(path)`
- `Scene.FromString(text)` -> `Scene.LoadJson(text)`
- `scene.Save(path)` -> `scene.SaveFile(path)`

Do not rename the implementation files to `rt_game_scene.*` unless there is a
separate cleanup pass. The existing `rt_scene_editor.*` files are acceptable
because the exported symbols already use the collision-safe `rt_game_scene_*`
prefix. Do not create `src/runtime/game/rt_scene.h`; it conflicts with
`src/runtime/graphics/rt_scene.h` under flat include lookup.

### Required New Scene Methods

Diagnostics and validity:

- `HasErrors() -> i1`
- `ClearDiagnostics() -> void`
- `DiagnosticRecords() -> seq<map>`

`Diagnostics()` remains a compatibility method returning `seq<str>`. New editor
code should use `DiagnosticRecords()`.

Typed scene properties:

- `GetInt(key, default) -> i64`
- `GetStr(key, default) -> str`
- `GetFloat(key, default) -> f64`
- `GetBool(key, default) -> i1`
- `Has(key) -> i1`
- `SetInt(key, value) -> void`
- `SetStr(key, value) -> void`
- `SetFloat(key, value) -> void`
- `SetBool(key, value) -> void`
- `Remove(key) -> void`

Scene property keys address entries in the top-level `properties` object. Do not
interpret `.` or `/` as nested paths in v1; nested editor data belongs in named
rich sections with explicit APIs.

Typed getters should not coerce strings into numbers or bools. Missing keys,
`null`, and incompatible scalar kinds return the caller-provided default.
Integral JSON numbers may satisfy `GetInt`; any JSON number may satisfy
`GetFloat`.

Typed object properties by index:

- `ObjectGetInt(index, key, default) -> i64`
- `ObjectGetStr(index, key, default) -> str`
- `ObjectGetFloat(index, key, default) -> f64`
- `ObjectGetBool(index, key, default) -> i1`
- `ObjectHas(index, key) -> i1`
- `ObjectKeys(index) -> seq<str>`
- `ObjectSetInt(index, key, value) -> void`
- `ObjectSetStr(index, key, value) -> void`
- `ObjectSetFloat(index, key, value) -> void`
- `ObjectSetBool(index, key, value) -> void`
- `ObjectRemove(index, key) -> void`

Object property keys follow the same scalar typing and no-nested-path rules as
scene property keys.

Object utilities:

- `CountOfType(type) -> i64`
- `ObjectOfType(type, n) -> i64` returning object index or `-1`
- `FindObject(id) -> i64` returning object index or `-1`
- `MoveObject(from, to) -> void`

Asset descriptors:

- `AssetDescriptors() -> seq<map>` with entries containing:
  - `path`
  - `kind`
  - `owner`
  - `layer`
  - `object`
  - `key`

`AssetPaths()` remains as a compatibility helper returning unique strings, but
it should be derived from explicit asset fields/descriptors rather than
substring guessing over arbitrary property names.

Tilemap render copy:

- `BuildTilemap() -> obj<Viper.Graphics.Tilemap>`

`BuildTilemap()` returns a new render/collision copy built from scene-owned tile
layers. It is not the serialization source of truth. Mutating the returned
`Tilemap` must not be documented as saveable. If a future live view is needed,
that is a separate design with invalidation, write-through, and ownership tests.

### API Delta Table

| Surface | Current status | Required action | Required tests |
|---|---|---|---|
| `New(width,height,tileWidth,tileHeight)` | implemented | keep | Zia/BASIC construction smoke |
| `LoadJson(text)` | implemented but can trap on malformed JSON | make non-trapping for user input | malformed, empty, non-object, legacy, v1 |
| `LoadFile(path)` | implemented but returns fallback scene and can inherit parse traps | make non-trapping and diagnostic-rich | missing, unreadable, malformed, v1 |
| `ToJson()` | implemented legacy shape | emit canonical v1 and preserve rich sections | golden round-trip fixtures |
| `SaveFile(path)` | implemented direct truncate write | replace with atomic same-directory temp/rename | write failure preserves target |
| `LastError()` | implemented string | keep as newest diagnostic summary | missing/malformed messages |
| `Diagnostics()` | implemented `seq<str>` | keep compatibility string list | legacy callers still compile |
| `DiagnosticRecords()` | missing | add `seq<map>` structured diagnostics | record fields and order |
| `HasErrors()` / `ClearDiagnostics()` | missing | add | invalid scene and clear behavior |
| typed scene getters/setters | missing | add `Get*`, `Set*`, `Has`, `Remove` | type/default/persistence |
| typed object getters/setters | missing | add indexed `ObjectGet*`, `ObjectSet*`, `ObjectKeys` | type/default/persistence |
| object search/reorder | partially missing | add `CountOfType`, `ObjectOfType`, `FindObject`, `MoveObject` | reorder/search persistence |
| `AssetPaths()` | implemented heuristic | keep, derive from descriptors | compatibility and dedupe |
| `AssetDescriptors()` | missing | add structured descriptor records | layer/object/key context |
| `BuildTilemap()` | missing | add render/collision copy | layer data, mutation isolation |

## 5. Internal Data Model

Implementation should normalize all supported input schemas into scene-owned C++
state before any public reads.

Recommended shape:

```cpp
enum class SceneScalarKind {
    Null,
    Bool,
    Int,
    Float,
    String,
};

struct SceneScalar {
    SceneScalarKind kind;
    bool boolValue;
    int64_t intValue;
    double floatValue;
    std::string stringValue;
};

struct SceneLayer {
    std::string name;
    std::string asset;
    bool visible;
    std::vector<int64_t> tiles;
};

struct SceneObject {
    std::string type;
    std::string id;
    int64_t x;
    int64_t y;
    std::map<std::string, SceneScalar> properties;
};

struct SceneDiagnostic {
    std::string code;
    std::string message;
    std::string path;
    int64_t line;
    int64_t column;
    std::string severity;
};

struct PreservedJsonSection {
    std::string key;
    void *jsonRoot;
};

struct SceneState {
    int64_t version;
    std::string name;
    int64_t width;
    int64_t height;
    int64_t tileWidth;
    int64_t tileHeight;
    std::string tilesetAsset;
    std::map<std::string, SceneScalar> properties;
    std::vector<SceneLayer> layers;
    std::vector<SceneObject> objects;
    std::vector<PreservedJsonSection> preservedSections;
    std::vector<SceneDiagnostic> diagnostics;
    bool valid;
};
```

Rules:

- `SceneState` is the only serialization source of truth.
- Preserve imported rich/unknown JSON sections by retaining or deep-copying their
  parsed runtime JSON subtrees; release them in the scene finalizer.
- If direct retained JSON subtrees are too risky, serialize those sections to
  canonical JSON strings and store the strings instead.
- Do not store typed scalar properties as strings. Compatibility string getters
  can format typed scalars on read.
- `ObjectType`, `ObjectId`, `ObjectX`, and `ObjectY` read object metadata, not
  duplicate property entries.
- Legacy flat object scalar keys are migrated into `SceneObject.properties`.
- Layer and object vectors define canonical order and editor reorder behavior.
- Any `BuildTilemap()` result is newly allocated from `SceneState` and does not
  back-reference the scene.

### Resource Limits

The current `rt_scene_editor.cpp` enforces none of these limits and allocates
`width * height` tile storage unguarded, so an oversized or crafted `LoadJson`
input can overflow `int64` or exhaust memory. These limits are a Phase 1
hardening gate that must land before ViperIDE scene editing relies on the type —
note that R6 in the runtime-prerequisites plan already lists the document API as
implemented, but this safety pass is still outstanding:

| Limit | Initial value | Behavior on overflow |
|---|---:|---|
| scene JSON bytes | 16 MiB | invalid scene diagnostic |
| scene width / height | enough that `width * height <= 4,194,304` | invalid scene diagnostic |
| layers | `TM_MAX_LAYERS` | invalid scene diagnostic |
| objects | 65,536 | invalid scene diagnostic |
| scene properties | 16,384 | invalid scene diagnostic |
| object properties | 256 per object | invalid scene diagnostic |
| property key bytes | 128 | invalid scene diagnostic |
| string value bytes | 64 KiB | invalid scene diagnostic |
| diagnostics retained | 256 | retain first 255 plus truncation diagnostic |
| preserved section bytes | 4 MiB total | invalid scene diagnostic |

These are editor/runtime safety defaults. They can be raised later after memory
and performance tests.

## 6. Scene JSON Schema v1

Canonical files are JSON text with a `.scene` extension. The loader may also
accept `.json` and `.level` for compatibility/import, but examples and Save As
should prefer `.scene`.

Schema v1 is an evolution of the landed top-level editor document shape, not the
previous nested `tilemap` proposal.

```jsonc
{
  "version": 1,
  "name": "descent",
  "width": 150,
  "height": 16,
  "tileWidth": 64,
  "tileHeight": 64,
  "tilesetAsset": "tiles/world.png",
  "properties": {
    "theme": "grasslands",
    "playerStartX": 96,
    "playerStartY": 480
  },
  "layers": [
    {
      "name": "base",
      "visible": true,
      "asset": "tiles/world.png",
      "tiles": [0, 1, 0, 2]
    }
  ],
  "objects": [
    {
      "type": "enemy",
      "id": "slime",
      "x": 640,
      "y": 480,
      "properties": {
        "hp": 3,
        "patrolSpeed": 20,
        "elite": false,
        "sprite": "sprites/slime.png"
      }
    }
  ],
  "camera": {
    "zoom": 100,
    "deadzone": { "w": 64, "h": 96 },
    "bounds": { "minX": 0, "minY": 0, "maxX": 9600, "maxY": 1024 },
    "parallax": [
      { "asset": "sky.png", "scrollXPct": 25, "scrollYPct": 0 }
    ]
  },
  "lighting": {
    "darkness": 180,
    "tintColor": 10,
    "playerLight": { "radius": 180, "color": 3150400 },
    "lights": [
      { "x": 640, "y": 480, "radius": 120, "color": 16777215, "lifetime": 0 }
    ]
  }
}
```

### Compatibility Input

`LoadJson` and `LoadFile` must accept the current legacy scene shape:

- missing `version`
- top-level dimensions
- `layers[].data` instead of `layers[].tiles`
- `layers[].asset`
- flat `objects[]` with only `type`, `id`, `x`, and `y`
- string-only top-level `properties`

Legacy input is normalized in memory and `ToJson()` writes canonical v1.

The loader may also accept the old nested draft shape:

- `tilemap.width`
- `tilemap.height`
- `tilemap.tileWidth`
- `tilemap.tileHeight`
- `tilemap.layers[].tiles`
- `tilemap.tilesetAsset`

Nested input is import compatibility only. Canonical output remains top-level
v1.

### Schema Rules

- `version` must be `1` for canonical v1. Missing version means legacy input.
  Versions greater than `1` produce diagnostics and an invalid scene.
- In v1, `width`, `height`, `tileWidth`, and `tileHeight` are required positive
  integers. Legacy input may default missing tile dimensions to `16`.
- `width * height` must not overflow and must be practical to allocate.
- `layers` is required to contain at least one layer after normalization.
- `layers[*].tiles` length must equal `width * height`; mismatches are schema
  errors, not truncation.
- Legacy `layers[*].data` is accepted as `tiles`.
- Layer count must not exceed `TM_MAX_LAYERS`.
- Layer names must fit the current tilemap layer name limit when building a
  `Tilemap`; overlong names should be diagnosed during load.
- Tile ID convention is canonical:
  - `0` means empty/not drawn.
  - `N > 0` means tileset frame `N - 1`.
- Object `type`, `id`, `x`, and `y` are reserved metadata fields.
- Object custom data lives under `objects[*].properties`.
- Legacy flat scalar object keys outside `type/id/x/y` are migrated into
  `properties`.
- Scene and object properties are scalar in v1: string, number, bool, or null.
  Arrays/objects are invalid unless the field is one of the explicitly
  preserved rich sections below.

### Preserve Rich Sections

Phase 1 loaders/savers must preserve these sections even before full runtime
APIs are exposed:

- `camera`
- `lighting`
- `collision`
- `tileProperties`
- `animations`
- `autotiles`
- unknown top-level JSON object sections

Preservation may be opaque: retain the parsed JSON subtree and serialize it
back in canonical order. Do not silently drop these sections during load-save
round trips.

## 7. Error Handling And Diagnostics

Scene loading is editor-facing. It must not trap on ordinary bad user input.

Rules:

- `LoadJson(null)` and `LoadJson("")` return an invalid scene with diagnostics.
- malformed JSON returns an invalid scene with diagnostics.
- non-object roots return an invalid scene with diagnostics.
- missing files return an invalid scene with diagnostics.
- unknown versions return an invalid scene with diagnostics.
- invalid dimensions, tile count mismatches, hard-limit overflows, and invalid
  nested property values produce diagnostics and an invalid scene.
- allocation failure may return `null`.
- out-of-range reads return safe defaults.
- out-of-range writes are no-ops and may add diagnostics only in debug/editor
  validation modes.

Implementation requirement:

- Do not call `rt_json_parse` directly on user scene text unless it is guarded by
  a non-trapping validation path or a trap recovery boundary.
- `DiagnosticRecords()` returns `seq<map>` with stable fields:
  - `code`: machine-readable string such as `scene.parse.malformed_json`
  - `severity`: `error`, `warning`, or `info`
  - `message`: human-readable summary
  - `path`: JSON Pointer path such as `/layers/0/tiles`, or empty when unknown
  - `line`: 1-based source line, or `0` when unknown
  - `column`: 1-based source column, or `0` when unknown
  - `source`: source file path for `LoadFile`, or empty for `LoadJson`
- `Diagnostics()` returns `seq<str>` derived from diagnostic messages for
  compatibility with the landed API.
- `LastError()` returns the newest error message, or the newest diagnostic
  message if only warnings/info exist.
- `HasErrors()` returns true when any retained diagnostic has severity `error`.
- `ClearDiagnostics()` removes retained diagnostics and clears `LastError()`.

Suggested initial diagnostic codes:

| Code | Trigger |
|---|---|
| `scene.load.empty` | null or empty input |
| `scene.load.file_missing` | `LoadFile` cannot open the path |
| `scene.parse.malformed_json` | JSON parser rejects the input |
| `scene.schema.root_not_object` | parsed root is not an object |
| `scene.schema.unsupported_version` | `version` is greater than `1` |
| `scene.schema.missing_field` | required canonical v1 field is absent |
| `scene.schema.invalid_type` | field exists with the wrong JSON type |
| `scene.schema.invalid_dimension` | width/height/tile size is invalid |
| `scene.schema.tile_count_mismatch` | layer tile count differs from `width * height` |
| `scene.schema.limit_exceeded` | resource limit is exceeded |
| `scene.save.write_failed` | temp write/flush/close/replace fails |

## 8. Deterministic Save

`ToJson()` emits canonical v1 JSON.

Canonical order:

1. `version`
2. `name`
3. `width`
4. `height`
5. `tileWidth`
6. `tileHeight`
7. `tilesetAsset`
8. `properties`
9. `layers`
10. `objects`
11. preserved rich sections in fixed known order
12. unknown preserved sections sorted by key

Property keys are sorted lexicographically unless a scene-owned ordered-key list
is added. This avoids relying on `rt_map_keys` iteration order.

Golden invariant:

```text
load input -> save canonical A -> load canonical A -> save canonical B -> A == B
```

`SaveFile(path)` must be atomic enough for editor use:

1. Resolve the target directory and create a same-directory temp path such as
   `.name.scene.tmp.<pid>.<counter>`.
2. Write the full canonical JSON bytes to the temp file.
3. Flush and close the temp file before replacing the target.
4. Replace the target with the temp file. On POSIX, use same-directory `rename`.
   On Windows, use the runtime's equivalent of replace-existing semantics, such
   as `MoveFileExW` with replace/write-through flags if available.
5. If any step fails, remove the temp file when possible, leave the existing
   target untouched, add `scene.save.write_failed`, and return false.

The implementation must not truncate the target before a complete replacement is
ready. Cross-device rename is avoided by creating the temp file in the target
directory.

## 9. Asset Resolution

Scene loading stores asset names only. It does not resolve or load assets.

The editor/game uses `Viper.Assets.Resolver.Resolve(scenePath, projectRoot,
assetRoots, assetPath)` for:

- scene-relative lookup
- project-root lookup
- asset-root lookup
- mounted `Viper.IO.Assets` lookup
- missing-asset diagnostics

`AssetDescriptors()` should expose enough context for the editor to show and
fix missing assets without guessing:

```jsonc
{
  "path": "tiles/world.png",
  "kind": "tileset",
  "owner": "layer",
  "layer": 0,
  "object": -1,
  "key": "asset"
}
```

## 10. Runtime Metadata Requirements

Update all runtime metadata when the API changes:

- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeOwnership.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/tests/unit/CMakeLists.txt`

Owned returns for class methods come from the typed `runtime.def` signatures:
the frontend calling convention treats `str`, `obj<...>`, `seq`, and `map`
returns as owned references the caller must release. The existing owned-returning
Scene helpers (`rt_game_scene_to_json`, `rt_game_scene_diagnostics`,
`rt_game_scene_asset_paths`, and the string getters) carry no
`RuntimeOwnership.hpp` entries today and still pass the runtime-surface audit, so
new owned returns (`DiagnosticRecords`, `AssetDescriptors`, `ObjectKeys`,
`BuildTilemap`, and the typed string getters) do not require explicit
`RuntimeOwnership.hpp` rows.

Add a `RuntimeOwnership.hpp` entry only when a helper has non-default ownership
semantics the typed signature cannot express — for example an argument whose
ownership is consumed or retained by the call. That table is scoped to low-level
string/array/collection primitives (`rt_tilemap_*` and the current Scene helpers
are intentionally absent), not routine class-method returns.

Do not add `RTCLS_GameSceneObject` or `RT_GAME_SCENE_OBJECT_CLASS_ID` until
`SceneObject` exists. If it is added later, object handles must remain valid
after parent-scene release or must hold a safe owner reference.

Run `./scripts/check_runtime_completeness.sh` after metadata edits.

### Runtime Registration Checklist

Every new method or owned return needs a complete metadata pass:

| Area | Required update |
|---|---|
| C entry point | add or update `rt_game_scene_*` symbol in `rt_scene_editor.h/.cpp` |
| Runtime surface | add class/member signatures in `src/il/runtime/runtime.def` |
| Class catalog | confirm `RTCLS_GameScene` and no premature `RTCLS_GameSceneObject` |
| Ownership | owned `str`/`seq`/`map`/`obj` returns are derived from the typed signature; add `RuntimeOwnership.hpp` rows only for consumed/retained args |
| Signatures | update runtime signature construction and arity/type checks |
| Build files | register new source/test files in CMake when added |
| Zia smoke | compile use of new methods from Zia |
| BASIC smoke | compile use of new methods from BASIC where syntax supports it |
| Audits | runtime surface, class catalog, linker import, completeness script |

Register API batches by phase so metadata churn stays reviewable:

| Phase | Runtime API batch |
|---|---|
| 1 | `DiagnosticRecords`, `HasErrors`, `ClearDiagnostics`; canonical `ToJson`; atomic `SaveFile` behavior |
| 2 | typed scene/object property methods, `ObjectKeys`, object search/reorder helpers |
| 3 | `BuildTilemap`, `AssetDescriptors`, descriptor-backed `AssetPaths` |
| 4 | camera, parallax, lighting, collision, tile property, animation, and autotile APIs |
| 5 | no new required runtime surface unless dogfood exposes a missing adapter primitive |

## 11. ViperIDE Integration Sequence

ViperIDE should consume the runtime in this order:

1. Recognize `.scene` as the canonical editable scene document kind. Keep `.json`
   and `.level` as import/compatibility candidates only.
2. Open files through `Viper.Game.Scene.LoadFile(path)` and display
   `DiagnosticRecords()` when `HasErrors()` is true.
3. Save files only through `scene.SaveFile(path)` until conflict detection and
   timestamp checks exist in the IDE layer.
4. Build the first viewport from `scene.BuildTilemap()` or existing scaled
   `Tilemap` primitives. Viewport code must treat returned tilemaps as render
   copies.
5. Route tile editing through scene-owned `SetTile`, `FillTiles`, layer, and
   object APIs. Do not mutate a render copy and then save the scene.
6. Use `AssetDescriptors()` plus `Viper.Assets.Resolver.Resolve` for missing
   asset UI, previews, and project-relative repair actions.
7. Add rich-section UI only after opaque preservation and canonical save tests
   already pass.

## 12. Schema Fixture Matrix

Add fixtures under `tests/fixtures/game/scenes/` or the nearest existing runtime
fixture directory. Each fixture should have a golden canonical output where the
input is valid.

| Fixture | Purpose | Required checks |
|---|---|---|
| `v1_minimal.scene` | smallest canonical document | load, save, reload, exact canonical output |
| `v1_full.scene` | layers, typed properties, objects, assets, rich sections | round-trip preservation and descriptors |
| `legacy_current.json` | current unversioned top-level shape with `layers[].data` | normalize to v1 output |
| `legacy_flat_objects.json` | old flat object scalar keys | migrate keys into `objects[].properties` |
| `legacy_nested.json` | old nested `tilemap` draft, if supported | import to v1 output |
| `tile_ids.scene` | empty and non-empty tile IDs | `0` empty, `N > 0` maps to frame `N - 1` |
| `assets.scene` | layer/object asset references | `AssetDescriptors()` owner/layer/object/key context |
| `invalid_malformed.scene` | parser failure | invalid scene, structured parse diagnostic |
| `invalid_non_object.scene` | root array/scalar | invalid scene, root diagnostic |
| `invalid_version.scene` | version greater than `1` | invalid scene, unsupported version diagnostic |
| `invalid_tile_count.scene` | layer tile count mismatch | invalid scene, JSON path points to bad layer |
| `invalid_limits.scene` | resource limit overflow | invalid scene, limit diagnostic |
| `save_existing.scene` | save over an existing target | simulated write failure preserves target |

## 13. Phases

### Phase 1 - Schema And Safe I/O

- Add canonical v1 schema read/write.
- Accept current legacy unversioned scene JSON.
- Accept old nested draft schema as import compatibility if feasible.
- Add non-trapping load diagnostics.
- Enforce the Section 5 resource limits and guard the `width * height` allocation
  against overflow/OOM (currently unbounded in `rt_scene_editor.cpp`).
- Add `DiagnosticRecords`, `HasErrors`, and `ClearDiagnostics`.
- Make `SaveFile` atomic.
- Preserve rich sections during round trip.
- Document `.scene` as the canonical extension.

Tests:

- valid v1 load
- legacy current-shape load
- old nested-shape import, if supported
- malformed JSON
- missing file
- non-object root
- unknown version
- invalid dimensions
- tile-count mismatch
- resource-limit overflow (oversized dimensions, layer/object/property caps)
  returns an invalid scene without crashing or OOM
- temp-save failure does not truncate target
- canonical load-save-load-save stability

### Phase 2 - Typed Properties And Objects

- Persist object custom properties.
- Parse legacy flat object scalar keys into `properties`.
- Add typed scene property getters/setters.
- Add typed object property getters/setters.
- Add `ObjectKeys`, `ObjectHas`, `CountOfType`, `ObjectOfType`,
  `FindObject`, and `MoveObject`.
- Keep string-only property APIs as compatibility wrappers.

Tests:

- string, int, float, bool, and null property round trip
- missing and incompatible typed getters return caller defaults
- object property setters persist after save/load
- object reorder persists
- out-of-range object reads/writes are safe

### Phase 3 - Rendering And Assets

- Add `BuildTilemap()`.
- Build all scene layers into the returned `Viper.Graphics.Tilemap`.
- Preserve layer visibility and collision layer where available.
- Add structured `AssetDescriptors()`.
- Keep `AssetPaths()` as a derived compatibility helper.
- Ensure scene editor tools mutate scene-owned data, not a returned `Tilemap`.

Tests:

- `BuildTilemap()` dimensions and layer tile values
- returned tilemap mutation does not alter scene save output
- asset descriptors include layer/object/key context
- resolver integration smoke with scene-relative and asset-root paths

### Phase 4 - Rich Engine Sections

- Add camera descriptor APIs.
- Add parallax descriptor APIs.
- Add lighting descriptor/configuration APIs.
- Add collision, tile property, animation, and autotile APIs that map cleanly to
  `Viper.Graphics.Tilemap`.

Tests:

- rich sections preserve before mutation APIs exist
- rich APIs persist after save/load
- `BuildTilemap()` applies collision, tile properties, animations, and autotiles
  where supported by `Tilemap`

### Phase 5 - Dogfood And Docs

- Add `docs/viperlib/game/scene.md`.
- Update `docs/viperlib/game.md` to link to the scene document page.
- Update tilemap docs to state the `0` empty, `N -> frame N - 1` convention.
- Add Zia and BASIC smoke tests for load, typed reads, mutation, save, reload.
- Add a small standalone probe scene first, then migrate
  `examples/games/xenoscape/level.zia`'s `buildDescent()` data to
  `examples/games/xenoscape/levels/descent.scene`.
- Add a Xenoscape spawn adapter that populates the existing `Level` fields from
  `Viper.Game.Scene`: tilemap render copy, player/checkpoint properties,
  enemies, pickups, boss metadata, theme, and tile metadata.
- Keep the procedural `buildDescent()` path during migration and compare the
  loaded `.scene` result against the procedural result in a smoke test.

### Phase 6 - LevelData Decision

After dogfood:

- keep `Viper.Game.LevelData` as a legacy loader,
- retire it from examples, or
- implement it as a thin compatibility wrapper over `Scene`.

Do not add new `LevelData` mutators.

## 14. Critical Files

Existing scene implementation:

- `src/runtime/game/rt_scene_editor.h`
- `src/runtime/game/rt_scene_editor.cpp`
- `src/tests/runtime/RTSceneEditorTests.cpp`

Runtime metadata:

- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeOwnership.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/tests/unit/CMakeLists.txt`

Docs and smoke tests:

- `docs/viperlib/game.md`
- `docs/viperlib/game/scene.md`
- `docs/viperlib/graphics/pixels.md`
- `docs/viperlib/graphics/tilemaps2d.md`
- `tests/fixtures/game/scenes/`
- `tests/rt_api/test_viperide_primitives.zia`
- new Zia scene runtime smoke
- new BASIC scene runtime smoke

Useful reference implementations:

- `src/runtime/game/rt_config.c` for typed default-return getters.
- `src/runtime/graphics/rt_tilemap_io.c` for JSON construction and save/load test patterns.
- `src/runtime/io/rt_ide_primitives.cpp` for `Viper.Assets.Resolver`.
- `src/runtime/graphics/rt_tilemap.c` and `src/runtime/graphics/rt_tilemap_internal.h` for layer/tile hard limits (`TM_MAX_LAYERS = 16`).
- `examples/games/xenoscape/level.zia` for the first real game adapter.

## 15. Verification

Required commands after implementation changes:

- `./scripts/check_runtime_completeness.sh`
- `./scripts/build_viper.sh`
- `ctest --test-dir build --output-on-failure`
- `./scripts/lint_platform_policy.sh`
- `./scripts/run_cross_platform_smoke.sh`

Minimum focused tests before ViperIDE scene editing depends on this:

- `test_rt_scene_editor`
- Zia scene API smoke
- BASIC scene API smoke
- runtime surface audit
- runtime class catalog audit
- linker runtime import audit
- schema fixture matrix
- Xenoscape scene adapter smoke after Phase 5

## 16. Open Decisions

- Whether `Scene.Load` / `Scene.FromString` aliases should be added now or kept
  out until examples need the shorter names.
- Whether nested draft schema import is worth keeping after canonical v1 lands.
- Whether a future `SceneObject` class is worth the ownership complexity once
  indexed object APIs are complete.
