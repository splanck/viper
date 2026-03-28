//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_platformer_ctrl.c
// Purpose: Platformer movement controller implementing standard mechanics:
//   jump buffering (grace window before landing), coyote time (grace window
//   after leaving ledge), variable jump height (hold vs release), and separate
//   ground/air acceleration curves. All timing is ms-based for dt-independence.
//
// Key invariants:
//   - Velocities are in x100 (centipixel) units for sub-pixel precision.
//   - Jump buffer and coyote timers count DOWN in ms.
//   - ShouldJump is set when jump_buffer > 0 AND (on_ground OR coyote > 0).
//   - Gravity applies apex hang bonus when |vy| < threshold.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: src/runtime/game/rt_platformer_ctrl.h
//
//===----------------------------------------------------------------------===//

#include "rt_platformer_ctrl.h"
#include "rt_object.h"
#include <stdlib.h>

struct rt_platformer_ctrl_impl {
    // Output velocity (x100 centipixels)
    int64_t vx;
    int64_t vy;
    int64_t facing; // 1=right, -1=left

    // Jump buffer + coyote
    int64_t jump_buffer_ms;    // config: buffer window
    int64_t coyote_ms;         // config: coyote window
    int64_t jump_buffer_timer; // current buffer countdown
    int64_t coyote_timer;      // current coyote countdown
    int8_t should_jump;        // one-shot flag
    int8_t was_on_ground;      // previous frame ground state

    // Acceleration config (x100 units per 16ms frame)
    int64_t accel_ground;
    int64_t accel_air;
    int64_t decel;

    // Jump config (x100 units, negative = up)
    int64_t jump_force_full;
    int64_t jump_force_cut; // Force applied when jump released early

    // Speed caps (x100 units)
    int64_t max_speed;
    int64_t max_speed_sprint;

    // Gravity config
    int64_t gravity;
    int64_t max_fall;
    int64_t apex_threshold;   // |vy| below this gets reduced gravity
    int64_t apex_gravity_pct; // Gravity multiplier at apex (percentage, e.g. 60)

    // State
    int8_t jump_held_prev;
};

rt_platformer_ctrl rt_platformer_ctrl_new(void) {
    struct rt_platformer_ctrl_impl *ctrl = (struct rt_platformer_ctrl_impl *)rt_obj_new_i64(
        0, (int64_t)sizeof(struct rt_platformer_ctrl_impl));
    if (!ctrl)
        return NULL;

    ctrl->vx = 0;
    ctrl->vy = 0;
    ctrl->facing = 1;
    ctrl->jump_buffer_ms = 100;
    ctrl->coyote_ms = 80;
    ctrl->jump_buffer_timer = 0;
    ctrl->coyote_timer = 0;
    ctrl->should_jump = 0;
    ctrl->was_on_ground = 0;
    ctrl->accel_ground = 80;
    ctrl->accel_air = 40;
    ctrl->decel = 60;
    ctrl->jump_force_full = -1500;
    ctrl->jump_force_cut = -600;
    ctrl->max_speed = 525;
    ctrl->max_speed_sprint = 788;
    ctrl->gravity = 78;
    ctrl->max_fall = 1200;
    ctrl->apex_threshold = 200;
    ctrl->apex_gravity_pct = 60;
    ctrl->jump_held_prev = 0;

    return ctrl;
}

void rt_platformer_ctrl_destroy(rt_platformer_ctrl ctrl) {
    if (ctrl && rt_obj_release_check0(ctrl))
        rt_obj_free(ctrl);
}

// Configuration setters
void rt_platformer_ctrl_set_jump_buffer(rt_platformer_ctrl ctrl, int64_t ms) {
    if (ctrl)
        ctrl->jump_buffer_ms = ms;
}

void rt_platformer_ctrl_set_coyote_time(rt_platformer_ctrl ctrl, int64_t ms) {
    if (ctrl)
        ctrl->coyote_ms = ms;
}

void rt_platformer_ctrl_set_acceleration(rt_platformer_ctrl ctrl,
                                         int64_t ground,
                                         int64_t air,
                                         int64_t decel) {
    if (!ctrl)
        return;
    ctrl->accel_ground = ground;
    ctrl->accel_air = air;
    ctrl->decel = decel;
}

void rt_platformer_ctrl_set_jump_force(rt_platformer_ctrl ctrl,
                                       int64_t full_force,
                                       int64_t cut_force) {
    if (!ctrl)
        return;
    ctrl->jump_force_full = full_force;
    ctrl->jump_force_cut = cut_force;
}

void rt_platformer_ctrl_set_max_speed(rt_platformer_ctrl ctrl, int64_t normal, int64_t sprint) {
    if (!ctrl)
        return;
    ctrl->max_speed = normal;
    ctrl->max_speed_sprint = sprint;
}

void rt_platformer_ctrl_set_gravity(rt_platformer_ctrl ctrl, int64_t gravity, int64_t max_fall) {
    if (!ctrl)
        return;
    ctrl->gravity = gravity;
    ctrl->max_fall = max_fall;
}

