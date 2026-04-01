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

void *rt_scenemanager_new(void);
void rt_scenemanager_add(void *mgr, void *name);
void rt_scenemanager_switch(void *mgr, void *name);
void rt_scenemanager_switch_transition(void *mgr, void *name, int64_t duration_ms);
void rt_scenemanager_update(void *mgr, int64_t dt);
void *rt_scenemanager_current(void *mgr);
void *rt_scenemanager_previous(void *mgr);
int8_t rt_scenemanager_is_scene(void *mgr, void *name);
int8_t rt_scenemanager_just_entered(void *mgr);
int8_t rt_scenemanager_just_exited(void *mgr);
int8_t rt_scenemanager_is_transitioning(void *mgr);
double rt_scenemanager_transition_progress(void *mgr);

#ifdef __cplusplus
}
#endif
