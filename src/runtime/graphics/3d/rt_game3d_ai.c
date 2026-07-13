//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_ai.c
// Purpose: NPC AI layers — Perception3D (sight cones with LoS raycasts and
//   enter/exit hysteresis, hearing from World3D.ReportSound stimuli) and a
//   data-built BehaviorTree3D runtime (Sequence/Selector/Inverter composites,
//   CanSee/Wait/MoveTo/Custom leaves with polled custom-leaf resolution),
//   both ticked in the world step before controllers so decisions feed the
//   same step's movement.
// Key invariants:
//   - Deterministic: world-entity-list scan order, fixed hysteresis windows,
//     no wall-clock reads; custom leaves park Running and resume on resolve.
//   - Trees are shared immutable data; per-entity state lives in the
//     BehaviorTreeInstance attached to the entity slot.
// Ownership/Lifetime:
//   - Components hold plain entity backrefs (NULLed at teardown); instances
//     retain their tree; perception retains nothing beyond bookkeeping.
// Links: misc/plans/thirdpersonupgrade/22-ai-perception-bt.md, ADR 0094.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_game3d.h"
#include "rt_game3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_object.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PERCEPTION3D_MAX_TRACKED 16
#define PERCEPTION3D_MAX_HEARD 8
#define BT3D_MAX_NODES 128
#define BT3D_MAX_CHILDREN 8

/*==========================================================================
 * Perception3D
 *=========================================================================*/

typedef struct rt_game3d_percept_track {
    void *entity;      /* plain pointer key; validated against the world list */
    int64_t entity_id; /* stable id guard: a reused heap address never aliases */
    double visible_time;
    double lost_time;
    double last_known[3];
    int8_t seen;
    int8_t touched; /* set when the perception tick saw this entity alive */
} rt_game3d_percept_track;

typedef struct rt_game3d_heard_event {
    double position[3];
    double loudness;
    int64_t tag;
} rt_game3d_heard_event;

typedef struct rt_game3d_perception {
    void *vptr;
    void *entity; /* owner backref */
    double sight_range;
    double fov_degrees;
    double eye_height;
    double hearing_range; /* range at loudness 1; 0 disables hearing */
    int64_t target_mask;  /* entity layer filter */
    int64_t los_mask;
    double time_to_see;
    double time_to_lose;
    rt_game3d_percept_track tracks[PERCEPTION3D_MAX_TRACKED];
    int32_t track_count;
    rt_game3d_heard_event heard[PERCEPTION3D_MAX_HEARD];
    int32_t heard_count;
    int8_t seen_changed; /* one-shot: any seen/lost transition this step */
} rt_game3d_perception;

static void game3d_perception_finalize(void *obj) {
    rt_game3d_perception *sense = (rt_game3d_perception *)obj;
    if (sense)
        sense->entity = NULL;
}

void *rt_game3d_perception_new(void *entity_obj) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_g3d_checked_or_null(entity_obj, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    if (!entity) {
        rt_trap("Game3D.Perception3D.New: invalid entity");
        return NULL;
    }
    rt_game3d_perception *sense = (rt_game3d_perception *)rt_obj_new_i64(
        RT_G3D_GAME3D_PERCEPTION_CLASS_ID, (int64_t)sizeof(rt_game3d_perception));
    if (!sense) {
        rt_trap("Game3D.Perception3D.New: allocation failed");
        return NULL;
    }
    memset(sense, 0, sizeof(*sense));
    rt_obj_set_finalizer(sense, game3d_perception_finalize);
    sense->entity = entity;
    sense->sight_range = 15.0;
    sense->fov_degrees = 110.0;
    sense->eye_height = 1.6;
    sense->target_mask = -1;
    sense->los_mask = -1;
    sense->time_to_see = 0.3;
    sense->time_to_lose = 2.0;
    {
        rt_game3d_perception *previous = (rt_game3d_perception *)rt_g3d_checked_or_null(
            entity->perception, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
        if (previous && previous != sense)
            previous->entity = NULL;
        game3d_assign_typed_ref(&entity->perception, sense, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
    }
    return sense;
}

static rt_game3d_perception *game3d_perception_checked(void *obj, const char *method) {
    rt_game3d_perception *sense =
        (rt_game3d_perception *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
    if (!sense)
        rt_trap(method);
    return sense;
}

void rt_game3d_perception_set_sight(void *obj,
                                    double range,
                                    double fov_degrees,
                                    double eye_height) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SetSight: invalid component");
    if (!sense)
        return;
    if (isfinite(range) && range > 0.0)
        sense->sight_range = range > 512.0 ? 512.0 : range;
    if (isfinite(fov_degrees) && fov_degrees > 1.0)
        sense->fov_degrees = fov_degrees > 360.0 ? 360.0 : fov_degrees;
    if (isfinite(eye_height))
        sense->eye_height = eye_height;
}

void rt_game3d_perception_set_hearing(void *obj, double range_at_loudness1) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SetHearing: invalid component");
    if (sense && isfinite(range_at_loudness1) && range_at_loudness1 >= 0.0)
        sense->hearing_range = range_at_loudness1 > 512.0 ? 512.0 : range_at_loudness1;
}

void rt_game3d_perception_set_target_mask(void *obj, int64_t mask) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SetTargetMask: invalid component");
    if (sense)
        sense->target_mask = mask;
}

