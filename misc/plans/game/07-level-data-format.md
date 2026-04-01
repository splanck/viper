# Plan 07: Level Data Format with Entity Spawns

## Context

XENOSCAPE's level.zia is 2063 lines of procedural level construction — 432 calls to
addPlatformTile/addEnemy/addPickup across 10 levels. Tilemap.LoadFromFile exists but
its format is undocumented and doesn't support entity spawn points.

## Design

Extend Tilemap.LoadFromFile to support a JSON-based level format that includes:
1. Tile layer data (CSV or inline arrays)
2. Object/entity spawn layer with type + position
3. Level metadata (theme, dimensions, player start)

## Changes

### rt_tilemap_io.c — extend LoadFromFile (~250 LOC)

**Level JSON format:**
```json
{
    "version": 1,
    "width": 150,
    "height": 18,
    "tileWidth": 64,
    "tileHeight": 64,
    "properties": {
        "theme": "grasslands",
        "playerStartX": 192,
        "playerStartY": 896
    },
    "layers": [
        {
            "name": "terrain",
            "type": "tiles",
            "data": [0,0,1,1,1,0,0,...]
        }
    ],
    "objects": [
        {"type": "enemy", "id": "slime", "x": 1408, "y": 896},
        {"type": "enemy", "id": "bat", "x": 2048, "y": 640},
        {"type": "pickup", "id": "coin", "x": 448, "y": 704},
        {"type": "pickup", "id": "health", "x": 960, "y": 512}
    ]
}
```

**New struct for parsed level data:**
```c
#define LEVEL_MAX_OBJECTS 512

typedef struct {
    char type[32];     // "enemy", "pickup", "trigger", etc.
    char id[32];       // "slime", "coin", etc.
    int64_t x, y;      // World pixel position
    int64_t props[4];  // Up to 4 integer properties
} level_object_t;

typedef struct {
    void *tilemap;                         // Parsed Tilemap
    level_object_t objects[LEVEL_MAX_OBJECTS];
    int32_t object_count;
    int64_t player_start_x;
    int64_t player_start_y;
    char theme[32];
} rt_level_data;
```

**New functions:**
```c
// Load level from JSON file. Returns LevelData object with tilemap + spawns.
void *rt_level_load(rt_string path);

// LevelData accessors
void *rt_level_get_tilemap(void *level);
int64_t rt_level_object_count(void *level);
rt_string rt_level_object_type(void *level, int64_t index);
rt_string rt_level_object_id(void *level, int64_t index);
int64_t rt_level_object_x(void *level, int64_t index);
int64_t rt_level_object_y(void *level, int64_t index);
int64_t rt_level_player_start_x(void *level);
int64_t rt_level_player_start_y(void *level);
rt_string rt_level_get_theme(void *level);
```

The JSON parsing uses the existing `rt_json_parse` internal API (already in runtime).

### runtime.def
```
RT_CLASS_BEGIN("Viper.Game.LevelData", LevelData, "obj", none)
    RT_METHOD("Load",          "obj(str)",          LevelDataLoad)
    RT_PROP("Tilemap",         "obj",  LevelDataGetTilemap, none)
    RT_PROP("ObjectCount",     "i64",  LevelDataObjectCount, none)
    RT_PROP("PlayerStartX",    "i64",  LevelDataPlayerStartX, none)
    RT_PROP("PlayerStartY",    "i64",  LevelDataPlayerStartY, none)
    RT_PROP("Theme",           "str",  LevelDataGetTheme, none)
    RT_METHOD("ObjectType",    "str(i64)",          LevelDataObjectType)
    RT_METHOD("ObjectId",      "str(i64)",          LevelDataObjectId)
    RT_METHOD("ObjectX",       "i64(i64)",          LevelDataObjectX)
    RT_METHOD("ObjectY",       "i64(i64)",          LevelDataObjectY)
RT_CLASS_END()
```

### Zia usage
```zia
var level = LevelData.Load("levels/level1.json")
var tilemap = level.get_Tilemap()
player.set_X(level.get_PlayerStartX())
player.set_Y(level.get_PlayerStartY())

// Spawn entities from object layer
var i = 0
while i < level.get_ObjectCount() {
    var objType = level.ObjectType(i)
    var objId = level.ObjectId(i)
    var ox = level.ObjectX(i)
    var oy = level.ObjectY(i)
    if objType == "enemy" { enemies.spawn(objId, ox, oy) }
    if objType == "pickup" { pickups.spawn(objId, ox, oy) }
    i = i + 1
}
```

### Files to modify
- `src/runtime/graphics/rt_tilemap_io.c` — extend or new level loader (~250 LOC)
- New: `src/runtime/game/rt_leveldata.h` (~40 LOC)
- `src/il/runtime/runtime.def` — ~12 entries
- `src/il/runtime/RuntimeSignatures.cpp` — include header
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_LevelData
- `src/runtime/CMakeLists.txt` — if new file needed

### Tests

**File:** `src/tests/unit/runtime/TestLevelData.cpp`
```
TEST(LevelData, LoadSimpleLevel)
  — Write JSON to temp, load, verify tilemap dimensions

TEST(LevelData, PlayerStartPosition)
  — Verify playerStartX/Y parsed correctly

TEST(LevelData, ObjectsLoaded)
  — 3 objects in JSON, verify count=3, types/ids/positions correct

TEST(LevelData, ThemeParsed)
  — Verify theme string from properties

TEST(LevelData, EmptyObjectLayer)
  — Level with no objects, verify count=0

TEST(LevelData, NonExistentFile)
  — Load missing file, verify returns NULL

TEST(LevelData, InvalidJSON)
  — Load malformed JSON, verify returns NULL
```

### Doc update
- New: `docs/viperlib/game/leveldata.md` — format spec + API
