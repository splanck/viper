# Viper Data-Driven Scene System (`Viper.Game.Scene`)

## Context

Goal: let a **full game scene be authored in JSON and imported into a Zia game as fully-typed objects, accessed through object properties — exactly as if hand-coded — without codegen or reflection.**

Today `Viper.Game.LevelData` (runtime.def:5654-5664, `rt_leveldata.c`) loads JSON but is **load-only, single-layer (flattens all tile layers), minimal-object (type/id/x/y), and unused by any shipped game** (xenoscape builds levels procedurally in Zia). We will generalize it into a rich, round-trippable **`Viper.Game.Scene`** runtime class. This is the data foundation the ViperIDE visual scene editor will read/write (see `misc/plans/viperide/roadmap.md`, Phase 3), and the path to replacing hand-coded levels with data-driven ones.

**Why a runtime class (no codegen, no reflection):** Zia is statically compiled and type-erased — there is no construct-by-name or set-field-by-name at runtime (`Lowerer_Expr_Complex.cpp:578-581` bakes classId+offsets at compile time). BUT *runtime* classes are resolved by the Zia compiler at compile time via the runtime catalog (`il::runtime::findRuntimeClassByQName`), so a runtime `Scene` class yields **fully-typed, IntelliSense-complete access with zero compiler changes**. `LevelData` already proves the pattern (its `get_Tilemap()` returns a live, typed `Tilemap`). The JSON **writer already exists** (`rt_json_format_pretty`, `rt_json.h:61`) and `rt_tilemap_io.c:558-665` is a proven Map/Seq→JSON round-trip pattern to mirror.

**The contract (sets expectations):** A `Scene` exposes two kinds of things, both typed, neither needing reflection:
1. **Real engine objects** built from JSON — `Tilemap` (full fidelity), plus camera/lighting/parallax configuration applied to engine objects.
2. **Game objects as typed property-bags** — `SceneObject` with `GetInt/GetStr/GetFloat/GetBool/Has` (the `Config` pattern, `rt_config.h`).

The one thing this deliberately does **not** do is auto-instantiate the user's *own* Zia subclasses with their custom fields — that alone would require reflection/codegen. Instead the game writes a thin, type-safe **spawn adapter** (a `match`/`if` on `obj.Type()`) that reads typed properties and constructs its Zia objects. This is what xenoscape effectively already does, and it is good design (decouples data from code).

## Scene JSON schema (v1)

Mirrors the proven `rt_tilemap_io` field layout for the tilemap, but tilesets are **asset-name references** (never embedded binary), plus arbitrary-property objects, camera, and lighting:

```jsonc
{
  "version": 1,
  "name": "level1",
  "properties": { "theme": "grasslands", "playerStartX": 96, "playerStartY": 480 },
  "tilemap": {
    "width": 150, "height": 16, "tileWidth": 64, "tileHeight": 64,
    "tilesetAsset": "tiles/world.png",      // name, resolved via Assets.Load — NOT a blob
    "collisionLayer": 0,
    "layers": [ { "name": "bg", "visible": true, "tilesetAsset": null, "data": [0,1,...] } ],
    "collision":   [ { "tile": 1, "type": 1 } ],
    "tileProperties": [ { "tile": 5, "entries": [ { "key": "hazard", "value": 5 } ] } ],
    "animations":  [ { "baseTile": 5, "frameCount": 2, "msPerFrame": 200, "frames": [5,6] } ],
    "autotiles":   [ { "baseTile": 20, "variants": [/*16*/] } ]
  },
  "camera":   { "deadzone": {"w":64,"h":96}, "bounds": {"minX":0,"minY":0,"maxX":9600,"maxY":1024},
                "zoom": 100, "parallax": [ { "asset": "sky.png", "scrollXPct": 25, "scrollYPct": 0 } ] },
  "lighting": { "darkness": 180, "lights": [ { "x": 640, "y": 480, "radius": 120, "intensity": 200 } ] },
  "objects": [
    { "type": "enemy", "id": "slime", "x": 640, "y": 480, "hp": 3, "patrolSpeed": 20 },
    { "type": "pickup", "id": "coin",  "x": 320, "y": 384 }
  ]
}
```
`objects[*]` carry **arbitrary** keys → surfaced as a `SceneObject` property-bag. Fixed sections (`tilemap`/`camera`/`lighting`) build real engine objects. Documented in new `docs/viperlib/game/scene.md`.

## Runtime classes (new C, in `src/runtime/game/`)

Modeled structurally on `rt_leveldata.c` (class-ID `#define`, impl struct, `checked_*` accessor, GC finalizer, owned-handle release on every path — the v0.2.6 hardening pattern).

**`Viper.Game.Scene`** (`rt_scene.h/.c`):
- Load: `Scene.Load(path) -> Scene?`, `Scene.FromString(json) -> Scene?` (parity with `rt_config`); both via `rt_json_parse` + `rt_jsonpath_*`.
- Round-trip: `scene.Save(path) -> Bool`, `scene.ToJson() -> String` — build a Map/Seq tree (reusing the `rt_tilemap_io.c:558-665` construction idiom) and emit with `rt_json_format_pretty(root, 2)`.
- Engine objects: `scene.get_Tilemap() -> Tilemap` (full multi-layer build using existing tilemap setters: `SetTileLayer`, `SetCollision`, `SetTileProperty`, `SetTileAnim`, autotile); `scene.ConfigureCamera(cam)` (applies deadzone/bounds/zoom/parallax to a caller-constructed `Camera`, since viewport size is runtime-dependent); `scene.ConfigureLighting(light)`.
- Top-level properties: `scene.GetInt/GetStr/GetFloat/GetBool/Has` (Config-style, for theme/playerStart/etc.).
- Objects: `scene.get_ObjectCount() -> Integer`, `scene.Object(i) -> SceneObject`, `scene.CountOfType(type)`, `scene.ObjectOfType(type, n) -> SceneObject`.
- Editor mutators (enable authoring + round-trip): `scene.SetProperty*`, `scene.AddObject(type,id,x,y) -> SceneObject`, `scene.RemoveObject(i)`; tilemap edits go through the `Tilemap` object directly.