void rt_game3d_perception_set_los_mask(void *obj, int64_t mask) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SetLosMask: invalid component");
    if (sense)
        sense->los_mask = mask;
}

int64_t rt_game3d_perception_seen_count(void *obj) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SeenCount: invalid component");
    if (!sense)
        return 0;
    int64_t count = 0;
    for (int32_t i = 0; i < sense->track_count; ++i)
        if (sense->tracks[i].seen)
            count++;
    return count;
}

void *rt_game3d_perception_seen_target(void *obj, int64_t index) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SeenTarget: invalid component");
    if (!sense)
        return NULL;
    int64_t seen_index = 0;
    for (int32_t i = 0; i < sense->track_count; ++i) {
        if (!sense->tracks[i].seen)
            continue;
        if (seen_index == index) {
            rt_game3d_entity *target = (rt_game3d_entity *)rt_g3d_checked_or_null(
                sense->tracks[i].entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
            if (!target || !target->alive)
                return NULL;
            rt_obj_retain_maybe(target);
            return target;
        }
        seen_index++;
    }
    return NULL;
}

/// @brief Last world position where @p target was seen (its live position while seen).
void *rt_game3d_perception_last_known_position(void *obj, void *target) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.LastKnownPosition: invalid component");
    if (!sense)
        return rt_vec3_new(0.0, 0.0, 0.0);
    for (int32_t i = 0; i < sense->track_count; ++i) {
        if (sense->tracks[i].entity == target)
            return rt_vec3_new(sense->tracks[i].last_known[0],
                               sense->tracks[i].last_known[1],
                               sense->tracks[i].last_known[2]);
    }
    return rt_vec3_new(0.0, 0.0, 0.0);
}

/// @brief One-shot: any seen/lost transition since the last call.
int8_t rt_game3d_perception_seen_changed(void *obj) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.SeenChanged: invalid component");
    if (!sense)
        return 0;
    int8_t changed = sense->seen_changed;
    sense->seen_changed = 0;
    return changed;
}

int64_t rt_game3d_perception_heard_count(void *obj) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.HeardCount: invalid component");
    return sense ? sense->heard_count : 0;
}

void *rt_game3d_perception_heard_position(void *obj, int64_t index) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.HeardPosition: invalid component");
    if (!sense || index < 0 || index >= sense->heard_count)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(sense->heard[index].position[0],
                       sense->heard[index].position[1],
                       sense->heard[index].position[2]);
}

int64_t rt_game3d_perception_heard_tag(void *obj, int64_t index) {
    rt_game3d_perception *sense =
        game3d_perception_checked(obj, "Game3D.Perception3D.HeardTag: invalid component");
    if (!sense || index < 0 || index >= sense->heard_count)
        return 0;
    return sense->heard[index].tag;
}

/// @brief Find the existing track slot for @p target, or NULL.
/// @details Slots key on the entity pointer AND its stable id, so a freed
///   entity's heap address reused by a new entity starts from a fresh track
///   instead of inheriting stale seen/last_known state.
static rt_game3d_percept_track *game3d_perception_find_track(rt_game3d_perception *sense,
                                                             const rt_game3d_entity *target) {
    for (int32_t i = 0; i < sense->track_count; ++i)
        if (sense->tracks[i].entity == target && sense->tracks[i].entity_id == target->id)
            return &sense->tracks[i];
    return NULL;
}

