//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_quests.h
// Purpose: Zanna.Game.Quests — quest/stage/objective tracker with polled
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

/// @brief Register a quest with @p quest_id and display @p title (fluent).
void *rt_quests_add_quest(void *tracker, rt_string quest_id, rt_string title);

/// @brief Append a stage to @p quest_id with journal @p text (fluent).
void *rt_quests_add_stage(void *tracker, rt_string quest_id, rt_string stage_id, rt_string text);

/// @brief Add a boolean (flag) objective to a stage (fluent).
void *rt_quests_add_flag(
    void *tracker, rt_string quest_id, rt_string stage_id, rt_string obj_id, rt_string text);

/// @brief Add a counting objective completing at @p target (fluent).
void *rt_quests_add_counter(void *tracker,
                            rt_string quest_id,
                            rt_string stage_id,
                            rt_string obj_id,
                            rt_string text,
                            int64_t target);

/* Lifecycle. */

/// @brief Move a hidden quest to ACTIVE; queues an ACTIVATED event. Returns 0 on unknown id or wrong state.
int8_t rt_quests_activate(void *tracker, rt_string quest_id);

/// @brief Move an active quest to FAILED; queues a QUEST_FAILED event.
int8_t rt_quests_fail(void *tracker, rt_string quest_id);

/// @brief Complete a flag objective in the current stage (advances stage/quest when all complete).
int8_t rt_quests_set_flag(void *tracker, rt_string quest_id, rt_string obj_id);

/// @brief Add @p amount to a counter objective (clamped at its target); advances on completion.
int8_t rt_quests_progress(void *tracker, rt_string quest_id, rt_string obj_id, int64_t amount);

/* Queries. */

/// @brief Number of quests currently in the ACTIVE state.
int64_t rt_quests_active_count(void *tracker);

/// @brief Id of the @p index-th active quest (0-based; empty when out of range).
rt_string rt_quests_active_quest(void *tracker, int64_t index);

/// @brief Registered display title for @p quest_id (empty when unknown).
rt_string rt_quests_quest_title(void *tracker, rt_string quest_id);

/// @brief Current RT_QUEST_STATE_* value for @p quest_id (HIDDEN when unknown).
int64_t rt_quests_quest_state(void *tracker, rt_string quest_id);

/// @brief Journal text of the quest's current stage (empty when not active).
rt_string rt_quests_current_stage_text(void *tracker, rt_string quest_id);

/// @brief Objective count of the quest's current stage.
int64_t rt_quests_objective_count(void *tracker, rt_string quest_id);

/// @brief Display text of the @p index-th objective in the current stage.
rt_string rt_quests_objective_text(void *tracker, rt_string quest_id, int64_t index);

/// @brief Current progress of the @p index-th objective (0/1 for flags).
int64_t rt_quests_objective_progress(void *tracker, rt_string quest_id, int64_t index);

/// @brief Completion target of the @p index-th objective (1 for flags).
int64_t rt_quests_objective_target(void *tracker, rt_string quest_id, int64_t index);

/// @brief True when the @p index-th objective has reached its target.
int8_t rt_quests_objective_complete(void *tracker, rt_string quest_id, int64_t index);

/* Polled events. */

/// @brief Number of queued progress events since the last clear.
int64_t rt_quests_event_count(void *tracker);

/// @brief Quest id associated with the @p index-th queued event.
rt_string rt_quests_event_quest_id(void *tracker, int64_t index);

/// @brief RT_QUEST_EVENT_* kind of the @p index-th queued event.
int64_t rt_quests_event_kind(void *tracker, int64_t index);

/// @brief Drop all queued events (typically after a UI poll).
void rt_quests_clear_events(void *tracker);

/// @brief True when a QUEST_COMPLETE event for @p quest_id is queued.
int8_t rt_quests_just_completed(void *tracker, rt_string quest_id);

/* Constant accessors (QuestState / QuestEventKind). */

/// @brief RT_QUEST_STATE_HIDDEN as a runtime constant.
int64_t rt_quests_state_hidden(void);

/// @brief RT_QUEST_STATE_ACTIVE as a runtime constant.
int64_t rt_quests_state_active(void);

/// @brief RT_QUEST_STATE_COMPLETE as a runtime constant.
int64_t rt_quests_state_complete(void);

/// @brief RT_QUEST_STATE_FAILED as a runtime constant.
int64_t rt_quests_state_failed(void);

/// @brief RT_QUEST_EVENT_ACTIVATED as a runtime constant.
int64_t rt_quests_event_activated(void);

/// @brief RT_QUEST_EVENT_OBJECTIVE_PROGRESS as a runtime constant.
int64_t rt_quests_event_objective_progress(void);

/// @brief RT_QUEST_EVENT_OBJECTIVE_COMPLETE as a runtime constant.
int64_t rt_quests_event_objective_complete(void);

/// @brief RT_QUEST_EVENT_STAGE_COMPLETE as a runtime constant.
int64_t rt_quests_event_stage_complete(void);

/// @brief RT_QUEST_EVENT_QUEST_COMPLETE as a runtime constant.
int64_t rt_quests_event_quest_complete(void);

/// @brief RT_QUEST_EVENT_QUEST_FAILED as a runtime constant.
int64_t rt_quests_event_quest_failed(void);

/* SaveData persistence. */

/// @brief Serialize quest state (not registration data) into @p savedata.
int8_t rt_quests_save(void *tracker, void *savedata);

/// @brief Apply id-matched saved state onto registered quests (unknown ids tolerated).
int8_t rt_quests_load(void *tracker, void *savedata);

#ifdef __cplusplus
}
#endif
