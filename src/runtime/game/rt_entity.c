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

#include <string.h>

// Tilemap query externs
extern int64_t rt_tilemap_get_tile_width(void *tm);
extern int64_t rt_tilemap_get_tile_height(void *tm);
extern int64_t rt_tilemap_get_width(void *tm);
extern int64_t rt_tilemap_get_height(void *tm);
extern int8_t rt_tilemap_is_solid_at(void *tm, int64_t px, int64_t py);

#define DT_BASE 16

//=============================================================================
// Internal struct
//=============================================================================

typedef struct {
    int64_t x, y;           // Centipixels (x100)
    int64_t vx, vy;         // Centipixels per DT_BASE ms
    int64_t width, height;  // Pixels
    int64_t dir;            // 1 = right, -1 = left
    int64_t hp, max_hp;
    int64_t type;
    int8_t active;
    int8_t on_ground;
    int8_t hit_left, hit_right, hit_ceiling;
} entity_impl;

static entity_impl *get(void *ent) {
    return (entity_impl *)ent;
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
    entity_impl *e = (entity_impl *)rt_obj_new_i64(0, (int64_t)sizeof(entity_impl));
    if (!e)
        return NULL;
    memset(e, 0, sizeof(entity_impl));
    e->x = x;
    e->y = y;
    e->width = w;
    e->height = h;
    e->dir = 1;
    e->active = 1;
    return e;
}

//=============================================================================
// Property accessors
//=============================================================================

int64_t rt_entity_get_x(void *ent) { return ent ? get(ent)->x : 0; }
int64_t rt_entity_get_y(void *ent) { return ent ? get(ent)->y : 0; }
void rt_entity_set_x(void *ent, int64_t v) { if (ent) get(ent)->x = v; }
void rt_entity_set_y(void *ent, int64_t v) { if (ent) get(ent)->y = v; }

int64_t rt_entity_get_vx(void *ent) { return ent ? get(ent)->vx : 0; }
int64_t rt_entity_get_vy(void *ent) { return ent ? get(ent)->vy : 0; }
void rt_entity_set_vx(void *ent, int64_t v) { if (ent) get(ent)->vx = v; }
void rt_entity_set_vy(void *ent, int64_t v) { if (ent) get(ent)->vy = v; }

int64_t rt_entity_get_width(void *ent) { return ent ? get(ent)->width : 0; }
int64_t rt_entity_get_height(void *ent) { return ent ? get(ent)->height : 0; }

int64_t rt_entity_get_dir(void *ent) { return ent ? get(ent)->dir : 1; }
void rt_entity_set_dir(void *ent, int64_t v) { if (ent) get(ent)->dir = v; }

int64_t rt_entity_get_hp(void *ent) { return ent ? get(ent)->hp : 0; }
void rt_entity_set_hp(void *ent, int64_t v) { if (ent) get(ent)->hp = v; }
int64_t rt_entity_get_max_hp(void *ent) { return ent ? get(ent)->max_hp : 0; }
void rt_entity_set_max_hp(void *ent, int64_t v) { if (ent) get(ent)->max_hp = v; }

int64_t rt_entity_get_type(void *ent) { return ent ? get(ent)->type : 0; }
void rt_entity_set_type(void *ent, int64_t v) { if (ent) get(ent)->type = v; }

int8_t rt_entity_get_active(void *ent) { return ent ? get(ent)->active : 0; }
void rt_entity_set_active(void *ent, int8_t v) { if (ent) get(ent)->active = v; }

int8_t rt_entity_on_ground(void *ent) { return ent ? get(ent)->on_ground : 0; }
int8_t rt_entity_hit_left(void *ent) { return ent ? get(ent)->hit_left : 0; }
int8_t rt_entity_hit_right(void *ent) { return ent ? get(ent)->hit_right : 0; }
int8_t rt_entity_hit_ceiling(void *ent) { return ent ? get(ent)->hit_ceiling : 0; }

//=============================================================================
// Physics: Gravity
//=============================================================================

