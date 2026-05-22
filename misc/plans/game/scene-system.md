# Viper Data-Driven Scene System (`Viper.Game.Scene`)

## Context

Goal: let a full game scene be authored in JSON and loaded into Zia and BASIC as typed runtime objects without reflection, source generation, or manual JSON parsing in game code.

This system is the runtime data foundation for a visual scene editor. The editor writes a stable JSON scene file; the Viper runtime parses that file into a `Viper.Game.Scene` runtime class; game code reads typed engine objects and typed property bags from that scene. User-defined Zia/BASIC game classes are still instantiated by game code through a thin spawn adapter.

`Viper.Game.LevelData` already proves part of the idea, but it is intentionally small: load-only, flat `RT_FUNC` registration, single flattened tile layer, and fixed object fields (`type`, `id`, `x`, `y`). The new scene system should be built fresh and coexist with `LevelData` until examples are migrated.

## Design Contract

`Viper.Game.Scene` exposes two categories of data:

1. Engine-facing data: a typed `Viper.Graphics.Tilemap` view, plus camera, lighting, and parallax descriptors that game code can apply to runtime objects.
2. Game object data: typed `Viper.Game.SceneObject` property bags with scalar getters such as `GetInt`, `GetStr`, `GetFloat`, `GetBool`, `Has`, and `Keys`.

This deliberately does not auto-instantiate arbitrary user Zia/BASIC subclasses. Viper has no runtime construct-by-name or set-field-by-name surface for user classes. The supported pattern is:

1. Load a `Scene`.
2. Enumerate `SceneObject`s.
3. Switch on `obj.Type`.
4. Construct the game-specific object using typed getters.

That keeps scene data decoupled from game code while still giving static frontends typed runtime access.

## Naming And Collision Rules

The public class may be `Viper.Game.Scene`, but every internal identifier must avoid the existing `Viper.Graphics.Scene` runtime class.

Required names:

- Runtime C files: `src/runtime/game/rt_game_scene.h`, `src/runtime/game/rt_game_scene.c`.
- C symbols: `rt_game_scene_*`, `rt_game_scene_object_*`.
- Runtime class ids: `RT_GAME_SCENE_CLASS_ID`, `RT_GAME_SCENE_OBJECT_CLASS_ID`.
- Runtime type ids: `GameScene`, `GameSceneObject`, backed by enum entries `RTCLS_GameScene`, `RTCLS_GameSceneObject`.
- `runtime.def` ids: `GameSceneLoad`, `GameSceneSave`, `GameSceneObjectGetInt`, etc.

Do not create `src/runtime/game/rt_scene.h`; `src/runtime/graphics/rt_scene.h` already exists and flat include lookup would make `"rt_scene.h"` ambiguous.

## Scene JSON Schema (v1)

`Scene` v1 is an editor-friendly schema, not the same file format as `Tilemap.SaveToFile`. It maps to the existing Tilemap runtime features but stores asset names instead of embedded pixel blobs.

```jsonc
{
  "version": 1,
  "name": "level1",
  "properties": {
    "theme": "grasslands",
    "playerStartX": 96,
    "playerStartY": 480
  },
  "tilemap": {
    "width": 150,
    "height": 16,
    "tileWidth": 64,
    "tileHeight": 64,
    "tilesetAsset": "tiles/world.png",
    "layers": [
      {
        "name": "base",
        "visible": true,
        "tilesetAsset": null,
        "tiles": [0, 1, 0, 2]
      }
    ],
    "collision": {
      "layer": 0,
      "types": [
        { "tile": 1, "type": 1 }
      ]
    },
    "tileProperties": [
      { "tile": 5, "entries": [ { "key": "hazard", "value": 5 } ] }
    ],
    "animations": [
      { "baseTile": 5, "msPerFrame": 200, "frames": [5, 6] }
    ],
    "autotiles": [
      { "baseTile": 20, "variants": [20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35] }
    ]
  },
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
  },
  "objects": [
    { "type": "enemy", "id": "slime", "x": 640, "y": 480, "hp": 3, "patrolSpeed": 20 },
    { "type": "pickup", "id": "coin", "x": 320, "y": 384 }
  ]
}
```

Schema rules:

