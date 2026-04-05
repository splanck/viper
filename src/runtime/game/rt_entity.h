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

void *rt_entity_new(int64_t x, int64_t y, int64_t w, int64_t h);

// Position (centipixels)
int64_t rt_entity_get_x(void *ent);
int64_t rt_entity_get_y(void *ent);
void rt_entity_set_x(void *ent, int64_t x);
void rt_entity_set_y(void *ent, int64_t y);

// Velocity (centipixels per 16ms)
int64_t rt_entity_get_vx(void *ent);
int64_t rt_entity_get_vy(void *ent);
void rt_entity_set_vx(void *ent, int64_t vx);
void rt_entity_set_vy(void *ent, int64_t vy);

// Size (pixels)
int64_t rt_entity_get_width(void *ent);
int64_t rt_entity_get_height(void *ent);

// Direction: 1=right, -1=left
int64_t rt_entity_get_dir(void *ent);
void rt_entity_set_dir(void *ent, int64_t dir);

// HP
int64_t rt_entity_get_hp(void *ent);
void rt_entity_set_hp(void *ent, int64_t hp);
int64_t rt_entity_get_max_hp(void *ent);
void rt_entity_set_max_hp(void *ent, int64_t hp);

// Type ID (user-defined)
int64_t rt_entity_get_type(void *ent);
void rt_entity_set_type(void *ent, int64_t type);

// Active flag
int8_t rt_entity_get_active(void *ent);
void rt_entity_set_active(void *ent, int8_t active);

// Collision flags (set by MoveAndCollide)
int8_t rt_entity_on_ground(void *ent);
int8_t rt_entity_hit_left(void *ent);
int8_t rt_entity_hit_right(void *ent);
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
