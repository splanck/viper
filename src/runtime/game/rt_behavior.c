//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_behavior.c
// Purpose: Composable behavior presets for 2D game AI.
//
// Key invariants:
//   - Behaviors are flag-based bitmask, applied in fixed order.
//   - Update order: gravity → patrol/chase → move+collide → wall/edge reverse
//     → shoot cooldown → sine float → animation.
//
//===----------------------------------------------------------------------===//

#include "rt_behavior.h"
#include "rt_entity.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BHV_PATROL       (1u << 0)
#define BHV_CHASE        (1u << 1)
#define BHV_GRAVITY      (1u << 2)
#define BHV_EDGE_REVERSE (1u << 3)
#define BHV_WALL_REVERSE (1u << 4)
#define BHV_SHOOT        (1u << 5)
#define BHV_SINE_FLOAT   (1u << 6)
#define BHV_ANIM_LOOP    (1u << 7)

typedef struct {
    uint32_t flags;

    // Patrol
    int64_t patrol_speed;

    // Chase
    int64_t chase_speed;
    int64_t chase_range; // Pixels (not centipixels)

    // Gravity
    int64_t gravity;
    int64_t max_fall;

    // Shoot
    int64_t shoot_cooldown_ms;
    int64_t shoot_timer;
    int8_t shoot_ready;

    // Sine float
    int64_t float_amplitude;
    int64_t float_speed;
    int64_t float_phase; // Accumulator (degrees * 100)

    // Animation
    int64_t anim_frames;
    int64_t anim_ms;
    int64_t anim_timer;
    int64_t anim_frame;
} behavior_impl;

static behavior_impl *get(void *bhv) {
    return (behavior_impl *)bhv;
}

void *rt_behavior_new(void) {
    behavior_impl *b = (behavior_impl *)rt_obj_new_i64(0, (int64_t)sizeof(behavior_impl));
    if (!b)
        return NULL;
    memset(b, 0, sizeof(behavior_impl));
    return b;
}

void rt_behavior_add_patrol(void *bhv, int64_t speed) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_PATROL;
    b->patrol_speed = speed;
}

void rt_behavior_add_chase(void *bhv, int64_t speed, int64_t range) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_CHASE;
    b->chase_speed = speed;
    b->chase_range = range;
}

void rt_behavior_add_gravity(void *bhv, int64_t gravity, int64_t max_fall) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_GRAVITY;
    b->gravity = gravity;
    b->max_fall = max_fall;
}

void rt_behavior_add_edge_reverse(void *bhv) {
    if (!bhv) return;
    get(bhv)->flags |= BHV_EDGE_REVERSE;
}

void rt_behavior_add_wall_reverse(void *bhv) {
    if (!bhv) return;
    get(bhv)->flags |= BHV_WALL_REVERSE;
}

void rt_behavior_add_shoot(void *bhv, int64_t cooldown_ms) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_SHOOT;
    b->shoot_cooldown_ms = cooldown_ms;
    b->shoot_timer = cooldown_ms;
}

void rt_behavior_add_sine_float(void *bhv, int64_t amplitude, int64_t speed) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_SINE_FLOAT;
    b->float_amplitude = amplitude;
    b->float_speed = speed;
}

void rt_behavior_add_anim_loop(void *bhv, int64_t frame_count, int64_t ms_per_frame) {
    if (!bhv) return;
    behavior_impl *b = get(bhv);
    b->flags |= BHV_ANIM_LOOP;
    b->anim_frames = frame_count;
    b->anim_ms = ms_per_frame;
}

void rt_behavior_update(void *bhv, void *entity, void *tilemap,
                        int64_t target_x, int64_t target_y, int64_t dt) {
    if (!bhv || !entity)
        return;
    behavior_impl *b = get(bhv);
    uint32_t f = b->flags;

    // 1. Gravity
    if (f & BHV_GRAVITY)
        rt_entity_apply_gravity(entity, b->gravity, b->max_fall, dt);

    // 2. Patrol: set vx based on direction
    if (f & BHV_PATROL) {
        int64_t dir = rt_entity_get_dir(entity);
        rt_entity_set_vx(entity, dir > 0 ? b->patrol_speed : -b->patrol_speed);
    }

    // 3. Chase: override vx if target is in range
    if (f & BHV_CHASE) {
        int64_t ex = rt_entity_get_x(entity) / 100; // Convert to pixels
        int64_t ey = rt_entity_get_y(entity) / 100;
        int64_t dx = target_x - ex;
        int64_t dy = target_y - ey;
        int64_t dist2 = dx * dx + dy * dy;
        int64_t range2 = b->chase_range * b->chase_range;
        if (dist2 <= range2 && dist2 > 0) {
            if (dx > 0) {
                rt_entity_set_vx(entity, b->chase_speed);
                rt_entity_set_dir(entity, 1);
            } else {
                rt_entity_set_vx(entity, -b->chase_speed);
                rt_entity_set_dir(entity, -1);
            }
        }
    }

    // 4. Sine float: add vertical oscillation
    if (f & BHV_SINE_FLOAT) {
        b->float_phase += b->float_speed * dt / 16;
        double rad = (double)b->float_phase * 3.14159265 / 18000.0;
        int64_t offset = (int64_t)(sin(rad) * (double)b->float_amplitude);
        rt_entity_set_vy(entity, offset);
    }

    // 5. Move + collide
    rt_entity_move_and_collide(entity, tilemap, dt);

    // 6. Wall reverse
    if (f & BHV_WALL_REVERSE)
        rt_entity_patrol_reverse(entity, b->patrol_speed > 0 ? b->patrol_speed : b->chase_speed);

    // 7. Edge reverse
    if ((f & BHV_EDGE_REVERSE) && rt_entity_on_ground(entity)) {
        if (rt_entity_at_edge(entity, tilemap)) {
            int64_t dir = rt_entity_get_dir(entity);
            rt_entity_set_dir(entity, -dir);
            int64_t spd = b->patrol_speed > 0 ? b->patrol_speed : b->chase_speed;
            rt_entity_set_vx(entity, -dir > 0 ? spd : -spd);
        }
    }

    // 8. Shoot cooldown
    if (f & BHV_SHOOT) {
        b->shoot_timer -= dt;
        if (b->shoot_timer <= 0) {
            b->shoot_ready = 1;
            b->shoot_timer = b->shoot_cooldown_ms;
        }
    }

    // 9. Animation loop
    if (f & BHV_ANIM_LOOP) {
        b->anim_timer += dt;
        if (b->anim_timer >= b->anim_ms) {
            b->anim_timer -= b->anim_ms;
            b->anim_frame = (b->anim_frame + 1) % b->anim_frames;
        }
    }
}

int8_t rt_behavior_shoot_ready(void *bhv) {
    if (!bhv) return 0;
    behavior_impl *b = get(bhv);
    if (b->shoot_ready) {
        b->shoot_ready = 0;
        return 1;
    }
    return 0;
}

int64_t rt_behavior_anim_frame(void *bhv) {
    return bhv ? get(bhv)->anim_frame : 0;
}