**`Viper.Game.SceneObject`** (typed property-bag; backed by an `rt_map`):
- Read: `GetInt(key,default)`, `GetStr(key,default)`, `GetFloat`, `GetBool`, `Has(key)`, `Keys()`, plus convenience `Type()`, `Id()`, `X()`, `Y()`.
- Write (editor): `SetInt/SetStr/SetFloat/SetBool`.

**runtime.def:** add `Viper.Game.Scene.*` and `Viper.Game.SceneObject.*` `RT_FUNC` entries beside the LevelData block (runtime.def:5654), using the dotted-name + `get_` convention. Use typed returns where it helps — `Object` returns `obj<Viper.Game.SceneObject>` (the DSL supports `obj<Qualified.Name>`, runtime.def:369). Run `./scripts/check_runtime_completeness.sh`.

## Zia usage + the spawn adapter pattern

```
var scene = Scene.Load("levels/level1.json");
tilemap = scene.get_Tilemap();                 // real, typed Tilemap
tilemap.SetTileset(Assets.Load(scene.GetStr("tilesetAsset","")));  // game resolves art by name
scene.ConfigureCamera(cam);

var i = 0;
while i < scene.get_ObjectCount() {
    var o = scene.Object(i);                    // typed SceneObject
    match o.Type() {                            // <-- the thin adapter (your code, type-safe)
        "enemy"  => spawnEnemy(o.Id(), o.X(), o.Y(), o.GetInt("hp", 1));
        "pickup" => spawnPickup(o.Id(), o.X(), o.Y());
    }
    i = i + 1;
}
```

## Phasing (each phase shippable, green, <50 files)
- **Phase A — Core read.** Schema v1 + `Scene`/`SceneObject` classes: load, typed property reads, full-fidelity multi-layer `get_Tilemap()`, object access. runtime.def + `TestScene.cpp` (load valid, empty/invalid→null) modeled on `TestLevelData.cpp`. Supersedes LevelData's load path.
- **Phase B — Round-trip.** `Save`/`ToJson` + mutators (`AddObject`, `SetProperty`, `SceneObject` setters). **Golden round-trip test** (load→Save→stable). This is what unlocks the visual editor.
- **Phase C — Rich engine sections.** `ConfigureCamera` (deadzone/bounds/zoom/parallax-by-asset), `ConfigureLighting`, tile animations/autotiles/per-layer tileset names.
- **Phase D — Dogfood + docs.** Migrate one xenoscape level (e.g. descent) to a JSON scene + a spawn adapter, proving parity with the procedural build; keep the procedural path working during migration. Write `docs/viperlib/game/scene.md` and document the adapter idiom.
- **Phase E — (decision, flagged).** Retire or fold `LevelData` into `Scene` once examples migrate.

## Critical files
- New: `src/runtime/game/rt_scene.h`, `rt_scene.c`; `src/tests/unit/runtime/TestScene.cpp` (+ CMake entry); `docs/viperlib/game/scene.md`.
- Modify: `src/il/runtime/runtime.def` (register Scene/SceneObject); `src/runtime/CMakeLists.txt`; `examples/games/xenoscape/` (one migrated level + adapter).
- Reuse/model on: `rt_leveldata.c` (class structure/finalizer), `rt_tilemap_io.c:558-665` (Map/Seq build idiom), `rt_json_format_pretty` (`rt_json.h:61`), `rt_jsonpath_*` + `rt_config.c` (typed dotted access), `rt_map`/`rt_seq`/`rt_box_*`, `rt_asset` (asset-name resolution).

## Verification
- `./scripts/build_viper.sh` + `ctest --test-dir build --output-on-failure`; `./scripts/check_runtime_completeness.sh` after runtime.def edits.
- Golden round-trip via `./scripts/update_goldens.sh` (load→Save→byte/semantic-stable).
- Run the migrated xenoscape level and confirm parity with the procedural version; `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh`.
- Manual: a tiny Zia program that `Scene.Load`s a file and accesses tilemap + objects with full static typing.

## Risks & decisions
- **Assets by name, never embedded.** `rt_tilemap_save_to_file` embeds binary tilesets (`serialize_pixels_blob`, `rt_tilemap_io.c:567-592`) — unacceptable for editable scene text. Scene stores `tilesetAsset`/parallax/sound **names**; the game resolves via `Assets.Load`. Optional `scene.ResolveTileset(name)` convenience over `rt_asset`.
- **Camera/lighting are configured, not returned.** Viewport size is runtime-dependent, so `ConfigureCamera(cam)`/`ConfigureLighting(light)` apply stored settings to caller-built objects rather than returning a fully-formed `Camera`.
- **Number precision.** `rt_json` numbers are f64-boxed; integer getters cast f64→i64 (same as `Config`/`rt_tilemap_io`). Document it.
- **User Zia subclasses are not auto-built** — by design (no reflection/codegen). The spawn adapter is the supported idiom; if true auto-instantiation of custom classes is ever required, that is a separate, larger initiative (codegen or runtime reflection) to weigh deliberately.
- **LevelData coexistence vs. replacement** — recommend building `Scene` fresh, leaving `LevelData` until examples migrate, then retiring it. Naming/retirement is the user's call.
