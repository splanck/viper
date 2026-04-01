# Plan 10: Typed Config Loader

## Context

XENOSCAPE's config.zia is 759 lines of `final` constant declarations — 562 values for
physics, tile IDs, enemy stats, screen dimensions, etc. All hardcoded in source.
Viper has JSON/TOML/INI parsers but they return untyped objects requiring verbose
JsonPath.GetStr/GetInt calls for each value. A game config system should make loading
structured data trivial.

## Design

A `Config` helper class that wraps JSON (or TOML) with typed getters and dotted path
access, plus a default value mechanism. Uses the existing JSON parser internally.

## Changes

### New file: `src/runtime/game/rt_config.c` (~200 LOC)

**Config struct:**
```c
typedef struct rt_config_impl {
    void *json_root;     // Parsed JSON root (Viper JSON object)
    char *source_path;   // File path (for error messages)
} rt_config_impl;
```

**Functions:**
```c
// Load config from file (reads file, parses JSON)
void *rt_config_load(rt_string path);

// Load config from string (inline JSON)
void *rt_config_from_string(rt_string json_str);

// Typed getters with dotted path (e.g., "physics.gravity")
int64_t rt_config_get_int(void *cfg, rt_string path, int64_t default_val);
double rt_config_get_float(void *cfg, rt_string path, double default_val);
rt_string rt_config_get_str(void *cfg, rt_string path, rt_string default_val);
int8_t rt_config_get_bool(void *cfg, rt_string path, int8_t default_val);

// Check existence
int8_t rt_config_has(void *cfg, rt_string path);

// Get subsection as new Config (for nested access)
void *rt_config_section(void *cfg, rt_string path);
```

**Implementation:** Uses existing `rt_json_parse_string()` and `rt_jsonpath_get_*()` internally. The `Config` class is a thin ergonomic wrapper.

### runtime.def
```
RT_CLASS_BEGIN("Viper.Game.Config", GameConfig, "obj", none)
    RT_METHOD("Load",       "obj(str)",              ConfigLoad)
    RT_METHOD("FromString", "obj(str)",              ConfigFromString)
    RT_METHOD("GetInt",     "i64(str,i64)",          ConfigGetInt)
    RT_METHOD("GetFloat",   "f64(str,f64)",          ConfigGetFloat)
    RT_METHOD("GetStr",     "str(str,str)",          ConfigGetStr)
    RT_METHOD("GetBool",    "i1(str,i1)",            ConfigGetBool)
    RT_METHOD("Has",        "i1(str)",               ConfigHas)
    RT_METHOD("Section",    "obj(str)",              ConfigSection)
RT_CLASS_END()
```

### Zia usage
```zia
// game.toml (or game.json):
// { "screen": { "width": 1280, "height": 720 },
//   "physics": { "gravity": 78, "maxFall": 1350, "playerSpeed": 525 },
//   "tiles": { "size": 64 } }

var cfg = Config.Load("game.json")
var screenW = cfg.GetInt("screen.width", 1280)
var screenH = cfg.GetInt("screen.height", 720)
var gravity = cfg.GetInt("physics.gravity", 78)
var tileSize = cfg.GetInt("tiles.size", 64)

// Subsection access:
var phys = cfg.Section("physics")
var jump = phys.GetInt("playerJump", -1500)
```

### Files to modify
- New: `src/runtime/game/rt_config.c` (~200 LOC)
- New: `src/runtime/game/rt_config.h` (~30 LOC)
- `src/il/runtime/runtime.def` — ~10 entries
- `src/il/runtime/RuntimeSignatures.cpp` — include header
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_GameConfig
- `src/runtime/CMakeLists.txt` — add source

### Tests

**File:** `src/tests/unit/runtime/TestGameConfig.cpp`
```
TEST(Config, LoadAndGetInt)
  — Write JSON to temp, load, verify GetInt returns correct value

TEST(Config, GetIntDefault)
  — Missing key returns default value

TEST(Config, GetStr)
  — Verify string value retrieval

TEST(Config, GetBool)
  — Verify boolean retrieval

TEST(Config, GetFloat)
  — Verify float retrieval

TEST(Config, DottedPath)
  — "physics.gravity" navigates nested object

TEST(Config, HasKey)
  — Existing key returns true, missing returns false

TEST(Config, Section)
  — Section("physics") returns sub-config, GetInt on it works

TEST(Config, FromString)
  — Parse inline JSON string

TEST(Config, NonExistentFile)
  — Load missing file returns NULL
```

### Doc update
- New: `docs/viperlib/game/config.md`