/// @brief Create a fresh track slot for @p target (NULL when the table is full).
/// @details Tracks are created lazily — only when a target first becomes
///   visible — so distant never-seen entities cannot hog slots. Dead tracks are
///   compacted away at the end of every perception tick, so the table cannot
///   fill permanently.
static rt_game3d_percept_track *game3d_perception_create_track(rt_game3d_perception *sense,
                                                               rt_game3d_entity *target) {
    if (sense->track_count >= PERCEPTION3D_MAX_TRACKED)
        return NULL;
    rt_game3d_percept_track *track = &sense->tracks[sense->track_count++];
    memset(track, 0, sizeof(*track));
    track->entity = target;
    track->entity_id = target->id;
    return track;
}

/// @brief Drop tracks that the current tick did not touch (dead/removed/filtered
///   entities), compacting the table so slots are always reclaimable.
static void game3d_perception_compact_tracks(rt_game3d_perception *sense) {
    int32_t kept = 0;
    for (int32_t i = 0; i < sense->track_count; ++i) {
        if (!sense->tracks[i].touched)
            continue;
        if (kept != i)
            sense->tracks[kept] = sense->tracks[i];
        kept++;
    }
    sense->track_count = kept;
}

/// @brief Per-step sight update (cone + LoS + hysteresis) for one perceiver.
void game3d_perception_tick(rt_game3d_world *world, rt_game3d_entity *owner, double dt) {
    rt_game3d_perception *sense = (rt_game3d_perception *)rt_g3d_checked_or_null(
        owner->perception, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
    if (!sense)
        return;
    /* Heard events expire every step; World3D.ReportSound refills them. */
    sense->heard_count = 0;
    /* Tracks touched by this tick survive; the rest are compacted away below. */
    for (int32_t i = 0; i < sense->track_count; ++i)
        sense->tracks[i].touched = 0;

    double origin[3];
    if (!game3d_entity_world_position_components(owner, origin))
        return;
    origin[1] += sense->eye_height;
    double forward[3] = {0.0, 0.0, -1.0};
    if (owner->node) {
        double qx, qy, qz, qw;
        if (rt_scene_node3d_get_world_rotation_components(owner->node, &qx, &qy, &qz, &qw)) {
            double x = 0.0, y = 0.0, z = -1.0;
            double tx = 2.0 * (qy * z - qz * y);
            double ty = 2.0 * (qz * x - qx * z);
            double tz = 2.0 * (qx * y - qy * x);
            forward[0] = x + qw * tx + (qy * tz - qz * ty);
            forward[1] = y + qw * ty + (qz * tx - qx * tz);
            forward[2] = z + qw * tz + (qx * ty - qy * tx);
        }
    }
    double cone_cos = cos(sense->fov_degrees * 0.5 * (3.14159265358979323846 / 180.0));

    int32_t count = world->entity_count;
    if (count < 0 || count > world->entity_capacity)
        count = world->entity_capacity > 0 ? world->entity_capacity : 0;
    double sight_range_sq = sense->sight_range * sense->sight_range;
    for (int32_t i = 0; i < count; ++i) {
        rt_game3d_entity *target = world->entities ? world->entities[i] : NULL;
        if (!target || !target->alive || target == owner)
            continue;
        if (sense->target_mask != -1 && (target->layer & sense->target_mask) == 0)
            continue;
        rt_game3d_percept_track *track = game3d_perception_find_track(sense, target);
        /* An untracked target with a full table can neither be recorded nor
         * transition any state: skip all remaining work for it. */
        if (!track && sense->track_count >= PERCEPTION3D_MAX_TRACKED)
            continue;
        double tpos[3];
        if (!game3d_entity_world_position_components(target, tpos))
            continue;
        double to[3] = {tpos[0] - origin[0], tpos[1] - origin[1], tpos[2] - origin[2]};
        double dist_sq = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
        int visible = 0;
        if (isfinite(dist_sq) && dist_sq <= sight_range_sq && dist_sq > 1e-12) {
            double dist = sqrt(dist_sq);
            double align = (to[0] * forward[0] + to[1] * forward[1] + to[2] * forward[2]) / dist;
            if (align >= cone_cos) {
                visible = 1;
                /* Skip the occlusion ray for near-touching targets: the epsilon
                 * pull-back would drive the ray length negative. */
                if (world->physics && dist > 0.05) {
                    void *o = rt_vec3_new(origin[0], origin[1], origin[2]);
                    void *d = rt_vec3_new(to[0] / dist, to[1] / dist, to[2] / dist);
                    if (o && d) {
                        void *hit =
                            rt_world3d_raycast(world->physics, o, d, dist - 0.05, sense->los_mask);
                        if (hit) {
                            /* rt_physics_hit3d_get_body returns a borrowed reference. */
                            void *hit_body = rt_physics_hit3d_get_body(hit);
                            if (hit_body != target->body)
                                visible = 0;
                            game3d_release_ref(&hit);
                        }
                    }
                    game3d_release_ref(&o);
                    game3d_release_ref(&d);
                }
            }
        }
        /* Tracks begin at first visibility; invisible untracked targets carry
         * no state worth storing. */
        if (!track && visible)
            track = game3d_perception_create_track(sense, target);
        if (!track)
            continue;
        track->touched = 1;
        if (visible) {
            track->visible_time += dt;
            track->lost_time = 0.0;
            memcpy(track->last_known, tpos, sizeof(track->last_known));
            if (!track->seen && track->visible_time >= sense->time_to_see) {
                track->seen = 1;
                sense->seen_changed = 1;
            }
        } else {
            track->visible_time = 0.0;
            track->lost_time += dt;
            if (track->seen && track->lost_time >= sense->time_to_lose) {
                track->seen = 0;
                sense->seen_changed = 1;
            }
        }
    }
    game3d_perception_compact_tracks(sense);
}

/// @brief World stimulus: deliver a sound event to every hearing perceiver in range.
void rt_game3d_world_report_sound(void *world_obj, void *position, double loudness, int64_t tag) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.World3D.ReportSound: invalid world");
    if (!world)
        return;
    double pos[3];
    if (!game3d_read_vec3(position, pos, "Game3D.World3D.ReportSound: position must be Vec3"))
        return;
    if (!isfinite(loudness) || loudness <= 0.0)
        return;
    int32_t count = world->entity_count;
    if (count < 0 || count > world->entity_capacity)
        count = world->entity_capacity > 0 ? world->entity_capacity : 0;
    for (int32_t i = 0; i < count; ++i) {
        rt_game3d_entity *entity = world->entities ? world->entities[i] : NULL;
        if (!entity || !entity->alive || !entity->perception)
            continue;
        rt_game3d_perception *sense = (rt_game3d_perception *)rt_g3d_checked_or_null(
            entity->perception, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
        if (!sense || sense->hearing_range <= 0.0 || sense->heard_count >= PERCEPTION3D_MAX_HEARD)
            continue;
        double epos[3];
        if (!game3d_entity_world_position_components(entity, epos))
            continue;
        double dx = pos[0] - epos[0];
        double dy = pos[1] - epos[1];
        double dz = pos[2] - epos[2];
        /* Squared-distance compare: skips a sqrt per entity per sound event. */
        double dist_sq = dx * dx + dy * dy + dz * dz;
        double reach = sense->hearing_range * loudness;
        if (!isfinite(dist_sq) || !isfinite(reach) || reach < 0.0 || dist_sq > reach * reach)
            continue;
        rt_game3d_heard_event *event = &sense->heard[sense->heard_count++];
        memcpy(event->position, pos, sizeof(event->position));
        event->loudness = loudness;
        event->tag = tag;
    }
}

/*==========================================================================
 * BehaviorTree3D — shared immutable tree + per-entity instance
 *=========================================================================*/

enum {
    BT3D_NODE_SEQUENCE = 0,
    BT3D_NODE_SELECTOR = 1,
    BT3D_NODE_INVERTER = 2,
    BT3D_NODE_CAN_SEE = 3,
    BT3D_NODE_WAIT = 4,
    BT3D_NODE_MOVE_TO_TARGET = 5,
    BT3D_NODE_MOVE_TO_LAST_KNOWN = 6,
    BT3D_NODE_CUSTOM = 7,
};

enum {
    BT3D_FAILURE = 0,
    BT3D_SUCCESS = 1,
    BT3D_RUNNING = 2,
};

typedef struct rt_game3d_bt_node {
    int32_t type;
    int32_t children[BT3D_MAX_CHILDREN];
    int32_t child_count;
    double p0;  /* Wait seconds / MoveTo speed */
    double p1;  /* MoveTo arrive distance */
    int64_t i0; /* Custom id */
} rt_game3d_bt_node;

typedef struct rt_game3d_btree {
    void *vptr;
    rt_game3d_bt_node nodes[BT3D_MAX_NODES];
    int32_t node_count;
    int32_t root;
} rt_game3d_btree;

typedef struct rt_game3d_bt_instance {
    void *vptr;
    void *entity; /* owner backref */
    void *tree;   /* retained BehaviorTree3D */
    void *target; /* plain entity pointer (validated against liveness) */
    double timers[BT3D_MAX_NODES];
    int32_t running_child[BT3D_MAX_NODES];
    int8_t custom_pending[BT3D_MAX_NODES];
    int8_t custom_result[BT3D_MAX_NODES]; /* 0 none, 1 success, 2 failure */
    int64_t pending_custom_id;            /* 0 = none; polled by the game */
    int32_t pending_custom_node;
} rt_game3d_bt_instance;

static void game3d_btree_finalize(void *obj) {
    (void)obj;
}

void *rt_game3d_btree_new(void) {
    rt_game3d_btree *tree = (rt_game3d_btree *)rt_obj_new_i64(RT_G3D_GAME3D_BTREE_CLASS_ID,
                                                              (int64_t)sizeof(rt_game3d_btree));
    if (!tree) {
        rt_trap("Game3D.BehaviorTree3D.New: allocation failed");
        return NULL;
    }
    memset(tree, 0, sizeof(*tree));
    rt_obj_set_finalizer(tree, game3d_btree_finalize);
    tree->root = -1;
    return tree;
}

static rt_game3d_btree *game3d_btree_checked(void *obj, const char *method) {
    rt_game3d_btree *tree =
        (rt_game3d_btree *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_BTREE_CLASS_ID);
    if (!tree)
        rt_trap(method);
    return tree;
}

static int64_t game3d_btree_add_node(rt_game3d_btree *tree, int32_t type) {
    if (!tree || tree->node_count >= BT3D_MAX_NODES) {
        rt_trap("Game3D.BehaviorTree3D: node budget (128) exceeded");
        return -1;
    }
    rt_game3d_bt_node *node = &tree->nodes[tree->node_count];
    memset(node, 0, sizeof(*node));
    node->type = type;
    return tree->node_count++;
}

int64_t rt_game3d_btree_sequence(void *obj) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.Sequence: invalid tree");
    return tree ? game3d_btree_add_node(tree, BT3D_NODE_SEQUENCE) : -1;
}

