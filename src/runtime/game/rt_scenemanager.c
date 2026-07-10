//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_scenemanager.c
// Purpose: Multi-scene manager — named scenes, switch, transitions, edge flags.
// Key invariants:
//   - Scene names are unique within one manager and switches never target unknown scenes.
//   - Timed transitions publish edge flags only when the active scene changes.
// Ownership/Lifetime:
//   - Each manager owns its bounded inline scene registry for the manager lifetime.
//   - Returned scene names are runtime-owned immutable strings.
// Links: src/runtime/game/rt_scenemanager.h,
//        src/tests/unit/runtime/TestSceneManager.cpp
//
//===----------------------------------------------------------------------===//

#include "rt_scenemanager.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <string.h>

#define SM_MAX_SCENES 64
#define SM_SCENE_NAME_MAX 128

typedef struct {
    char name[SM_SCENE_NAME_MAX];
    int8_t active;
} sm_scene_t;

typedef struct {
    sm_scene_t scenes[SM_MAX_SCENES];
    int32_t count;
    int32_t current;  // Index into scenes[] (-1 = none)
    int32_t previous; // Previous scene index
    int8_t just_entered;
    int8_t just_exited;
    // Transition
    int8_t transitioning;
    int32_t next_scene;     // Target scene during transition
    int64_t trans_timer;    // Countdown ms
    int64_t trans_duration; // Total duration ms
    int8_t transition_completed;
} scenemanager_impl;

/// @brief Safe-cast a handle to the SceneManager impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p mgr is NULL.
static scenemanager_impl *checked_scenemanager(void *mgr, const char *api) {
    if (!mgr)
        return NULL;
    if (rt_obj_class_id(mgr) != RT_SCENEMANAGER_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (scenemanager_impl *)mgr;
}

/// @brief Copy a runtime-string scene name into a fixed @p out buffer,
///        NUL-terminated and truncated. @return 1 on success, 0 on bad input.
static int scene_name_from_handle(void *name, char out[SM_SCENE_NAME_MAX]) {
    if (!name || !out)
        return 0;
    const char *cname = rt_string_cstr((rt_string)name);
    if (!cname)
        return 0;
    strncpy(out, cname, SM_SCENE_NAME_MAX - 1);
    out[SM_SCENE_NAME_MAX - 1] = '\0';
    return 1;
}

/// @brief Linear-search registered scenes by name.
/// @return The scene's index, or -1 if not found / bad input.
static int find_scene(scenemanager_impl *sm, const char *name) {
    if (!sm || !name)
        return -1;
    for (int32_t i = 0; i < sm->count; i++) {
        if (strcmp(sm->scenes[i].name, name) == 0)
            return i;
    }
    return -1;
}

/// @brief Create a new scene manager for named game state transitions.
/// @details Manages a flat list of named scenes (e.g., "menu", "gameplay", "pause").
///          Supports instant switching and timed transitions with progress tracking.
void *rt_scenemanager_new(void) {
    scenemanager_impl *sm = (scenemanager_impl *)rt_obj_new_i64(RT_SCENEMANAGER_CLASS_ID,
                                                                (int64_t)sizeof(scenemanager_impl));
    if (!sm)
        return NULL;
    memset(sm, 0, sizeof(scenemanager_impl));
    sm->current = -1;
    sm->previous = -1;
    sm->next_scene = -1;
    return sm;
}

/// @brief Register a named scene. The first scene added becomes the current scene.
void rt_scenemanager_add(void *mgr, void *name) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.Add: expected Viper.Game.SceneManager");
    if (!sm || !name)
        return;
    if (sm->count >= SM_MAX_SCENES)
        return;
    char cname[SM_SCENE_NAME_MAX];
    if (!scene_name_from_handle(name, cname))
        return;
    if (find_scene(sm, cname) >= 0)
        return;
    sm_scene_t *s = &sm->scenes[sm->count++];
    strncpy(s->name, cname, SM_SCENE_NAME_MAX - 1);
    s->name[SM_SCENE_NAME_MAX - 1] = '\0';
    s->active = 1;
    // Auto-set first scene as current if none
    if (sm->current < 0) {
        sm->current = sm->count - 1;
        sm->just_entered = 1;
    }
}

/// @brief Instantly switch to a named scene (sets just_entered and just_exited flags).
void rt_scenemanager_switch(void *mgr, void *name) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.Switch: expected Viper.Game.SceneManager");
    if (!sm || !name)
        return;
    char cname[SM_SCENE_NAME_MAX];
    if (!scene_name_from_handle(name, cname))
        return;
    int idx = find_scene(sm, cname);
    if (idx < 0 || idx == sm->current)
        return;
    sm->previous = sm->current;
    sm->current = idx;
    sm->just_entered = 1;
    sm->just_exited = 1;
    sm->transitioning = 0;
    sm->transition_completed = 0;
    sm->next_scene = -1;
    sm->trans_timer = 0;
    sm->trans_duration = 0;
}