/// @brief Apply downward gravitational acceleration, clamped to max_fall terminal velocity.
void rt_entity_apply_gravity(void *ent, int64_t gravity, int64_t max_fall, int64_t dt) {
    if (!ent)
        return;
    entity_impl *e = get(ent);
    e->vy = e->vy + gravity * dt / DT_BASE;
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
    if (!ent)
        return;
    entity_impl *e = get(ent);

    // Reset collision flags
    e->on_ground = 0;
    e->hit_left = 0;
    e->hit_right = 0;
    e->hit_ceiling = 0;

    if (!tilemap) {
        // No tilemap: just move
        e->x += e->vx * dt / DT_BASE;
        e->y += e->vy * dt / DT_BASE;
        return;
    }

    int64_t tw = rt_tilemap_get_tile_width(tilemap);
    int64_t th = rt_tilemap_get_tile_height(tilemap);
    if (tw <= 0 || th <= 0)
        return;

    int64_t dispX = e->vx * dt / DT_BASE;
    int64_t dispY = e->vy * dt / DT_BASE;

    // Convert to pixel coords for collision (centipixels / 100)
    int64_t px = e->x / 100;
    int64_t py = e->y / 100;
    int64_t w = e->width;
    int64_t h = e->height;

    // ── Resolve X axis ──
    int64_t newPx = px + dispX / 100;

    if (dispX > 0) {
        // Moving right: check right edge
        int64_t rightEdge = newPx + w - 1;
        int64_t topTile = py / th;
        int64_t botTile = (py + h - 1) / th;
        int64_t tileCol = rightEdge / tw;
        for (int64_t row = topTile; row <= botTile; row++) {
            if (rt_tilemap_is_solid_at(tilemap, rightEdge, row * th)) {
                newPx = tileCol * tw - w;
                e->vx = 0;
                e->hit_right = 1;
                break;
            }
        }
    } else if (dispX < 0) {
        // Moving left: check left edge
        int64_t leftEdge = newPx;
        int64_t topTile = py / th;
        int64_t botTile = (py + h - 1) / th;
        int64_t tileCol = leftEdge / tw;
        for (int64_t row = topTile; row <= botTile; row++) {
            if (rt_tilemap_is_solid_at(tilemap, leftEdge, row * th)) {
                newPx = (tileCol + 1) * tw;
                e->vx = 0;
                e->hit_left = 1;
                break;
            }
        }
    }

    // ── Resolve Y axis ──
    int64_t newPy = py + dispY / 100;

    if (dispY > 0) {
        // Moving down: check bottom edge
        int64_t botEdge = newPy + h - 1;
        int64_t leftTile = newPx / tw;
        int64_t rightTile = (newPx + w - 1) / tw;
        int64_t tileRow = botEdge / th;
        for (int64_t col = leftTile; col <= rightTile; col++) {
            if (rt_tilemap_is_solid_at(tilemap, col * tw, botEdge)) {
                newPy = tileRow * th - h;
                e->vy = 0;
                e->on_ground = 1;
                break;
            }
        }
    } else if (dispY < 0) {
        // Moving up: check top edge
        int64_t topEdge = newPy;
        int64_t leftTile = newPx / tw;
        int64_t rightTile = (newPx + w - 1) / tw;
        int64_t tileRow = topEdge / th;
        for (int64_t col = leftTile; col <= rightTile; col++) {
            if (rt_tilemap_is_solid_at(tilemap, col * tw, topEdge)) {
                newPy = (tileRow + 1) * th;
                e->vy = 0;
                e->hit_ceiling = 1;
                break;
            }
        }
    }

    // Write back in centipixels
    e->x = newPx * 100;
    e->y = newPy * 100;
}

//=============================================================================
// Physics: Combined gravity + move + collide
//=============================================================================

/// @brief Apply gravity then move-and-collide in one call (convenience wrapper).
void rt_entity_update_physics(void *ent, void *tilemap,
                              int64_t gravity, int64_t max_fall, int64_t dt) {
    rt_entity_apply_gravity(ent, gravity, max_fall, dt);
    rt_entity_move_and_collide(ent, tilemap, dt);
}

//=============================================================================
// AI helpers
//=============================================================================

/// @brief Check whether the entity is at a platform edge (no solid tile below in facing direction).
int8_t rt_entity_at_edge(void *ent, void *tilemap) {
    if (!ent || !tilemap)
        return 0;
    entity_impl *e = get(ent);
    int64_t px = e->x / 100;
    int64_t py = e->y / 100;
    int64_t checkX = (e->dir > 0) ? (px + e->width) : (px - 1);
    int64_t checkY = py + e->height + 2;
    return !rt_tilemap_is_solid_at(tilemap, checkX, checkY);
}

/// @brief Reverse direction when hitting a wall (check hit_left/hit_right flags).
void rt_entity_patrol_reverse(void *ent, int64_t speed) {
    if (!ent)
        return;
    entity_impl *e = get(ent);
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
    if (!ent || !other)
        return 0;
    entity_impl *a = get(ent);
    entity_impl *b = get(other);
    int64_t ax = a->x / 100, ay = a->y / 100;
    int64_t bx = b->x / 100, by = b->y / 100;
    return ax < bx + b->width && ax + a->width > bx &&
           ay < by + b->height && ay + a->height > by;
}
