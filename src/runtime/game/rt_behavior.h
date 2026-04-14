//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_behavior.h
// Purpose: Composable behavior presets for 2D game AI. Combines common patterns
//          (patrol, chase, gravity, edge reverse, shoot, animation) into a
//          single Update() call that operates on an Entity + Tilemap.
//
// Key invariants:
//   - Behaviors are flag-based (bitmask). Multiple can be active simultaneously.
//   - Update() applies all enabled behaviors in a fixed order.
//   - Requires Entity (Plan 03) for position/velocity/collision.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create an empty behavior bundle (no behaviors enabled yet).
void *rt_behavior_new(void);
/// @brief Enable horizontal patrol — moves at @p speed, reversing on edges/walls if those bits set.
void rt_behavior_add_patrol(void *bhv, int64_t speed);
/// @brief Enable chase — moves toward target at @p speed when within @p range pixels.
void rt_behavior_add_chase(void *bhv, int64_t speed, int64_t range);
/// @brief Enable gravity — accelerates downward by @p gravity per tick, capped at @p max_fall.
void rt_behavior_add_gravity(void *bhv, int64_t gravity, int64_t max_fall);
/// @brief Enable edge-reverse — flips facing/velocity when about to walk off a tile edge.
void rt_behavior_add_edge_reverse(void *bhv);
/// @brief Enable wall-reverse — flips facing/velocity when blocked by a tile wall.
void rt_behavior_add_wall_reverse(void *bhv);
/// @brief Enable periodic shooting with @p cooldown_ms between shots (use ShootReady to query).
void rt_behavior_add_shoot(void *bhv, int64_t cooldown_ms);
/// @brief Enable sine-wave vertical hover with the given amplitude (pixels) and speed (deg/sec).
void rt_behavior_add_sine_float(void *bhv, int64_t amplitude, int64_t speed);
/// @brief Enable looping sprite animation with @p frame_count frames at @p ms_per_frame each.
void rt_behavior_add_anim_loop(void *bhv, int64_t frame_count, int64_t ms_per_frame);

/// @brief Apply all enabled behaviors to an entity.
/// target_x/y: player position (for Chase/Shoot). Pass 0,0 if unused.
void rt_behavior_update(
    void *bhv, void *entity, void *tilemap, int64_t target_x, int64_t target_y, int64_t dt);

/// @brief One-shot flag (consumed on read): true when the shoot cooldown has elapsed.
int8_t rt_behavior_shoot_ready(void *bhv);
/// @brief Get the current frame index of the looping animation (0..frame_count-1).
int64_t rt_behavior_anim_frame(void *bhv);

#ifdef __cplusplus
}
#endif
