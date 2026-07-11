//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_quests.h
// Purpose: Viper.Game.Quests — quest/stage/objective tracker with polled
//   progress events and SaveData persistence (plan 29). 2D/3D-agnostic:
//   a pure state machine with no clock, canvas, or scene dependencies.
// Key invariants:
//   - String ids are the stable identity (registration is idempotent on id);
//     bounded shapes trap at registration (32 quests, 16 stages, 8
//     objectives per stage).
//   - Saved state excludes registration data: games re-register at boot,
//     then Load applies id-matched state (unknown saved ids are tolerated).
// Ownership/Lifetime:
//   - GC-managed tracker handle; retained id/text strings; finalizer frees.
// Links: misc/plans/thirdpersonupgrade/29-quest-tracker.md, ADR 0099,
//        src/runtime/game/rt_achievement.h (structural template)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_QUESTS_CLASS_ID INT64_C(-0x510220)

/* Quest states. */
#define RT_QUEST_STATE_HIDDEN 0
#define RT_QUEST_STATE_ACTIVE 1
#define RT_QUEST_STATE_COMPLETE 2
#define RT_QUEST_STATE_FAILED 3

/* Event kinds. */
#define RT_QUEST_EVENT_ACTIVATED 0
#define RT_QUEST_EVENT_OBJECTIVE_PROGRESS 1
#define RT_QUEST_EVENT_OBJECTIVE_COMPLETE 2
#define RT_QUEST_EVENT_STAGE_COMPLETE 3
#define RT_QUEST_EVENT_QUEST_COMPLETE 4
#define RT_QUEST_EVENT_QUEST_FAILED 5

/// @brief Create an empty quest tracker.
void *rt_quests_new(void);

/* Registration (fluent; idempotent on ids). */
void *rt_quests_add_quest(void *tracker, rt_string quest_id, rt_string title);
void *rt_quests_add_stage(void *tracker, rt_string quest_id, rt_string stage_id, rt_string text);
void *rt_quests_add_flag(
    void *tracker, rt_string quest_id, rt_string stage_id, rt_string obj_id, rt_string text);
void *rt_quests_add_counter(void *tracker,
                            rt_string quest_id,
                            rt_string stage_id,
                            rt_string obj_id,
                            rt_string text,
                            int64_t target);

/* Lifecycle. */
int8_t rt_quests_activate(void *tracker, rt_string quest_id);
int8_t rt_quests_fail(void *tracker, rt_string quest_id);
int8_t rt_quests_set_flag(void *tracker, rt_string quest_id, rt_string obj_id);
int8_t rt_quests_progress(void *tracker, rt_string quest_id, rt_string obj_id, int64_t amount);

/* Queries. */
int64_t rt_quests_active_count(void *tracker);
rt_string rt_quests_active_quest(void *tracker, int64_t index);
rt_string rt_quests_quest_title(void *tracker, rt_string quest_id);
int64_t rt_quests_quest_state(void *tracker, rt_string quest_id);
rt_string rt_quests_current_stage_text(void *tracker, rt_string quest_id);
int64_t rt_quests_objective_count(void *tracker, rt_string quest_id);
rt_string rt_quests_objective_text(void *tracker, rt_string quest_id, int64_t index);
int64_t rt_quests_objective_progress(void *tracker, rt_string quest_id, int64_t index);
int64_t rt_quests_objective_target(void *tracker, rt_string quest_id, int64_t index);
int8_t rt_quests_objective_complete(void *tracker, rt_string quest_id, int64_t index);

/* Polled events. */
int64_t rt_quests_event_count(void *tracker);
rt_string rt_quests_event_quest_id(void *tracker, int64_t index);
int64_t rt_quests_event_kind(void *tracker, int64_t index);
void rt_quests_clear_events(void *tracker);
int8_t rt_quests_just_completed(void *tracker, rt_string quest_id);

/* Constant accessors (QuestState / QuestEventKind). */
int64_t rt_quests_state_hidden(void);
int64_t rt_quests_state_active(void);
int64_t rt_quests_state_complete(void);
int64_t rt_quests_state_failed(void);
int64_t rt_quests_event_activated(void);
int64_t rt_quests_event_objective_progress(void);
int64_t rt_quests_event_objective_complete(void);
int64_t rt_quests_event_stage_complete(void);
int64_t rt_quests_event_quest_complete(void);
int64_t rt_quests_event_quest_failed(void);

/* SaveData persistence. */
int8_t rt_quests_save(void *tracker, void *savedata);
int8_t rt_quests_load(void *tracker, void *savedata);

#ifdef __cplusplus
}
#endif