int64_t rt_game3d_btree_selector(void *obj) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.Selector: invalid tree");
    return tree ? game3d_btree_add_node(tree, BT3D_NODE_SELECTOR) : -1;
}

int64_t rt_game3d_btree_inverter(void *obj) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.Inverter: invalid tree");
    return tree ? game3d_btree_add_node(tree, BT3D_NODE_INVERTER) : -1;
}

int64_t rt_game3d_btree_can_see(void *obj) {
    rt_game3d_btree *tree = game3d_btree_checked(obj, "Game3D.BehaviorTree3D.CanSee: invalid tree");
    return tree ? game3d_btree_add_node(tree, BT3D_NODE_CAN_SEE) : -1;
}

int64_t rt_game3d_btree_wait(void *obj, double seconds) {
    rt_game3d_btree *tree = game3d_btree_checked(obj, "Game3D.BehaviorTree3D.Wait: invalid tree");
    if (!tree)
        return -1;
    int64_t node = game3d_btree_add_node(tree, BT3D_NODE_WAIT);
    if (node >= 0)
        tree->nodes[node].p0 = isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
    return node;
}

int64_t rt_game3d_btree_move_to_target(void *obj, double speed, double arrive_distance) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.MoveToTarget: invalid tree");
    if (!tree)
        return -1;
    int64_t node = game3d_btree_add_node(tree, BT3D_NODE_MOVE_TO_TARGET);
    if (node >= 0) {
        tree->nodes[node].p0 = isfinite(speed) && speed > 0.0 ? speed : 2.0;
        tree->nodes[node].p1 =
            isfinite(arrive_distance) && arrive_distance > 0.0 ? arrive_distance : 0.5;
    }
    return node;
}

