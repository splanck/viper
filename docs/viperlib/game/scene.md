---
status: active
audience: public
last-verified: 2026-05-22
---

# Editable Scene Documents
> JSON-backed `Viper.Game2D.SceneDocument` documents for IDE scene tools and game-owned spawn adapters.

**Part of [Game Utilities](README.md)**

`Viper.Game2D.SceneDocument` is an editable scene document. It owns tile layers, placed
objects, typed scalar properties, asset references, diagnostics, and canonical
JSON save/load. It does not instantiate game classes by string name; game code
loads a scene and maps objects/properties into game-owned entities.

## Construction And I/O

| Method | Signature | Description |
|---|---|---|
| `SceneDocument.New(width, height, tileWidth, tileHeight)` | `SceneDocument(i64, i64, i64, i64)` | Create a scene with one base layer. |
| `SceneDocument.LoadJson(text)` | `SceneDocument(str)` | Load scene JSON without trapping on malformed user input. |
| `SceneDocument.Load(path)` | `SceneDocument(str)` | Read and load a scene file. |
| `ToJson()` | `str()` | Emit canonical schema v1 JSON. |
| `Save(path)` | `i1(str)` | Save through a same-directory temporary file before replacement. |

Canonical scene files use the `.scene` extension. The loader also accepts legacy
unversioned JSON with `layers[].data` and LevelData-shaped scalar properties for
import compatibility.

`Save` writes a temporary file next to the target and replaces the target
only after that write succeeds. Failed replacement leaves the temporary file
removed and records an error diagnostic; an existing target is not deleted first.

## Diagnostics

| Method | Description |
|---|---|
| `HasErrors()` | True when retained diagnostics include an error. |
| `LastError()` | Newest diagnostic message. |
| `Diagnostics()` | Compatibility `Seq<str>` of diagnostic messages. |
| `DiagnosticRecords()` | `Seq<Map>` with `code`, `severity`, `message`, `path`, `line`, `column`, and `source`. |
| `ClearDiagnostics()` | Clear retained diagnostics and `LastError()`. |

Bad JSON, missing files, unknown versions, invalid dimensions, v1 tile count
mismatches, and resource-limit violations return an invalid scene with
diagnostics instead of trapping.

Edit-time rejections, such as an over-long layer name or property key, are
reported as warning diagnostics. They do not make `HasErrors()` true unless a
load/save/schema error is also retained.

## Tiles And Layers

| Method | Description |
|---|---|
| `AddLayer(name)` | Add a layer and return its index, or `-1` if the scene is at its layer/cell limit. |
| `LayerCount()` | Number of layers. |
| `LayerName(layer)` / `SetLayerName(layer, name)` | Read or update layer names. Names must fit Tilemap's 31-byte layer-name limit. |
| `LayerVisible(layer)` / `SetLayerVisible(layer, visible)` | Read or update visibility. |
| `MoveLayer(from, to)` / `RemoveLayer(layer)` | Reorder or remove layers. The final layer is kept. |
| `GetTile(layer, x, y)` / `SetTile(layer, x, y, tile)` | Read or mutate tile IDs. Out-of-range reads return `0`; writes no-op. |
| `FillTiles(layer, x, y, width, height, tile)` | Fill a clamped rectangular region. |
| `SetLayerAsset(layer, path)` / `LayerAsset(layer)` | Store a layer tileset/source asset path. |

Tile ID `0` means empty/not drawn. Tile ID `N > 0` maps to tileset frame
`N - 1` when rendered by `Tilemap`.

## Objects And Properties

Objects have reserved metadata fields: `type`, `id`, `x`, and `y`. Custom data
lives in typed scalar properties: `null`, bool, int, float, or string.

| Method | Description |
|---|---|
| `AddObject(type, id, x, y)` | Add a placed object and return its index. |
| `ObjectCount()` / `RemoveObject(index)` | Count or remove objects. |
| `ObjectType(index)` / `ObjectId(index)` | Read object metadata. |
| `ObjectX(index)` / `ObjectY(index)` / `SetObjectPosition(index, x, y)` | Read or update position. |
| `ObjectGetInt/Str/Float/Bool(index, key, default)` | Typed property reads; incompatible kinds return the supplied default. |
| `ObjectSetInt/Str/Float/Bool(index, key, value)` | Typed property writes. |
| `ObjectHas(index, key)` / `ObjectKeys(index)` / `ObjectRemove(index, key)` | Inspect or remove object properties. |
| `CountOfType(type)` / `ObjectOfType(type, n)` / `FindObject(id)` | Search helpers returning counts or indexes. |
| `MoveObject(from, to)` | Reorder objects. |

Compatibility methods `SetObjectProperty`, `GetObjectProperty`, and
`DeleteObjectProperty` remain string wrappers over typed object properties.

Scene-level typed properties use the same scalar rules:

| Method | Description |
|---|---|
| `GetInt/Str/Float/Bool(key, default)` | Typed scene property reads. |
| `SetInt/Str/Float/Bool(key, value)` | Typed scene property writes. |
| `Has(key)` / `Remove(key)` | Inspect or remove scene properties. |

Compatibility methods `SetProperty`, `GetProperty`, and `DeleteProperty` remain
available for string-oriented callers.

## Assets And Tilemap Copies

`AssetDescriptors()` returns `Seq<Map>` records with:

- `path`: scene-authored asset path
- `kind`: `tileset`, `sprite`, `image`, `audio`, or `unknown`
- `owner`: `scene`, `layer`, `object`, or `section`
- `layer`, `object`: owner indexes, or `-1`
- `key`: source field/property key
- `section`: rich section name when applicable
- `source`: scene file path for `Load`, or empty for `LoadJson`

`AssetPaths()` returns the unique path strings derived from descriptors.
Descriptors are guaranteed for explicit scene fields such as `tilesetAsset` and
layer `asset`. String scene/object properties and preserved rich sections are
also scanned by asset-like key names for compatibility; treat those matches as
best-effort conventions until the schema grows typed asset properties.

`BuildTilemap()` creates a new `Viper.Graphics2D.Tilemap` render/collision copy.
Scene layer `0` maps to the Tilemap base layer; additional scene layers are
added up to the Tilemap layer limit. Mutating the returned Tilemap does not
change the scene or saved JSON. Asset paths are not resolved or loaded by
`BuildTilemap()`; use `Viper.Assets.Resolver.Resolve` and bind tilesets in game
or editor code.

When present, preserved `collision`, `tileProperties`, `animations`, and
`autotiles` sections are applied to the returned Tilemap using the matching
Tilemap runtime APIs.

## JSON Schema V1

```json
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
      "id": "slime-1",
      "x": 640,
      "y": 480,
      "properties": {
        "hp": 3,
        "sprite": "sprites/slime.png"
      }
    }
  ]
}
```

Known rich sections such as `camera`, `lighting`, `collision`,
`tileProperties`, `animations`, and `autotiles` are preserved through load/save
round trips even when no higher-level editor API has changed them.
