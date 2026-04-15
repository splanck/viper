//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_scenemanager.c
// Purpose: Multi-scene manager — named scenes, switch, transitions, edge flags.
//
//===----------------------------------------------------------------------===//

#include "rt_scenemanager.h"
#include "rt_object.h"
#include "rt_string.h"

#include <string.h>

#define SM_MAX_SCENES 16

typedef struct {
    char name[32];
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
} scenemanager_impl;

static scenemanager_impl *get(void *mgr) {
    return (scenemanager_impl *)mgr;
}

static int find_scene(scenemanager_impl *sm, const char *name) {
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
    scenemanager_impl *sm =
        (scenemanager_impl *)rt_obj_new_i64(0, (int64_t)sizeof(scenemanager_impl));
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
    if (!mgr || !name)
        return;
    scenemanager_impl *sm = get(mgr);
    if (sm->count >= SM_MAX_SCENES)
        return;
    const char *cname = rt_string_cstr((rt_string)name);
    if (!cname)
        return;
    sm_scene_t *s = &sm->scenes[sm->count++];
    strncpy(s->name, cname, 31);
    s->name[31] = '\0';
    s->active = 1;
    // Auto-set first scene as current if none
    if (sm->current < 0) {
        sm->current = sm->count - 1;
        sm->just_entered = 1;
    }
}

/// @brief Instantly switch to a named scene (sets just_entered and just_exited flags).
void rt_scenemanager_switch(void *mgr, void *name) {
    if (!mgr || !name)
        return;
    scenemanager_impl *sm = get(mgr);
    const char *cname = rt_string_cstr((rt_string)name);
    int idx = find_scene(sm, cname);
    if (idx < 0 || idx == sm->current)
        return;
    sm->previous = sm->current;
    sm->current = idx;
    sm->just_entered = 1;
    sm->just_exited = 1;
    sm->transitioning = 0;
}

/// @brief Begin a timed transition to a new scene (completes after duration_ms).
void rt_scenemanager_switch_transition(void *mgr, void *name, int64_t duration_ms) {
    if (!mgr || !name)
        return;
    scenemanager_impl *sm = get(mgr);
    const char *cname = rt_string_cstr((rt_string)name);
    int idx = find_scene(sm, cname);
    if (idx < 0)
        return;
    sm->transitioning = 1;
    sm->next_scene = idx;
    sm->trans_duration = duration_ms > 0 ? duration_ms : 1;
    sm->trans_timer = sm->trans_duration;
}

/// @brief Advance the scene manager by dt milliseconds. Clears one-shot flags, completes
/// transitions.
void rt_scenemanager_update(void *mgr, int64_t dt) {
    if (!mgr)
        return;
    scenemanager_impl *sm = get(mgr);
    sm->just_entered = 0;
    sm->just_exited = 0;

    if (sm->transitioning) {
        sm->trans_timer -= dt;
        if (sm->trans_timer <= 0) {
            sm->transitioning = 0;
            sm->previous = sm->current;
            sm->current = sm->next_scene;
            sm->next_scene = -1;
            sm->just_entered = 1;
            sm->just_exited = 1;
        }
    }
}

/// @brief Get the name of the currently active scene.
void *rt_scenemanager_current(void *mgr) {
    if (!mgr)
        return (void *)rt_const_cstr("");
    scenemanager_impl *sm = get(mgr);
    if (sm->current >= 0 && sm->current < sm->count)
        return (void *)rt_const_cstr(sm->scenes[sm->current].name);
    return (void *)rt_const_cstr("");
}

/// @brief Get the name of the previously active scene (before the last transition).
void *rt_scenemanager_previous(void *mgr) {
    if (!mgr)
        return (void *)rt_const_cstr("");
    scenemanager_impl *sm = get(mgr);
    if (sm->previous >= 0 && sm->previous < sm->count)
        return (void *)rt_const_cstr(sm->scenes[sm->previous].name);
    return (void *)rt_const_cstr("");
}

/// @brief Check whether the current scene matches the given name.
int8_t rt_scenemanager_is_scene(void *mgr, void *name) {
    if (!mgr || !name)
        return 0;
    scenemanager_impl *sm = get(mgr);
    if (sm->current < 0)
        return 0;
    const char *cname = rt_string_cstr((rt_string)name);
    return strcmp(sm->scenes[sm->current].name, cname) == 0;
}

/// @brief Check whether a scene was entered this frame (one-shot, cleared on next update).
int8_t rt_scenemanager_just_entered(void *mgr) {
    return mgr ? get(mgr)->just_entered : 0;
}

/// @brief Check whether a scene was exited this frame (one-shot, cleared on next update).
int8_t rt_scenemanager_just_exited(void *mgr) {
    return mgr ? get(mgr)->just_exited : 0;
}

/// @brief Check whether a timed scene transition is currently in progress.
int8_t rt_scenemanager_is_transitioning(void *mgr) {
    return mgr ? get(mgr)->transitioning : 0;
}

/// @brief Get the transition progress as a ratio (0.0 = start, 1.0 = complete).
double rt_scenemanager_transition_progress(void *mgr) {
    if (!mgr)
        return 0.0;
    scenemanager_impl *sm = get(mgr);
    if (!sm->transitioning || sm->trans_duration <= 0)
        return 0.0;
    double elapsed = (double)(sm->trans_duration - sm->trans_timer);
    return elapsed / (double)sm->trans_duration;
}
