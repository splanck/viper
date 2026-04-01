# Plan 08: SceneManager with Transitions

## Context

XENOSCAPE's game.zia (1220 lines) is a central "god object" with 10 state branches
and 23 subsystems. Scene/SceneNode exist for rendering hierarchies but there's no
multi-scene manager for switching between title/gameplay/pause/gameover screens.

## Design

SceneManager owns multiple named "game screens" and handles transitions between them.
Each screen is a callback-based lifecycle (no subclassing — Zia doesn't have virtual
dispatch across module boundaries).

The approach: SceneManager stores state IDs and the game checks which scene is active.
This is simpler than a callback system and works with Zia's current capabilities.

## Changes

### New file: `src/runtime/game/rt_scenemanager.c` (~250 LOC)

**SceneManager struct:**
```c
#define SM_MAX_SCENES 16

typedef struct {
    char name[32];
    int64_t id;
    int8_t active;
} sm_scene_entry;

typedef struct rt_scenemanager_impl {
    sm_scene_entry scenes[SM_MAX_SCENES];
    int32_t scene_count;
    int64_t current_scene;     // Current scene ID (-1 if none)
    int64_t previous_scene;    // For transition tracking
    int8_t transitioning;      // Currently in transition
    int64_t transition_timer;  // Countdown
    int64_t transition_duration;
    int64_t next_scene;        // Scene to switch to after transition
    int8_t just_entered;       // Edge flag: just entered new scene
    int8_t just_exited;        // Edge flag: just exited previous scene
} rt_scenemanager_impl;
```

**Functions:**
```c
void *rt_scenemanager_new(void);
void rt_scenemanager_add(void *mgr, rt_string name);
void rt_scenemanager_switch(void *mgr, rt_string name);
void rt_scenemanager_switch_with_transition(void *mgr, rt_string name, int64_t duration_ms);
void rt_scenemanager_update(void *mgr, int64_t dt);

// State queries
rt_string rt_scenemanager_current(void *mgr);
rt_string rt_scenemanager_previous(void *mgr);
int8_t rt_scenemanager_is_scene(void *mgr, rt_string name);
int8_t rt_scenemanager_just_entered(void *mgr);
int8_t rt_scenemanager_just_exited(void *mgr);
int8_t rt_scenemanager_is_transitioning(void *mgr);
double rt_scenemanager_transition_progress(void *mgr); // 0.0 to 1.0
```

**Update logic:**
1. Clear just_entered/just_exited flags
2. If transitioning: decrement timer, when done switch current → next_scene
3. Set edge flags on scene change

### runtime.def
```
RT_CLASS_BEGIN("Viper.Game.SceneManager", SceneManager, "obj", SceneManagerNew)
    RT_METHOD("Add",                "void(str)",            SceneManagerAdd)
    RT_METHOD("Switch",             "void(str)",            SceneManagerSwitch)
    RT_METHOD("SwitchWithTransition","void(str,i64)",       SceneManagerSwitchTrans)
    RT_METHOD("Update",             "void(i64)",            SceneManagerUpdate)
    RT_PROP("Current",              "str",  SceneManagerCurrent, none)
    RT_PROP("Previous",             "str",  SceneManagerPrevious, none)
    RT_METHOD("IsScene",            "i1(str)",              SceneManagerIsScene)
    RT_PROP("JustEntered",          "i1",   SceneManagerJustEntered, none)
    RT_PROP("JustExited",           "i1",   SceneManagerJustExited, none)
    RT_PROP("Transitioning",        "i1",   SceneManagerTransitioning, none)
    RT_PROP("TransitionProgress",   "f64",  SceneManagerTransProgress, none)
RT_CLASS_END()
```

### Zia usage
```zia
var scenes = SceneManager.New()
scenes.Add("menu")
scenes.Add("playing")
scenes.Add("paused")
scenes.Add("gameover")
scenes.Switch("menu")

// Main loop:
scenes.Update(dt)

if scenes.IsScene("menu") {
    if scenes.get_JustEntered() { initMenu() }
    updateMenu()
    drawMenu(canvas)
}
if scenes.IsScene("playing") {
    if scenes.get_JustEntered() { loadLevel(currentLevel) }
    updateGameplay(dt)
    drawGameplay(canvas)
}
if scenes.IsScene("paused") {
    drawGameplay(canvas)  // Draw game underneath
    drawPauseOverlay(canvas)
}

// Transition with fade:
scenes.SwitchWithTransition("playing", 500)
if scenes.get_Transitioning() {
    var p = scenes.get_TransitionProgress()  // 0.0 → 1.0
    canvas.BoxAlpha(0, 0, SCREEN_W, SCREEN_H, 0x000000, (p * 255) as Integer)
}
```

### Files to modify
- New: `src/runtime/game/rt_scenemanager.c` (~250 LOC)
- New: `src/runtime/game/rt_scenemanager.h` (~40 LOC)
- `src/il/runtime/runtime.def` — ~13 entries
- `src/il/runtime/RuntimeSignatures.cpp` — include header
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_SceneManager
- `src/runtime/CMakeLists.txt` — add source

### Tests

**File:** `src/tests/unit/runtime/TestSceneManager.cpp`
```
TEST(SceneManager, AddAndSwitch)
TEST(SceneManager, JustEnteredOnSwitch)
TEST(SceneManager, JustExitedOnSwitch)
TEST(SceneManager, CurrentAndPrevious)
TEST(SceneManager, IsSceneQuery)
TEST(SceneManager, TransitionTimerCountdown)
TEST(SceneManager, TransitionProgressRamp)
TEST(SceneManager, SwitchDuringTransitionOverrides)
TEST(SceneManager, UnknownSceneNoOp)
```

### Doc update
- New: `docs/viperlib/game/scenemanager.md`