int64_t rt_game3d_btree_move_to_last_known(void *obj, double speed, double arrive_distance) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.MoveToLastKnown: invalid tree");
    if (!tree)
        return -1;
    int64_t node = game3d_btree_add_node(tree, BT3D_NODE_MOVE_TO_LAST_KNOWN);
    if (node >= 0) {
        tree->nodes[node].p0 = isfinite(speed) && speed > 0.0 ? speed : 2.0;
        tree->nodes[node].p1 =
            isfinite(arrive_distance) && arrive_distance > 0.0 ? arrive_distance : 0.5;
    }
    return node;
}

int64_t rt_game3d_btree_custom(void *obj, int64_t id) {
    rt_game3d_btree *tree = game3d_btree_checked(obj, "Game3D.BehaviorTree3D.Custom: invalid tree");
    if (!tree)
        return -1;
    int64_t node = game3d_btree_add_node(tree, BT3D_NODE_CUSTOM);
    if (node >= 0)
        tree->nodes[node].i0 = id;
    return node;
}

void rt_game3d_btree_add_child(void *obj, int64_t parent, int64_t child) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.AddChild: invalid tree");
    if (!tree || parent < 0 || parent >= tree->node_count || child < 0 ||
        child >= tree->node_count || parent == child)
        return;
    rt_game3d_bt_node *node = &tree->nodes[parent];
    if (node->child_count >= BT3D_MAX_CHILDREN) {
        rt_trap("Game3D.BehaviorTree3D.AddChild: child budget (8) exceeded");
        return;
    }
    node->children[node->child_count++] = (int32_t)child;
}