void rt_platformer_ctrl_set_apex_bonus(rt_platformer_ctrl ctrl,
                                       int64_t threshold,
                                       int64_t gravity_mult_pct) {
    if (!ctrl)
        return;
    ctrl->apex_threshold = threshold;
    ctrl->apex_gravity_pct = gravity_mult_pct;
}

void rt_platformer_ctrl_update(rt_platformer_ctrl ctrl,
                               int64_t dt,
                               int8_t input_left,
                               int8_t input_right,
                               int8_t jump_pressed,
                               int8_t jump_held,
                               int8_t on_ground,
                               int8_t sprint) {
    if (!ctrl || dt <= 0)
        return;

    int64_t dt_scale = dt; // ms since last frame
    int64_t dt_base = 16;  // baseline 60fps frame

    ctrl->should_jump = 0;

    // --- Coyote time ---
    if (on_ground) {
        ctrl->coyote_timer = ctrl->coyote_ms;
    } else {
        if (ctrl->was_on_ground && ctrl->vy >= 0) {
            // Just left ground (didn't jump) — start coyote window
            ctrl->coyote_timer = ctrl->coyote_ms;
        }
        if (ctrl->coyote_timer > 0)
            ctrl->coyote_timer -= dt_scale;
    }

    // --- Jump buffer ---
    if (jump_pressed)
        ctrl->jump_buffer_timer = ctrl->jump_buffer_ms;
    if (ctrl->jump_buffer_timer > 0)
        ctrl->jump_buffer_timer -= dt_scale;

    // --- Resolve jump ---
    if (ctrl->jump_buffer_timer > 0 && (on_ground || ctrl->coyote_timer > 0)) {
        ctrl->should_jump = 1;
        ctrl->jump_buffer_timer = 0;
        ctrl->coyote_timer = 0;
    }

    // --- Variable jump height (cut jump on release) ---
    if (!jump_held && ctrl->jump_held_prev && ctrl->vy < ctrl->jump_force_cut) {
        ctrl->vy = ctrl->jump_force_cut;
    }
    ctrl->jump_held_prev = jump_held;

    // --- Horizontal acceleration ---
    int64_t move_dir = 0;
    if (input_left)
        move_dir = -1;
    if (input_right)
        move_dir = 1;

    if (move_dir != 0) {
        ctrl->facing = move_dir;
        int64_t accel = on_ground ? ctrl->accel_ground : ctrl->accel_air;
        ctrl->vx += move_dir * accel * dt_scale / dt_base;

        int64_t cap = sprint ? ctrl->max_speed_sprint : ctrl->max_speed;
        if (ctrl->vx > cap)
            ctrl->vx = cap;
        if (ctrl->vx < -cap)
            ctrl->vx = -cap;
    } else {
        // Decelerate
        int64_t dec = ctrl->decel * dt_scale / dt_base;
        if (ctrl->vx > 0) {
            ctrl->vx -= dec;
            if (ctrl->vx < 0)
                ctrl->vx = 0;
        } else if (ctrl->vx < 0) {
            ctrl->vx += dec;
            if (ctrl->vx > 0)
                ctrl->vx = 0;
        }
    }

    // --- Gravity with apex hang ---
    int64_t g = ctrl->gravity * dt_scale / dt_base;
    int64_t abs_vy = ctrl->vy;
    if (abs_vy < 0)
        abs_vy = -abs_vy;
    if (abs_vy < ctrl->apex_threshold) {
        g = g * ctrl->apex_gravity_pct / 100;
    }
    ctrl->vy += g;
    if (ctrl->vy > ctrl->max_fall)
        ctrl->vy = ctrl->max_fall;

    ctrl->was_on_ground = on_ground;
}

// Output queries
int64_t rt_platformer_ctrl_get_vx(rt_platformer_ctrl ctrl) {
    return ctrl ? ctrl->vx : 0;
}

int64_t rt_platformer_ctrl_get_vy(rt_platformer_ctrl ctrl) {
    return ctrl ? ctrl->vy : 0;
}

int8_t rt_platformer_ctrl_should_jump(rt_platformer_ctrl ctrl) {
    if (!ctrl)
        return 0;
    int8_t result = ctrl->should_jump;
    ctrl->should_jump = 0; // One-shot: consumed on read
    return result;
}

int64_t rt_platformer_ctrl_get_jump_force(rt_platformer_ctrl ctrl) {
    return ctrl ? ctrl->jump_force_full : 0;
}

int64_t rt_platformer_ctrl_get_facing(rt_platformer_ctrl ctrl) {
    return ctrl ? ctrl->facing : 1;
}

int8_t rt_platformer_ctrl_is_moving(rt_platformer_ctrl ctrl) {
    return ctrl ? (ctrl->vx != 0 ? 1 : 0) : 0;
}

void rt_platformer_ctrl_set_vx(rt_platformer_ctrl ctrl, int64_t vx) {
    if (ctrl)
        ctrl->vx = vx;
}

void rt_platformer_ctrl_set_vy(rt_platformer_ctrl ctrl, int64_t vy) {
    if (ctrl)
        ctrl->vy = vy;
}