- `version` must be `1`. Unknown major versions fail to load. Future optional fields may be ignored only within version 1.
- `tilemap.width` and `tilemap.height` are required and must be greater than zero. `tileWidth` and `tileHeight` default to `32` if missing or invalid, matching `LevelData` behavior.
- `layers[*].tiles` length must equal `width * height`. A mismatch fails the load instead of silently truncating.
- Tile IDs follow existing `Tilemap` rules: `0` is empty; positive IDs are 1-based tileset indices.
- Tilemap hard limits are runtime limits and must be validated: `TM_MAX_LAYERS`, `MAX_TILE_COLLISION_IDS`, `TM_MAX_TILE_ANIMS`, `TM_MAX_ANIM_FRAMES`, `MAX_TILE_PROPS`, `MAX_PROP_KEYS`, `MAX_PROP_KEY_LEN`, and `MAX_AUTOTILE_RULES`.
- Object properties are scalar in v1: string, number, bool, and null. Arrays/objects in `objects[*]` are rejected in Phase A or preserved only if an explicit future `GetJson` API is added.
- Reserved object keys are `type`, `id`, `x`, and `y`. They also live in the property bag; convenience properties read from the same stored values.

## Runtime Ownership Model

`GameScene` owns normalized scene data. That data is the serialization source of truth.

The cached `Viper.Graphics.Tilemap` is a runtime view built from scene data. It is not the serialization source of truth because the current `Tilemap` stores pixels, not asset names, and does not retain tileset asset paths. Phase A should expose this cached view read-only. Phase B editor mutators must update `GameScene`'s normalized data first, then invalidate or rebuild the cached `Tilemap`.

`SceneObject` should be a live runtime object backed by a scene-owned object record or a shared retained map. Do not return detached copies if editor setters exist, because detached copies cannot save back into the parent scene. The object handle must remain valid after the parent scene is released by retaining the backing map/object record or by keeping a safe owner reference.

`ConfigureCamera` and `ConfigureLighting` mutate caller-owned runtime objects and take no ownership of them.

## Runtime Classes

Register both classes with the structured `RT_CLASS` macro family. Add backing `RT_FUNC` entries for every method/property, then expose them through `RT_CLASS_BEGIN`.

Instance method signatures in `RT_METHOD` omit the receiver. The backing `RT_FUNC` ABI includes the receiver as the first `obj` argument.

Example registration shape:

```cpp
RT_FUNC(GameSceneLoad,        rt_game_scene_load,        "Viper.Game.Scene.Load",           "obj<Viper.Game.Scene>(str)")
RT_FUNC(GameSceneFromString,  rt_game_scene_from_string, "Viper.Game.Scene.FromString",     "obj<Viper.Game.Scene>(str)")
RT_FUNC(GameSceneSave,        rt_game_scene_save,        "Viper.Game.Scene.Save",           "i1(obj,str)")
RT_FUNC(GameSceneToJson,      rt_game_scene_to_json,     "Viper.Game.Scene.ToJson",         "str(obj)")
RT_FUNC(GameSceneGetTilemap,  rt_game_scene_get_tilemap, "Viper.Game.Scene.get_Tilemap",    "obj<Viper.Graphics.Tilemap>(obj)")
RT_FUNC(GameSceneObjectCount, rt_game_scene_object_count,"Viper.Game.Scene.get_ObjectCount","i64(obj)")
RT_FUNC(GameSceneObjectAt,    rt_game_scene_object_at,   "Viper.Game.Scene.Object",         "obj<Viper.Game.SceneObject>(obj,i64)")

RT_CLASS_BEGIN("Viper.Game.Scene", GameScene, "obj", none)
    RT_METHOD("Load", "obj<Viper.Game.Scene>(str)", GameSceneLoad)
    RT_METHOD("FromString", "obj<Viper.Game.Scene>(str)", GameSceneFromString)
    RT_METHOD("Save", "i1(str)", GameSceneSave)
    RT_METHOD("ToJson", "str()", GameSceneToJson)
    RT_PROP("Tilemap", "obj<Viper.Graphics.Tilemap>", GameSceneGetTilemap, none)
    RT_PROP("ObjectCount", "i64", GameSceneObjectCount, none)
    RT_METHOD("Object", "obj<Viper.Game.SceneObject>(i64)", GameSceneObjectAt)
RT_CLASS_END()
```

The actual implementation should also expose:

- Dotted-path scalar reads over the normalized scene root: `GetInt(path, default)`, `GetStr(path, default)`, `GetFloat(path, default)`, `GetBool(path, default)`, `Has(path)`. Game-specific properties live under `properties.*`.
- Object enumeration helpers: `CountOfType(type)`, `ObjectOfType(type, n)`, and optionally `FindObject(id)`.
- Camera scalar application: `ConfigureCamera(camera)` applies zoom, bounds, and deadzone only.
- Parallax descriptors: `ParallaxCount`, `ParallaxAsset(i)`, `ParallaxScrollXPct(i)`, `ParallaxScrollYPct(i)`. Game code resolves assets and calls `Camera.AddParallax`.
- Lighting application: `ConfigureLighting(light)` sets darkness/tint/player light, calls `ClearLights`, then adds configured dynamic lights up to the target `Lighting2D` capacity.

