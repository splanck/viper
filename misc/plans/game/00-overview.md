# Game Engine Abstractions — Overview

## Motivation

The XENOSCAPE sidescroller demo (17,085 lines) revealed that ~8,000 lines are boilerplate
that higher-level abstractions would eliminate. These 10 plans address the most impactful
gaps, prioritized by prerequisite dependencies and lines of game code eliminated.

## Implementation Order (dependency-aware)

```
Phase 1 (no dependencies):
  01-camera-enhancements     — SmoothFollow, Deadzone (Shake exists via ScreenFX)  (~80 C LOC)
  02-animcontroller-names    — String names + frame events         (~80 C LOC)
  04-tilemap-tile-animation  — Per-tile anim in Tilemap           (~150 C LOC)
  05-raycast-2d              — Line intersection + tilemap DDA    (~350 C LOC)
  10-config-loader           — Typed JSON config wrapper          (~200 C LOC)

Phase 2 (depends on Phase 1):
  03-entity-class            — Unified game object                (~350 C LOC)
  08-scene-manager           — Multi-scene + transitions          (~250 C LOC)
  09-game-ui-menu            — Button + MenuList input handling (MenuList rendering exists)  (~300 C LOC)

Phase 3 (depends on 03-entity):
  06-behavior-system         — Composable AI presets              (~400 C LOC)
  07-level-data-format       — JSON level loader + entity spawns  (~250 C LOC)
```

## Summary Table

| # | Plan | New C LOC | Tests | Game Lines Eliminated |
|---|------|-----------|-------|----------------------|
| 01 | Camera SmoothFollow + Deadzone | ~80 | 4 | ~500 |
| 02 | AnimController names | ~80 | 6 | ~300 |
| 03 | Entity class | ~350 | 10 | ~1,500 |
| 04 | Tilemap tile anim | ~150 | 4 | ~100 |
| 05 | 2D Raycast | ~350 | 10 | ~150 |
| 06 | Behavior system | ~400 | 10 | ~1,200 |
| 07 | Level data format | ~250 | 7 | ~1,800 |
| 08 | Scene manager | ~250 | 9 | ~500 |
| 09 | Game UI Button + MenuList input | ~300 | 8 | ~800 |
| 10 | Config loader | ~200 | 10 | ~200 |
| **Total** | | **~2,630** | **80** | **~7,050** |

## New Files

```
src/runtime/game/rt_entity.c          (~350 LOC)
src/runtime/game/rt_entity.h
src/runtime/game/rt_behavior.c        (~400 LOC)
src/runtime/game/rt_behavior.h
src/runtime/game/rt_raycast2d.c       (~150 LOC)
src/runtime/game/rt_raycast2d.h
src/runtime/game/rt_scenemanager.c    (~250 LOC)
src/runtime/game/rt_scenemanager.h
src/runtime/game/rt_gameui_button.c   (~200 LOC)
src/runtime/game/rt_gameui_button.h
src/runtime/game/rt_gameui_menu.c     (~250 LOC)
src/runtime/game/rt_gameui_menu.h
src/runtime/game/rt_config.c          (~200 LOC)
src/runtime/game/rt_config.h
src/runtime/game/rt_leveldata.h
```

## Modified Files

```
src/runtime/graphics/rt_camera.c       — 3 new methods
src/runtime/graphics/rt_camera.h       — declarations
src/runtime/game/rt_animstate.c        — name field + 4 methods
src/runtime/game/rt_animstate.h        — declarations
src/runtime/game/rt_collision.c        — 2 line intersection functions
src/runtime/graphics/rt_tilemap.c      — tile anim storage + draw modification
src/runtime/graphics/rt_tilemap.h      — declarations
src/runtime/graphics/rt_tilemap_io.c   — level loader
src/il/runtime/runtime.def             — ~120 new entries total
src/il/runtime/RuntimeSignatures.cpp   — new includes
src/il/runtime/classes/RuntimeClasses.hpp — 7 new RTCLS_ entries
src/runtime/CMakeLists.txt             — new source files
src/tests/CMakeLists.txt               — 10 new test registrations
```

## Test Files

```
src/tests/unit/runtime/TestCameraEnhance.cpp    (6 tests)
src/tests/unit/runtime/TestAnimStateNamed.cpp   (6 tests)
src/tests/unit/runtime/TestEntity.cpp           (10 tests)
src/tests/unit/runtime/TestTilemapAnim.cpp      (4 tests)
src/tests/unit/runtime/TestRaycast2D.cpp        (10 tests)
src/tests/unit/runtime/TestBehavior.cpp         (10 tests)
src/tests/unit/runtime/TestLevelData.cpp        (7 tests)
src/tests/unit/runtime/TestSceneManager.cpp     (9 tests)
src/tests/unit/runtime/TestGameMenu.cpp         (8 tests)
src/tests/unit/runtime/TestGameConfig.cpp       (10 tests)
```

## Documentation

```
docs/viperlib/game/entity.md          (new)
docs/viperlib/game/behavior.md        (new)
docs/viperlib/game/raycast.md         (new)
docs/viperlib/game/scenemanager.md    (new)
docs/viperlib/game/leveldata.md       (new)
docs/viperlib/game/ui-menu.md         (new)
docs/viperlib/game/config.md          (new)
docs/viperlib/game/tilemap.md         (update)
docs/viperlib/game/animstatemachine.md (update)
docs/viperlib/graphics/camera.md       (update)
docs/viperlib/game/collision.md        (update)
```
