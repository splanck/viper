//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_entity.c
// Purpose: Lightweight 2D game entity with built-in tilemap collision.
//          Extracts the common gravity + moveAndCollide + collision response
//          pattern used by every game object in platformers/sidescrollers.
//
// Key invariants:
//   - Position in centipixels (x100). Collision in pixel coordinates.
//   - MoveAndCollide resolves X axis first, then Y axis.
//   - Collision flags reset at start of each MoveAndCollide call.
//   - DT_BASE = 16 (milliseconds per base frame at 60fps).
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64; no finalizer.
//
// Links: rt_entity.h, misc/plans/game/03-entity-class.md
//
//===----------------------------------------------------------------------===//

#include "rt_entity.h"
#include "rt_object.h"
#include "rt_tilemap.h"
#include "rt_trap.h"

#include <limits.h>
#include <string.h>

#define DT_BASE 16

//=============================================================================
// Internal struct
//=============================================================================

typedef struct {
    int64_t x, y;          // Centipixels (x100)
    int64_t vx, vy;        // Centipixels per DT_BASE ms
    int64_t width, height; // Pixels
    int64_t dir;           // 1 = right, -1 = left
    int64_t hp, max_hp;
    int64_t type;
    int8_t active;
    int8_t on_ground;
    int8_t hit_left, hit_right, hit_ceiling;
} entity_impl;

static entity_impl *get(void *ent) {
    return (entity_impl *)ent;
}

