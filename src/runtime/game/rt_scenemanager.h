//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_scenemanager.h
// Purpose: Multi-scene manager with named scenes and transitions.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a multi-scene manager with no active scene.
void *rt_scenemanager_new(void);
/// @brief Register a named scene (pass any rt_string for name).
void rt_scenemanager_add(void *mgr, void *name);
/// @brief Switch immediately to the named scene (no transition).
void rt_scenemanager_switch(void *mgr, void *name);
/// @brief Switch to the named scene with a fade transition over @p duration_ms.
void rt_scenemanager_switch_transition(void *mgr, void *name, int64_t duration_ms);
/// @brief Tick the manager by @p dt milliseconds (advances any active transition).
void rt_scenemanager_update(void *mgr, int64_t dt);
/// @brief Get the name of the currently-active scene (NULL if none).
void *rt_scenemanager_current(void *mgr);
/// @brief Get the name of the scene that was active before the current one.
void *rt_scenemanager_previous(void *mgr);
/// @brief True if the named scene matches the current scene.
int8_t rt_scenemanager_is_scene(void *mgr, void *name);
/// @brief One-shot flag: true for one frame after a scene becomes active.
int8_t rt_scenemanager_just_entered(void *mgr);
/// @brief One-shot flag: true for one frame after a scene is left.
int8_t rt_scenemanager_just_exited(void *mgr);
/// @brief True if a fade transition is currently animating.
int8_t rt_scenemanager_is_transitioning(void *mgr);
/// @brief Transition progress as a fraction (0.0 = just started, 1.0 = complete).
double rt_scenemanager_transition_progress(void *mgr);

#ifdef __cplusplus
}
#endif