/// @brief Begin a timed transition to a new scene (completes after duration_ms).
void rt_scenemanager_switch_transition(void *mgr, void *name, int64_t duration_ms) {
    scenemanager_impl *sm = checked_scenemanager(
        mgr, "SceneManager.SwitchTransition: expected Viper.Game.SceneManager");
    if (!sm || !name)
        return;
    char cname[SM_SCENE_NAME_MAX];
    if (!scene_name_from_handle(name, cname))
        return;
    int idx = find_scene(sm, cname);
    if (idx < 0 || idx == sm->current || (sm->transitioning && idx == sm->next_scene))
        return;
    sm->transitioning = 1;
    sm->transition_completed = 0;
    sm->next_scene = idx;
    sm->trans_duration = duration_ms > 0 ? duration_ms : 1;
    sm->trans_timer = sm->trans_duration;
}

/// @brief Advance the scene manager by dt milliseconds. Clears one-shot flags, completes
/// transitions.
void rt_scenemanager_update(void *mgr, int64_t dt) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.Update: expected Viper.Game.SceneManager");
    if (!sm)
        return;
    sm->transition_completed = 0;
    sm->just_entered = 0;
    sm->just_exited = 0;

    if (sm->transitioning && dt > 0) {
        sm->trans_timer -= dt;
        if (sm->trans_timer <= 0) {
            sm->transitioning = 0;
            sm->transition_completed = 1;
            sm->previous = sm->current;
            sm->current = sm->next_scene;
            sm->next_scene = -1;
            sm->trans_timer = 0;
            sm->just_entered = 1;
            sm->just_exited = 1;
        }
    }
}

/// @brief Get the name of the currently active scene.
void *rt_scenemanager_current(void *mgr) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.Current: expected Viper.Game.SceneManager");
    if (!sm)
        return (void *)rt_const_cstr("");
    if (sm->current >= 0 && sm->current < sm->count)
        return (void *)rt_const_cstr(sm->scenes[sm->current].name);
    return (void *)rt_const_cstr("");
}

/// @brief Get the name of the previously active scene (before the last transition).
void *rt_scenemanager_previous(void *mgr) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.Previous: expected Viper.Game.SceneManager");
    if (!sm)
        return (void *)rt_const_cstr("");
    if (sm->previous >= 0 && sm->previous < sm->count)
        return (void *)rt_const_cstr(sm->scenes[sm->previous].name);
    return (void *)rt_const_cstr("");
}

/// @brief Check whether the current scene matches the given name.
int8_t rt_scenemanager_is_scene(void *mgr, void *name) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.IsScene: expected Viper.Game.SceneManager");
    if (!sm || !name)
        return 0;
    if (sm->current < 0)
        return 0;
    char cname[SM_SCENE_NAME_MAX];
    if (!scene_name_from_handle(name, cname))
        return 0;
    return strcmp(sm->scenes[sm->current].name, cname) == 0;
}

/// @brief Check whether a scene was entered this frame (one-shot, cleared on next update).
int8_t rt_scenemanager_just_entered(void *mgr) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.JustEntered: expected Viper.Game.SceneManager");
    return sm ? sm->just_entered : 0;
}

/// @brief Check whether a scene was exited this frame (one-shot, cleared on next update).
int8_t rt_scenemanager_just_exited(void *mgr) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.JustExited: expected Viper.Game.SceneManager");
    return sm ? sm->just_exited : 0;
}

/// @brief Check whether a timed scene transition is currently in progress.
int8_t rt_scenemanager_is_transitioning(void *mgr) {
    scenemanager_impl *sm =
        checked_scenemanager(mgr, "SceneManager.IsTransitioning: expected Viper.Game.SceneManager");
    return sm ? sm->transitioning : 0;
}

/// @brief Get the transition progress as a ratio (0.0 = start, 1.0 = complete).
double rt_scenemanager_transition_progress(void *mgr) {
    scenemanager_impl *sm = checked_scenemanager(
        mgr, "SceneManager.TransitionProgress: expected Viper.Game.SceneManager");
    if (!sm)
        return 0.0;
    if (sm->transition_completed)
        return 1.0;
    if (!sm->transitioning || sm->trans_duration <= 0)
        return 0.0;
    double elapsed = (double)(sm->trans_duration - sm->trans_timer);
    if (elapsed < 0.0)
        elapsed = 0.0;
    if (elapsed > (double)sm->trans_duration)
        elapsed = (double)sm->trans_duration;
    return elapsed / (double)sm->trans_duration;
}