void rt_game3d_btree_set_root(void *obj, int64_t node) {
    rt_game3d_btree *tree =
        game3d_btree_checked(obj, "Game3D.BehaviorTree3D.SetRoot: invalid tree");
    if (tree && node >= 0 && node < tree->node_count)
        tree->root = (int32_t)node;
}

/*--------------------------------------------------------------------------
 * Instance
 *-------------------------------------------------------------------------*/

static void game3d_bt_instance_finalize(void *obj) {
    rt_game3d_bt_instance *instance = (rt_game3d_bt_instance *)obj;
    if (!instance)
        return;
    instance->entity = NULL;
    game3d_release_ref(&instance->tree);
}

void *rt_game3d_bt_instance_new(void *entity_obj, void *tree_obj) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_g3d_checked_or_null(entity_obj, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    rt_game3d_btree *tree =
        (rt_game3d_btree *)rt_g3d_checked_or_null(tree_obj, RT_G3D_GAME3D_BTREE_CLASS_ID);
    if (!entity || !tree || tree->root < 0) {
        rt_trap("Game3D.BehaviorTreeInstance3D.New: entity and a rooted tree required");
        return NULL;
    }
    rt_game3d_bt_instance *instance = (rt_game3d_bt_instance *)rt_obj_new_i64(
        RT_G3D_GAME3D_BTINSTANCE_CLASS_ID, (int64_t)sizeof(rt_game3d_bt_instance));
    if (!instance) {
        rt_trap("Game3D.BehaviorTreeInstance3D.New: allocation failed");
        return NULL;
    }
    memset(instance, 0, sizeof(*instance));
    rt_obj_set_finalizer(instance, game3d_bt_instance_finalize);
    instance->entity = entity;
    rt_obj_retain_maybe(tree_obj);
    instance->tree = tree_obj;
    instance->pending_custom_node = -1;
    {
        rt_game3d_bt_instance *previous = (rt_game3d_bt_instance *)rt_g3d_checked_or_null(
            entity->btree, RT_G3D_GAME3D_BTINSTANCE_CLASS_ID);
        if (previous && previous != instance)
            previous->entity = NULL;
        game3d_assign_typed_ref(&entity->btree, instance, RT_G3D_GAME3D_BTINSTANCE_CLASS_ID);
    }
    return instance;
}