static entity_impl *checked_entity(void *ent, const char *api) {
    if (!ent)
        return NULL;
    if (rt_obj_class_id(ent) != RT_ENTITY_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return get(ent);
}

static int64_t entity_floor_div(int64_t value, int64_t divisor) {
    if (divisor == 0)
        return 0;
    int64_t quot = value / divisor;
    int64_t rem = value % divisor;
    if (rem != 0 && ((rem < 0) != (divisor < 0)))
        quot--;
    return quot;
}

static int64_t entity_saturating_add(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

static int64_t entity_saturating_mul(int64_t a, int64_t b) {
#if defined(__SIZEOF_INT128__)
    __int128 result = (__int128)a * (__int128)b;
    if (result > INT64_MAX)
        return INT64_MAX;
    if (result < INT64_MIN)
        return INT64_MIN;
    return (int64_t)result;
#else
    long double result = (long double)a * (long double)b;
    if (result > (long double)INT64_MAX)
        return INT64_MAX;
    if (result < (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)result;
#endif
}

static int64_t entity_scaled_delta(int64_t velocity, int64_t dt) {
#if defined(__SIZEOF_INT128__)
    __int128 result = (__int128)velocity * (__int128)dt;
    if (result < 0)
        result = -((-result) >> 4);
    else
        result >>= 4;
    if (result > INT64_MAX)
        return INT64_MAX;
    if (result < INT64_MIN)
        return INT64_MIN;
    return (int64_t)result;
#else
    return entity_saturating_mul(velocity, dt) / DT_BASE;
#endif
}

static int64_t entity_cp_to_px(int64_t cp) {
    return entity_floor_div(cp, 100);
}

static int8_t entity_range_overlaps(int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
    return a0 < b1 && a1 > b0;
}

//=============================================================================
// Constructor
//=============================================================================

/// @brief Create a new 2D game entity with position, bounding box, and physics state.
/// @details Entities are lightweight game objects with position (in centipixels),
///          velocity, direction, HP, collision flags, and active state. They support
///          tilemap-based physics via move_and_collide (axis-separated collision
///          resolution) and helper methods for patrol AI.
void *rt_entity_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    entity_impl *e =
        (entity_impl *)rt_obj_new_i64(RT_ENTITY_CLASS_ID, (int64_t)sizeof(entity_impl));
    if (!e)
        return NULL;
    memset(e, 0, sizeof(entity_impl));
    e->x = x;
    e->y = y;
    e->width = w > 0 ? w : 1;
    e->height = h > 0 ? h : 1;
    e->dir = 1;
    e->active = 1;
    return e;
}

//=============================================================================
// Property accessors
//
// All getters/setters below operate directly on the rt_entity_impl struct.
// Position (x, y) and velocity (vx, vy) are stored in *centipixels* (1/100 px)
// for sub-pixel precision in the integer integrator. Width/height are in pixels.
// `dir` is the facing direction (-1 = left, +1 = right). Each accessor returns
// 0 / no-op for a NULL handle to keep call sites trap-free.
//=============================================================================

/// @brief Read X position in centipixels (divide by 100 to get pixels).
int64_t rt_entity_get_x(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.X: expected Viper.Game.Entity");
    return e ? e->x : 0;
}

/// @brief Read Y position in centipixels.
int64_t rt_entity_get_y(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Y: expected Viper.Game.Entity");
    return e ? e->y : 0;
}

/// @brief Set X position in centipixels (teleport — bypasses collision).
void rt_entity_set_x(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.X.set: expected Viper.Game.Entity");
    if (e)
        e->x = v;
}

/// @brief Set Y position in centipixels.
void rt_entity_set_y(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.Y.set: expected Viper.Game.Entity");
    if (e)
        e->y = v;
}

/// @brief Read X velocity in centipixels per dt unit.
int64_t rt_entity_get_vx(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.VX: expected Viper.Game.Entity");
    return e ? e->vx : 0;
}

/// @brief Read Y velocity in centipixels per dt unit.
int64_t rt_entity_get_vy(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.VY: expected Viper.Game.Entity");
    return e ? e->vy : 0;
}

/// @brief Set X velocity in centipixels per dt.
void rt_entity_set_vx(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.VX.set: expected Viper.Game.Entity");
    if (e)
        e->vx = v;
}

/// @brief Set Y velocity in centipixels per dt.
void rt_entity_set_vy(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.VY.set: expected Viper.Game.Entity");
    if (e)
        e->vy = v;
}

/// @brief Read entity bounding-box width in pixels (set on construction).
int64_t rt_entity_get_width(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Width: expected Viper.Game.Entity");
    return e ? e->width : 0;
}

/// @brief Read entity bounding-box height in pixels.
int64_t rt_entity_get_height(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Height: expected Viper.Game.Entity");
    return e ? e->height : 0;
}

/// @brief Read facing direction (-1 = left, +1 = right). Defaults to +1 for NULL.
int64_t rt_entity_get_dir(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Dir: expected Viper.Game.Entity");
    return e ? e->dir : 1;
}

/// @brief Set facing direction. Used by sprite-mirroring and patrol/turn logic.
void rt_entity_set_dir(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.Dir.set: expected Viper.Game.Entity");
    if (e)
        e->dir = v < 0 ? -1 : 1;
}

/// @brief Read current hit-point count.
int64_t rt_entity_get_hp(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.HP: expected Viper.Game.Entity");
    return e ? e->hp : 0;
}

/// @brief Set current hit-point count (no clamping — caller responsible for capping at max_hp).
void rt_entity_set_hp(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.HP.set: expected Viper.Game.Entity");
    if (e)
        e->hp = v;
}

/// @brief Read maximum hit-point cap.
int64_t rt_entity_get_max_hp(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.MaxHP: expected Viper.Game.Entity");
    return e ? e->max_hp : 0;
}

/// @brief Set the max hit-point cap.
void rt_entity_set_max_hp(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.MaxHP.set: expected Viper.Game.Entity");
    if (e)
        e->max_hp = v;
}

/// @brief Read user-defined entity type tag (e.g., 0=player, 1=enemy, 2=pickup). Game-specific.
int64_t rt_entity_get_type(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Type: expected Viper.Game.Entity");
    return e ? e->type : 0;
}

/// @brief Set the entity type tag.
void rt_entity_set_type(void *ent, int64_t v) {
    entity_impl *e = checked_entity(ent, "Entity.Type.set: expected Viper.Game.Entity");
    if (e)
        e->type = v;
}

/// @brief Returns 1 if the entity is currently active (participates in update/draw).
int8_t rt_entity_get_active(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.Active: expected Viper.Game.Entity");
    return e ? e->active : 0;
}

/// @brief Toggle active flag. Inactive entities should be skipped by game-loop systems.
void rt_entity_set_active(void *ent, int8_t v) {
    entity_impl *e = checked_entity(ent, "Entity.Active.set: expected Viper.Game.Entity");
    if (e)
        e->active = v ? 1 : 0;
}

/// @brief Last-collision flag: 1 if the entity is touching a solid tile below.
int8_t rt_entity_on_ground(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.OnGround: expected Viper.Game.Entity");
    return e ? e->on_ground : 0;
}

/// @brief Last-collision flag: 1 if the entity bumped a solid tile on its left side.
int8_t rt_entity_hit_left(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.HitLeft: expected Viper.Game.Entity");
    return e ? e->hit_left : 0;
}

/// @brief Last-collision flag: 1 if the entity bumped a solid tile on its right side.
int8_t rt_entity_hit_right(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.HitRight: expected Viper.Game.Entity");
    return e ? e->hit_right : 0;
}

/// @brief Last-collision flag: 1 if the entity bumped a solid tile above (head bonk).
int8_t rt_entity_hit_ceiling(void *ent) {
    entity_impl *e = checked_entity(ent, "Entity.HitCeiling: expected Viper.Game.Entity");
    return e ? e->hit_ceiling : 0;
}

//=============================================================================
// Physics: Gravity
//=============================================================================

static int8_t entity_vertical_edge_hits(
    void *tilemap, int64_t edge_px, int64_t top_px, int64_t bottom_px, int64_t tile_h) {
    int64_t top_row = entity_floor_div(top_px, tile_h);
    int64_t bottom_row = entity_floor_div(bottom_px, tile_h);
    for (int64_t row = top_row; row <= bottom_row; ++row) {
        if (rt_tilemap_is_solid_at(tilemap, edge_px, entity_saturating_mul(row, tile_h)))
            return 1;
    }
    return 0;
}

static int8_t entity_horizontal_edge_hits(
    void *tilemap, int64_t left_px, int64_t right_px, int64_t edge_px, int64_t tile_w) {
    int64_t left_col = entity_floor_div(left_px, tile_w);
    int64_t right_col = entity_floor_div(right_px, tile_w);
    for (int64_t col = left_col; col <= right_col; ++col) {
        if (rt_tilemap_is_solid_at(tilemap, entity_saturating_mul(col, tile_w), edge_px))
            return 1;
    }
    return 0;
}

static void entity_sweep_x(
    entity_impl *e, void *tilemap, int64_t delta_cp, int64_t tw, int64_t th) {
    if (delta_cp == 0)
        return;

    int64_t old_x = e->x;
    int64_t target_x = entity_saturating_add(e->x, delta_cp);
    int64_t y_px = entity_cp_to_px(e->y);
    int64_t top_px = y_px;
    int64_t bottom_px = entity_saturating_add(y_px, e->height - 1);

    int64_t old_left = entity_cp_to_px(old_x);
    int64_t new_left = entity_cp_to_px(target_x);
    if (delta_cp > 0) {
        int64_t old_right = entity_saturating_add(old_left, e->width - 1);
        int64_t new_right = entity_saturating_add(new_left, e->width - 1);
        int64_t first_col = entity_floor_div(old_right, tw) + 1;
        int64_t last_col = entity_floor_div(new_right, tw);
        for (int64_t col = first_col; col <= last_col; ++col) {
            int64_t edge_px = entity_saturating_mul(col, tw);
            if (entity_vertical_edge_hits(tilemap, edge_px, top_px, bottom_px, th)) {
                e->x = entity_saturating_mul(edge_px - e->width, 100);
                e->vx = 0;
                e->hit_right = 1;
                return;
            }
        }
    } else {
        int64_t old_col = entity_floor_div(old_left, tw);
        int64_t new_col = entity_floor_div(new_left, tw);
        for (int64_t col = old_col - 1; col >= new_col; --col) {
            int64_t edge_px = entity_saturating_add(entity_saturating_mul(col + 1, tw), -1);
            if (entity_vertical_edge_hits(tilemap, edge_px, top_px, bottom_px, th)) {
                e->x = entity_saturating_mul(entity_saturating_mul(col + 1, tw), 100);
                e->vx = 0;
                e->hit_left = 1;
                return;
            }
            if (col == INT64_MIN)
                break;
        }
    }

    e->x = target_x;
}

static void entity_sweep_y(
    entity_impl *e, void *tilemap, int64_t delta_cp, int64_t tw, int64_t th) {
    if (delta_cp == 0)
        return;

    int64_t old_y = e->y;
    int64_t target_y = entity_saturating_add(e->y, delta_cp);
    int64_t x_px = entity_cp_to_px(e->x);
    int64_t left_px = x_px;
    int64_t right_px = entity_saturating_add(x_px, e->width - 1);

    int64_t old_top = entity_cp_to_px(old_y);
    int64_t new_top = entity_cp_to_px(target_y);
    if (delta_cp > 0) {
        int64_t old_bottom = entity_saturating_add(old_top, e->height - 1);
        int64_t new_bottom = entity_saturating_add(new_top, e->height - 1);
        int64_t first_row = entity_floor_div(old_bottom, th) + 1;
        int64_t last_row = entity_floor_div(new_bottom, th);
        for (int64_t row = first_row; row <= last_row; ++row) {
            int64_t edge_px = entity_saturating_mul(row, th);
            if (entity_horizontal_edge_hits(tilemap, left_px, right_px, edge_px, tw)) {
                e->y = entity_saturating_mul(edge_px - e->height, 100);
                e->vy = 0;
                e->on_ground = 1;
                return;
            }
        }
    } else {
        int64_t old_row = entity_floor_div(old_top, th);
        int64_t new_row = entity_floor_div(new_top, th);
        for (int64_t row = old_row - 1; row >= new_row; --row) {
            int64_t edge_px = entity_saturating_add(entity_saturating_mul(row + 1, th), -1);
            if (entity_horizontal_edge_hits(tilemap, left_px, right_px, edge_px, tw)) {
                e->y = entity_saturating_mul(entity_saturating_mul(row + 1, th), 100);
                e->vy = 0;
                e->hit_ceiling = 1;
                return;
            }
            if (row == INT64_MIN)
                break;
        }
    }

    e->y = target_y;
}

/// @brief Apply downward gravitational acceleration, clamped to max_fall terminal velocity.
void rt_entity_apply_gravity(void *ent, int64_t gravity, int64_t max_fall, int64_t dt) {
    entity_impl *e = checked_entity(ent, "Entity.ApplyGravity: expected Viper.Game.Entity");
    if (!e || dt <= 0)
        return;
    e->vy = entity_saturating_add(e->vy, entity_scaled_delta(gravity, dt));
    if (e->vy > max_fall)
        e->vy = max_fall;
}

//=============================================================================
// Physics: MoveAndCollide (tilemap)
//=============================================================================

/// @brief Move the entity by its velocity and resolve tilemap collisions per axis.
/// @details Moves X first, then Y. For each axis, checks the leading edge tiles
///          for solidity and pushes the entity out if overlapping. Sets collision
///          flags (on_ground, hit_left, hit_right, hit_ceiling) and zeroes the
///          velocity component on collision. Positions are in centipixels (÷100 for px).
void rt_entity_move_and_collide(void *ent, void *tilemap, int64_t dt) {
    entity_impl *e = checked_entity(ent, "Entity.MoveAndCollide: expected Viper.Game.Entity");
    if (!e || dt <= 0)
        return;

    // Reset collision flags
    e->on_ground = 0;
    e->hit_left = 0;
    e->hit_right = 0;
    e->hit_ceiling = 0;

    if (!tilemap) {
        // No tilemap: just move
        e->x = entity_saturating_add(e->x, entity_scaled_delta(e->vx, dt));
        e->y = entity_saturating_add(e->y, entity_scaled_delta(e->vy, dt));
        return;
    }

    int64_t tw = rt_tilemap_get_tile_width(tilemap);
    int64_t th = rt_tilemap_get_tile_height(tilemap);
    if (tw <= 0 || th <= 0)
        return;

    int64_t dispX = entity_scaled_delta(e->vx, dt);
    int64_t dispY = entity_scaled_delta(e->vy, dt);

    entity_sweep_x(e, tilemap, dispX, tw, th);
    entity_sweep_y(e, tilemap, dispY, tw, th);
}

//=============================================================================
// Physics: Combined gravity + move + collide
//=============================================================================

/// @brief Apply gravity then move-and-collide in one call (convenience wrapper).
void rt_entity_update_physics(
    void *ent, void *tilemap, int64_t gravity, int64_t max_fall, int64_t dt) {
    rt_entity_apply_gravity(ent, gravity, max_fall, dt);
    rt_entity_move_and_collide(ent, tilemap, dt);
}

//=============================================================================
// AI helpers
//=============================================================================

/// @brief Check whether the entity is at a platform edge (no solid tile below in facing direction).
int8_t rt_entity_at_edge(void *ent, void *tilemap) {
    entity_impl *e = checked_entity(ent, "Entity.AtEdge: expected Viper.Game.Entity");
    if (!e || !tilemap)
        return 0;
    int64_t px = entity_cp_to_px(e->x);
    int64_t py = entity_cp_to_px(e->y);
    int64_t checkX =
        (e->dir > 0) ? entity_saturating_add(px, e->width) : entity_saturating_add(px, -1);
    int64_t checkY = entity_saturating_add(entity_saturating_add(py, e->height), 2);
    return !rt_tilemap_is_solid_at(tilemap, checkX, checkY);
}

/// @brief Reverse direction when hitting a wall (check hit_left/hit_right flags).
void rt_entity_patrol_reverse(void *ent, int64_t speed) {
    entity_impl *e = checked_entity(ent, "Entity.PatrolReverse: expected Viper.Game.Entity");
    if (!e)
        return;
    if (e->hit_left) {
        e->vx = speed;
        e->dir = 1;
    }
    if (e->hit_right) {
        e->vx = -speed;
        e->dir = -1;
    }
}

/// @brief Test whether two entities' bounding boxes overlap (AABB collision test).
int8_t rt_entity_overlaps(void *ent, void *other) {
    entity_impl *a = checked_entity(ent, "Entity.Overlaps: expected Viper.Game.Entity");
    entity_impl *b = checked_entity(other, "Entity.Overlaps: expected Viper.Game.Entity");
    if (!a || !b)
        return 0;
    int64_t ax = entity_cp_to_px(a->x);
    int64_t ay = entity_cp_to_px(a->y);
    int64_t bx = entity_cp_to_px(b->x);
    int64_t by = entity_cp_to_px(b->y);
    int64_t ar = entity_saturating_add(ax, a->width);
    int64_t ab = entity_saturating_add(ay, a->height);
    int64_t br = entity_saturating_add(bx, b->width);
    int64_t bb = entity_saturating_add(by, b->height);
    return entity_range_overlaps(ax, ar, bx, br) && entity_range_overlaps(ay, ab, by, bb);
}