`SceneObject` should expose:

- Read properties: `Type`, `Id`, `X`, `Y`.
- Read methods: `GetInt(key, default)`, `GetStr(key, default)`, `GetFloat(key, default)`, `GetBool(key, default)`, `Has(key)`, `Keys()`.
- Phase B write methods: `SetInt`, `SetStr`, `SetFloat`, `SetBool`, `Remove`, only after live write-through semantics are implemented.

`Keys()` should be registered as `seq<str>()`, not untyped `obj()`.

## Runtime Metadata Requirements

Implementation must update all of these, not just `runtime.def`:

- `src/il/runtime/runtime.def`: backing `RT_FUNC`s and structured `RT_CLASS` declarations.
- `src/il/runtime/classes/RuntimeClasses.hpp`: add `RTCLS_GameScene` and `RTCLS_GameSceneObject`.
- `src/il/runtime/RuntimeOwnership.hpp`: mark owned returns such as scene factories, `ToJson`, `Object`, `Keys`, and any copied string/object returns.
- `src/runtime/CMakeLists.txt`: add `rt_game_scene.c` to the game runtime component.
- `src/il/runtime/RuntimeSignatures.cpp`: include `rt_game_scene.h` if the generator/header audit does not pick it up automatically.
- `src/tests/unit/CMakeLists.txt`: add the new unit test executable.

Run `./scripts/check_runtime_completeness.sh` after metadata edits.

## Error Handling

- `Scene.Load(path)` returns null for missing, empty, unreadable, or malformed JSON.
- `Scene.FromString(json)` returns null for null, empty, malformed, non-object, unknown-version, or invalid required schema.
- Invalid tilemap dimensions, tile array size mismatches, unsupported nested object properties, and hard-limit overflows fail the load.
- Optional sections (`camera`, `lighting`, `objects`, `properties`) may be absent and default to empty configuration.
- `Object(i)` returns null out of range.
- Getters return the caller-provided default for missing keys or incompatible scalar types. `Has` distinguishes missing from present.
- `Save(path)` returns false on I/O failure. It writes a temp file in the same directory and renames it into place so failed saves do not truncate the target.
- Unknown asset names do not fail scene loading. Runtime code stores and exposes asset names; game/editor code resolves them.
- Add a diagnostic string before or during Phase B, either `Scene.LastError` or an out-of-band runtime error helper, because editor users need actionable schema errors.

## Deterministic Save

Do not rely on generic `Map` key iteration order for canonical output. `rt_map_keys` is implementation-defined. Build the output tree by inserting known scene fields in a fixed serializer order, and serialize object property bags by either:

- preserving original object key order in a scene-owned ordered list, or
- sorting property keys during serialization.

The golden invariant is save-normalized stability:

```text
load input -> save canonical A -> load canonical A -> save canonical B -> A == B
```

Hand-authored input may differ in whitespace or key order.

## Zia Usage

```zia
module Game;

func start() {
    var scene = Viper.Game.Scene.Load("levels/level1.json");
    if scene == null {
        return;
    }

    var tilemap = scene.Tilemap;
    var tilesetName = scene.GetStr("tilemap.tilesetAsset", "");
    var theme = scene.GetStr("properties.theme", "default");

    scene.ConfigureCamera(camera);

    var i = 0;
    while i < scene.ObjectCount {
        var obj = scene.Object(i);
        if obj != null {
            if obj.Type == "enemy" {
                spawnEnemy(obj.Id, obj.X, obj.Y, obj.GetInt("hp", 1));
            } else if obj.Type == "pickup" {
                spawnPickup(obj.Id, obj.X, obj.Y);
            }
        }
        i = i + 1;
    }
}
```

Also add a BASIC smoke example/test because BASIC consumes runtime class metadata too.

## Phasing

Phase A - Core read:

- Add `GameScene` and `GameSceneObject` runtime classes with unique internal names.
- Parse schema v1 into normalized scene-owned data.
- Expose dotted-path scalar reads.
- Expose object enumeration and scalar object reads.
- Build and cache a typed `Viper.Graphics.Tilemap` view.
- Expose asset-name descriptors for tilesets/parallax, but do not resolve assets inside the loader.
- Add unit tests for valid load, malformed JSON, missing file, invalid dimensions, layer tile-count mismatch, object scalar getters, out-of-range object access, class-id validation, and cached tilemap behavior.
- Add Zia and BASIC compile smoke tests for `Scene.Load`, `scene.Tilemap`, `scene.ObjectCount`, `scene.Object(0)`, and typed object getters.