static rt_game3d_bt_instance *game3d_bt_instance_checked(void *obj, const char *method) {
    rt_game3d_bt_instance *instance =
        (rt_game3d_bt_instance *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_BTINSTANCE_CLASS_ID);
    if (!instance)
        rt_trap(method);
    return instance;
}

void rt_game3d_bt_instance_set_target(void *obj, void *target_entity) {
    rt_game3d_bt_instance *instance = game3d_bt_instance_checked(
        obj, "Game3D.BehaviorTreeInstance3D.SetTarget: invalid instance");
    if (!instance)
        return;
    rt_game3d_entity *target =
        (rt_game3d_entity *)rt_g3d_checked_or_null(target_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    instance->target = target;
}

/// @brief Pending Custom-leaf id awaiting game resolution (0 = none).
int64_t rt_game3d_bt_instance_pending_custom(void *obj) {
    rt_game3d_bt_instance *instance = game3d_bt_instance_checked(
        obj, "Game3D.BehaviorTreeInstance3D.get_PendingCustom: invalid instance");
    return instance ? instance->pending_custom_id : 0;
}

/// @brief Resolve the pending Custom leaf (1 = success, 0 = failure).
void rt_game3d_bt_instance_resolve(void *obj, int8_t success) {
    rt_game3d_bt_instance *instance =
        game3d_bt_instance_checked(obj, "Game3D.BehaviorTreeInstance3D.Resolve: invalid instance");
    if (!instance || instance->pending_custom_node < 0)
        return;
    instance->custom_result[instance->pending_custom_node] = success ? 1 : 2;
    instance->custom_pending[instance->pending_custom_node] = 0;
    instance->pending_custom_id = 0;
    instance->pending_custom_node = -1;
}

static int game3d_bt_move_toward(
    rt_game3d_entity *entity, const double target[3], double speed, double arrive, double dt) {
    double pos[3];
    if (!game3d_entity_world_position_components(entity, pos))
        return BT3D_FAILURE;
    double to[3] = {target[0] - pos[0], target[1] - pos[1], target[2] - pos[2]};
    double dist = sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (!isfinite(dist) || dist <= arrive)
        return BT3D_SUCCESS;
    double step = speed * dt;
    if (step >= dist)
        step = dist;
    rt_game3d_entity_set_position(entity,
                                  pos[0] + to[0] / dist * step,
                                  pos[1] + to[1] / dist * step,
                                  pos[2] + to[2] / dist * step);
    return BT3D_RUNNING;
}

static int game3d_bt_tick_node(rt_game3d_world *world,
                               rt_game3d_bt_instance *instance,
                               rt_game3d_btree *tree,
                               int32_t node_index,
                               double dt);

static int game3d_bt_tick_composite(rt_game3d_world *world,
                                    rt_game3d_bt_instance *instance,
                                    rt_game3d_btree *tree,
                                    int32_t node_index,
                                    double dt,
                                    int stop_on) {
    rt_game3d_bt_node *node = &tree->nodes[node_index];
    int32_t start = instance->running_child[node_index];
    if (start < 0 || start >= node->child_count)
        start = 0;
    for (int32_t c = start; c < node->child_count; ++c) {
        int status = game3d_bt_tick_node(world, instance, tree, node->children[c], dt);
        if (status == BT3D_RUNNING) {
            instance->running_child[node_index] = c;
            return BT3D_RUNNING;
        }
        if (status == stop_on) {
            instance->running_child[node_index] = 0;
            return status;
        }
    }
    instance->running_child[node_index] = 0;
    return stop_on == BT3D_FAILURE ? BT3D_SUCCESS : BT3D_FAILURE;
}

static int game3d_bt_tick_node(rt_game3d_world *world,
                               rt_game3d_bt_instance *instance,
                               rt_game3d_btree *tree,
                               int32_t node_index,
                               double dt) {
    if (node_index < 0 || node_index >= tree->node_count)
        return BT3D_FAILURE;
    rt_game3d_bt_node *node = &tree->nodes[node_index];
    rt_game3d_entity *owner =
        (rt_game3d_entity *)rt_g3d_checked_or_null(instance->entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
    if (!owner)
        return BT3D_FAILURE;
    switch (node->type) {
        case BT3D_NODE_SEQUENCE:
            return game3d_bt_tick_composite(world, instance, tree, node_index, dt, BT3D_FAILURE);
        case BT3D_NODE_SELECTOR:
            return game3d_bt_tick_composite(world, instance, tree, node_index, dt, BT3D_SUCCESS);
        case BT3D_NODE_INVERTER: {
            if (node->child_count < 1)
                return BT3D_FAILURE;
            int status = game3d_bt_tick_node(world, instance, tree, node->children[0], dt);
            if (status == BT3D_RUNNING)
                return BT3D_RUNNING;
            return status == BT3D_SUCCESS ? BT3D_FAILURE : BT3D_SUCCESS;
        }
        case BT3D_NODE_CAN_SEE: {
            rt_game3d_perception *sense = (rt_game3d_perception *)rt_g3d_checked_or_null(
                owner->perception, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
            if (!sense || !instance->target)
                return BT3D_FAILURE;
            for (int32_t i = 0; i < sense->track_count; ++i)
                if (sense->tracks[i].entity == instance->target && sense->tracks[i].seen)
                    return BT3D_SUCCESS;
            return BT3D_FAILURE;
        }
        case BT3D_NODE_WAIT: {
            instance->timers[node_index] += dt;
            if (instance->timers[node_index] >= node->p0) {
                instance->timers[node_index] = 0.0;
                return BT3D_SUCCESS;
            }
            return BT3D_RUNNING;
        }
        case BT3D_NODE_MOVE_TO_TARGET: {
            rt_game3d_entity *target = (rt_game3d_entity *)rt_g3d_checked_or_null(
                instance->target, RT_G3D_GAME3D_ENTITY_CLASS_ID);
            if (!target || !target->alive)
                return BT3D_FAILURE;
            double tpos[3];
            if (!game3d_entity_world_position_components(target, tpos))
                return BT3D_FAILURE;
            return game3d_bt_move_toward(owner, tpos, node->p0, node->p1, dt);
        }
        case BT3D_NODE_MOVE_TO_LAST_KNOWN: {
            rt_game3d_perception *sense = (rt_game3d_perception *)rt_g3d_checked_or_null(
                owner->perception, RT_G3D_GAME3D_PERCEPTION_CLASS_ID);
            if (!sense || !instance->target)
                return BT3D_FAILURE;
            for (int32_t i = 0; i < sense->track_count; ++i) {
                if (sense->tracks[i].entity == instance->target)
                    return game3d_bt_move_toward(
                        owner, sense->tracks[i].last_known, node->p0, node->p1, dt);
            }
            return BT3D_FAILURE;
        }
        case BT3D_NODE_CUSTOM: {
            if (instance->custom_result[node_index] != 0) {
                int status = instance->custom_result[node_index] == 1 ? BT3D_SUCCESS : BT3D_FAILURE;
                instance->custom_result[node_index] = 0;
                return status;
            }
            if (!instance->custom_pending[node_index] && instance->pending_custom_node < 0) {
                instance->custom_pending[node_index] = 1;
                instance->pending_custom_id = node->i0;
                instance->pending_custom_node = node_index;
            }
            return BT3D_RUNNING;
        }
        default:
            return BT3D_FAILURE;
    }
}

/// @brief Per-step AI tick for one entity: perception first, then the tree.
void game3d_ai_tick(rt_game3d_world *world, rt_game3d_entity *entity, double dt) {
    if (entity->perception)
        game3d_perception_tick(world, entity, dt);
    if (entity->btree) {
        rt_game3d_bt_instance *instance = (rt_game3d_bt_instance *)rt_g3d_checked_or_null(
            entity->btree, RT_G3D_GAME3D_BTINSTANCE_CLASS_ID);
        rt_game3d_btree *tree = instance ? (rt_game3d_btree *)rt_g3d_checked_or_null(
                                               instance->tree, RT_G3D_GAME3D_BTREE_CLASS_ID)
                                         : NULL;
        if (instance && tree && tree->root >= 0)
            (void)game3d_bt_tick_node(world, instance, tree, tree->root, dt);
    }
}

#else
typedef int rt_game3d_ai_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
