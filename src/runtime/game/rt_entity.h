//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_entity.h
// Purpose: Lightweight 2D game entity combining position, velocity, size,
//          health, and built-in tilemap collision resolution. Replaces the
//          common pattern of parallel arrays + manual gravity/moveAndCollide.
//
// Key invariants:
//   - Position (x, y) is in centipixels (x100 for subpixel precision).
//   - Velocity (vx, vy) is in centipixels per 16ms base frame.
//   - Size (width, height) is in pixels.
//   - Collision flags (on_ground, hit_left, hit_right, hit_ceiling) are set
//     by MoveAndCollide/UpdatePhysics and valid until the next call.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: rt_entity.c (implementation), misc/plans/game/03-entity-class.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 2D entity at pixel coordinates (x, y) with size (w × h) pixels.
/// Position is internally stored in centipixels (×100); HP defaults to 1.
void *rt_entity_new(int64_t x, int64_t y, int64_t w, int64_t h);

// Position (centipixels)
/// @brief Get the entity's X position in centipixels.
int64_t rt_entity_get_x(void *ent);
/// @brief Get the entity's Y position in centipixels.
int64_t rt_entity_get_y(void *ent);
/// @brief Set the entity's X position in centipixels.
void rt_entity_set_x(void *ent, int64_t x);
/// @brief Set the entity's Y position in centipixels.
void rt_entity_set_y(void *ent, int64_t y);

// Velocity (centipixels per 16ms)
/// @brief Get horizontal velocity in centipixels per 16ms base frame.
int64_t rt_entity_get_vx(void *ent);
/// @brief Get vertical velocity in centipixels per 16ms (positive = downward).
int64_t rt_entity_get_vy(void *ent);
/// @brief Set horizontal velocity.
void rt_entity_set_vx(void *ent, int64_t vx);
/// @brief Set vertical velocity.
void rt_entity_set_vy(void *ent, int64_t vy);

// Size (pixels)
/// @brief Get the entity's collision width in pixels.
int64_t rt_entity_get_width(void *ent);
/// @brief Get the entity's collision height in pixels.
int64_t rt_entity_get_height(void *ent);

// Direction: 1=right, -1=left
/// @brief Get the entity's facing direction (1 = right, -1 = left).
int64_t rt_entity_get_dir(void *ent);
/// @brief Set the entity's facing direction (clamped to +1 / -1).
void rt_entity_set_dir(void *ent, int64_t dir);

// HP
/// @brief Get the current HP (typically clamped to 0..MaxHP).
int64_t rt_entity_get_hp(void *ent);
/// @brief Set the current HP.
void rt_entity_set_hp(void *ent, int64_t hp);
/// @brief Get the maximum HP cap.
int64_t rt_entity_get_max_hp(void *ent);
/// @brief Set the maximum HP cap (does not change current HP).
void rt_entity_set_max_hp(void *ent, int64_t hp);

// Type ID (user-defined)
/// @brief Get the user-defined entity type ID (e.g., player=0, enemy=1, pickup=2).
int64_t rt_entity_get_type(void *ent);
/// @brief Set the user-defined entity type ID.
void rt_entity_set_type(void *ent, int64_t type);

// Active flag
/// @brief Get the active flag (inactive entities are typically skipped by update/draw).
int8_t rt_entity_get_active(void *ent);
/// @brief Set the active flag.
void rt_entity_set_active(void *ent, int8_t active);

// Collision flags (set by MoveAndCollide)
/// @brief True if the entity is currently standing on a solid tile (set by MoveAndCollide).
int8_t rt_entity_on_ground(void *ent);
/// @brief True if the entity collided with a wall to its left during the last MoveAndCollide.
int8_t rt_entity_hit_left(void *ent);
/// @brief True if the entity collided with a wall to its right during the last MoveAndCollide.
int8_t rt_entity_hit_right(void *ent);
/// @brief True if the entity collided with a ceiling during the last MoveAndCollide.
int8_t rt_entity_hit_ceiling(void *ent);

/// @brief Apply gravity to vy, capped at max_fall. Call once per frame.
void rt_entity_apply_gravity(void *ent, int64_t gravity, int64_t max_fall, int64_t dt);

/// @brief Move by velocity*dt, resolve against tilemap. Sets collision flags.
void rt_entity_move_and_collide(void *ent, void *tilemap, int64_t dt);

/// @brief Apply gravity + move + collide in one call.
void rt_entity_update_physics(
    void *ent, void *tilemap, int64_t gravity, int64_t max_fall, int64_t dt);

/// @brief Check if entity is at a platform edge (no solid tile below leading edge).
int8_t rt_entity_at_edge(void *ent, void *tilemap);

/// @brief Reverse direction on wall hit. Sets vx to ±speed based on dir.
void rt_entity_patrol_reverse(void *ent, int64_t speed);

/// @brief Check AABB overlap with another entity.
int8_t rt_entity_overlaps(void *ent, void *other);

#ifdef __cplusplus
}
#endif