Phase B - Round-trip and editor mutation:

- Add `ToJson`, `Save`, canonical serialization, and temp-file rename.
- Add live scene property mutators.
- Add live `SceneObject` mutators that write through to scene-owned data.
- Add object add/remove/reorder APIs.
- Decide and implement tilemap edit APIs on `Scene` itself, or extend `Tilemap` with scene-owned metadata. Do not claim direct `Tilemap` edits are saveable until this is solved.
- Add canonical round-trip tests and object setter persistence tests.

Phase C - Rich engine sections:

- Add camera scalar configuration.
- Add parallax descriptor accessors.
- Add lighting configuration with runtime API-compatible fields (`darkness`, `tintColor`, player light, dynamic lights with `color` and `lifetime`).
- Add tile animations, autotile rules, per-layer tileset asset names, and their tests.

Phase D - Dogfood and docs:

- Migrate one Xenoscape level to a JSON scene plus spawn adapter.
- Keep the procedural level path during migration.
- Write `docs/viperlib/game/scene.md` with schema, runtime APIs, Zia usage, BASIC usage, and editor notes.

Phase E - LevelData decision:

- After examples migrate, either retire `Viper.Game.LevelData`, keep it as a legacy loader, or implement it as a thin compatibility wrapper over `Scene`.

## Critical Files

New:

- `src/runtime/game/rt_game_scene.h`
- `src/runtime/game/rt_game_scene.c`
- `src/tests/unit/runtime/TestGameScene.cpp`
- `docs/viperlib/game/scene.md`

Modify:

- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeOwnership.hpp`
- `src/il/runtime/RuntimeSignatures.cpp` if needed for header inclusion
- `src/runtime/CMakeLists.txt`
- `src/tests/unit/CMakeLists.txt`
- Zia frontend/runtime-class smoke tests
- BASIC frontend/runtime-class smoke tests
- `examples/games/xenoscape/` during dogfood phase

Reuse/model on:

- `rt_leveldata.c` for load/finalizer hardening patterns.
- `rt_config.c` for typed getter behavior.
- `rt_tilemap_io.c` for Map/Seq JSON construction style, but not for embedded tileset blob fields.
- `rt_json_format_pretty`, `rt_jsonpath_*`, `rt_map`, `rt_seq`, and `rt_box_*`.

## Verification

Required commands:

- `./scripts/check_runtime_completeness.sh`
- `./scripts/build_viper.sh`
- `ctest --test-dir build --output-on-failure`
- `./scripts/lint_platform_policy.sh`
- `./scripts/run_cross_platform_smoke.sh`

Required tests:

- Unit: successful load with multiple layers.
- Unit: missing, empty, malformed, non-object, unknown-version, invalid-dimension, and tile-count-mismatch inputs return null.
- Unit: object getters cover string, int, float, bool, missing defaults, and incompatible defaults.
- Unit: `Object(i)` out of range returns null.
- Unit: `SceneObject` remains valid according to the chosen lifetime model.
- Unit: cached `Tilemap` returns the same handle until invalidated.
- Unit: `ToJson`/`Save` canonical output is stable after load-save-load-save.
- Unit: Phase B setters persist into `Save`.
- Frontend: Zia static compile smoke for typed access.
- Frontend: BASIC static compile smoke for typed access.
- Example: migrated Xenoscape level behaves like the procedural version.

## Risks And Decisions

- Public naming: `Viper.Game.Scene` is user-friendly but close to `Viper.Graphics.Scene`. Internals must use `GameScene`; docs should distinguish data scenes from scene graph scenes.
- Asset resolution: the loader should stay data-only. It exposes asset names; game/editor code resolves them. Optional convenience resolvers can be added later without making load depend on assets.
- Tilemap editing: current `Tilemap` cannot save asset-name metadata. Scene-owned data must remain the source of truth unless Tilemap is extended.
- Numeric precision: `rt_json` stores JSON numbers as f64. Document integer precision and cast behavior.
- Schema evolution: version 1 should fail on unknown major versions. Add a migration story before introducing version 2.
- Diagnostics: null/false is enough for runtime game fallback, but not enough for a scene editor. Add a retrievable last-error mechanism before editor integration.
