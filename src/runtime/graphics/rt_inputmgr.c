//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_inputmgr.c
/// @brief Implementation of high-level input manager.
///
//===----------------------------------------------------------------------===//

#include "rt_inputmgr.h"
#include "rt_input.h"
#include "rt_object.h"
#include <stdlib.h>
#include <string.h>

// Maximum number of keys to track for debouncing
#define MAX_DEBOUNCE_KEYS 32

/// Internal structure for InputManager.
struct rt_inputmgr_impl
{
    int64_t debounce_delay;                     // Frames to wait for debounce
    int64_t debounce_timers[MAX_DEBOUNCE_KEYS]; // Per-key debounce timers
    int64_t debounce_keys[MAX_DEBOUNCE_KEYS];   // Key codes being debounced
    int64_t debounce_count;                     // Number of keys being tracked
};

rt_inputmgr rt_inputmgr_new(void)
{
    struct rt_inputmgr_impl *mgr =
        (struct rt_inputmgr_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_inputmgr_impl));

    mgr->debounce_delay = 12; // Default: 12 frames (~200ms at 60fps)
    mgr->debounce_count = 0;
    memset(mgr->debounce_timers, 0, sizeof(mgr->debounce_timers));
    memset(mgr->debounce_keys, 0, sizeof(mgr->debounce_keys));

    return mgr;
}

void rt_inputmgr_destroy(rt_inputmgr mgr)
{
    (void)mgr;
}

void rt_inputmgr_update(rt_inputmgr mgr)
{
    if (!mgr)
        return;

    // Decrement all debounce timers
    for (int64_t i = 0; i < mgr->debounce_count; i++)
    {
        if (mgr->debounce_timers[i] > 0)
        {
            mgr->debounce_timers[i]--;
        }
    }
}

//=============================================================================
// Keyboard
//=============================================================================

int8_t rt_inputmgr_key_pressed(rt_inputmgr mgr, int64_t key)
{
    (void)mgr; // Uses global keyboard state
    return rt_keyboard_was_pressed(key);
}

int8_t rt_inputmgr_key_released(rt_inputmgr mgr, int64_t key)
{
    (void)mgr;
    return rt_keyboard_was_released(key);
}

int8_t rt_inputmgr_key_held(rt_inputmgr mgr, int64_t key)
{
    (void)mgr;
    return rt_keyboard_is_down(key);
}

// Find or create a debounce slot for a key
static int64_t find_or_create_debounce_slot(rt_inputmgr mgr, int64_t key)
{
    // Look for existing slot
    for (int64_t i = 0; i < mgr->debounce_count; i++)
    {
        if (mgr->debounce_keys[i] == key)
        {
            return i;
        }
    }

    // Create new slot if space available
    if (mgr->debounce_count < MAX_DEBOUNCE_KEYS)
    {
        int64_t slot = mgr->debounce_count++;
        mgr->debounce_keys[slot] = key;
        mgr->debounce_timers[slot] = 0;
        return slot;
    }

    // No space - reuse oldest slot (slot 0)
    return 0;
}

int8_t rt_inputmgr_key_pressed_debounced(rt_inputmgr mgr, int64_t key)
{
    if (!mgr)
        return 0;

    int64_t slot = find_or_create_debounce_slot(mgr, key);

    // Check if debounce timer has expired and key is pressed
    if (mgr->debounce_timers[slot] == 0 && rt_keyboard_is_down(key))
    {
        mgr->debounce_timers[slot] = mgr->debounce_delay;
        return 1;
    }

    // If key is released, reset timer so next press is immediate
    if (!rt_keyboard_is_down(key))
    {
        mgr->debounce_timers[slot] = 0;
    }

    return 0;
}

void rt_inputmgr_set_debounce_delay(rt_inputmgr mgr, int64_t frames)
{
    if (mgr && frames >= 0)
    {
        mgr->debounce_delay = frames;
    }
}

int64_t rt_inputmgr_get_debounce_delay(rt_inputmgr mgr)
{
    return mgr ? mgr->debounce_delay : 0;
}

//=============================================================================
// Mouse
//=============================================================================

int8_t rt_inputmgr_mouse_pressed(rt_inputmgr mgr, int64_t button)
{
    (void)mgr;
    return rt_mouse_was_pressed(button);
}

int8_t rt_inputmgr_mouse_released(rt_inputmgr mgr, int64_t button)
{
    (void)mgr;
    return rt_mouse_was_released(button);
}

int8_t rt_inputmgr_mouse_held(rt_inputmgr mgr, int64_t button)
{
    (void)mgr;
    return rt_mouse_is_down(button);
}

int64_t rt_inputmgr_mouse_x(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_x();
}

int64_t rt_inputmgr_mouse_y(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_y();
}

int64_t rt_inputmgr_mouse_delta_x(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_delta_x();
}

int64_t rt_inputmgr_mouse_delta_y(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_delta_y();
}

int64_t rt_inputmgr_scroll_y(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_wheel_y();
}

int64_t rt_inputmgr_scroll_x(rt_inputmgr mgr)
{
    (void)mgr;
    return rt_mouse_wheel_x();
}

//=============================================================================
// Gamepad
//=============================================================================

int8_t rt_inputmgr_pad_pressed(rt_inputmgr mgr, int64_t pad, int64_t button)
{
    (void)mgr;

    if (pad == -1)
    {
        // Check any gamepad
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_was_pressed(i, button))
            {
                return 1;
            }
        }
        return 0;
    }

    return rt_pad_was_pressed(pad, button);
}

int8_t rt_inputmgr_pad_released(rt_inputmgr mgr, int64_t pad, int64_t button)
{
    (void)mgr;

    if (pad == -1)
    {
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_was_released(i, button))
            {
                return 1;
            }
        }
        return 0;
    }

    return rt_pad_was_released(pad, button);
}

int8_t rt_inputmgr_pad_held(rt_inputmgr mgr, int64_t pad, int64_t button)
{
    (void)mgr;

    if (pad == -1)
    {
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_is_down(i, button))
            {
                return 1;
            }
        }
        return 0;
    }

    return rt_pad_is_down(pad, button);
}

double rt_inputmgr_pad_left_x(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_left_x(pad);
}

double rt_inputmgr_pad_left_y(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_left_y(pad);
}

double rt_inputmgr_pad_right_x(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_right_x(pad);
}

double rt_inputmgr_pad_right_y(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_right_y(pad);
}

double rt_inputmgr_pad_left_trigger(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_left_trigger(pad);
}

double rt_inputmgr_pad_right_trigger(rt_inputmgr mgr, int64_t pad)
{
    (void)mgr;
    return rt_pad_right_trigger(pad);
}

//=============================================================================
// Unified Direction Input
//=============================================================================

int8_t rt_inputmgr_up(rt_inputmgr mgr)
{
    (void)mgr;

    // Keyboard: Up arrow or W
    if (rt_keyboard_is_down(VIPER_KEY_UP) || rt_keyboard_is_down(VIPER_KEY_W))
    {
        return 1;
    }

    // Gamepad: D-pad up or left stick up
    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            if (rt_pad_is_down(i, VIPER_PAD_UP))
                return 1;
            if (rt_pad_left_y(i) < -0.5)
                return 1;
        }
    }

    return 0;
}

int8_t rt_inputmgr_down(rt_inputmgr mgr)
{
    (void)mgr;

    if (rt_keyboard_is_down(VIPER_KEY_DOWN) || rt_keyboard_is_down(VIPER_KEY_S))
    {
        return 1;
    }

    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            if (rt_pad_is_down(i, VIPER_PAD_DOWN))
                return 1;
            if (rt_pad_left_y(i) > 0.5)
                return 1;
        }
    }

    return 0;
}

int8_t rt_inputmgr_left(rt_inputmgr mgr)
{
    (void)mgr;

    if (rt_keyboard_is_down(VIPER_KEY_LEFT) || rt_keyboard_is_down(VIPER_KEY_A))
    {
        return 1;
    }

    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            if (rt_pad_is_down(i, VIPER_PAD_LEFT))
                return 1;
            if (rt_pad_left_x(i) < -0.5)
                return 1;
        }
    }

    return 0;
}

int8_t rt_inputmgr_right(rt_inputmgr mgr)
{
    (void)mgr;

    if (rt_keyboard_is_down(VIPER_KEY_RIGHT) || rt_keyboard_is_down(VIPER_KEY_D))
    {
        return 1;
    }

    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            if (rt_pad_is_down(i, VIPER_PAD_RIGHT))
                return 1;
            if (rt_pad_left_x(i) > 0.5)
                return 1;
        }
    }

    return 0;
}

int8_t rt_inputmgr_confirm(rt_inputmgr mgr)
{
    (void)mgr;

    // Keyboard: Enter or Space
    if (rt_keyboard_was_pressed(VIPER_KEY_ENTER) || rt_keyboard_was_pressed(VIPER_KEY_SPACE))
    {
        return 1;
    }

    // Gamepad: A button
    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i) && rt_pad_was_pressed(i, VIPER_PAD_A))
        {
            return 1;
        }
    }

    return 0;
}

int8_t rt_inputmgr_cancel(rt_inputmgr mgr)
{
    (void)mgr;

    // Keyboard: Escape
    if (rt_keyboard_was_pressed(VIPER_KEY_ESCAPE))
    {
        return 1;
    }

    // Gamepad: B button
    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i) && rt_pad_was_pressed(i, VIPER_PAD_B))
        {
            return 1;
        }
    }

    return 0;
}

double rt_inputmgr_axis_x(rt_inputmgr mgr)
{
    (void)mgr;

    double value = 0.0;

    // Keyboard contribution
    if (rt_keyboard_is_down(VIPER_KEY_LEFT) || rt_keyboard_is_down(VIPER_KEY_A))
    {
        value -= 1.0;
    }
    if (rt_keyboard_is_down(VIPER_KEY_RIGHT) || rt_keyboard_is_down(VIPER_KEY_D))
    {
        value += 1.0;
    }

    // Gamepad contribution (use first connected pad with significant input)
    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            double pad_x = rt_pad_left_x(i);
            if (pad_x < -0.1 || pad_x > 0.1)
            {
                // Use gamepad value if more extreme than keyboard
                if ((pad_x < 0 && pad_x < value) || (pad_x > 0 && pad_x > value))
                {
                    value = pad_x;
                }
            }
            // D-pad
            if (rt_pad_is_down(i, VIPER_PAD_LEFT) && value > -1.0)
                value = -1.0;
            if (rt_pad_is_down(i, VIPER_PAD_RIGHT) && value < 1.0)
                value = 1.0;
        }
    }

    // Clamp
    if (value < -1.0)
        value = -1.0;
    if (value > 1.0)
        value = 1.0;

    return value;
}

double rt_inputmgr_axis_y(rt_inputmgr mgr)
{
    (void)mgr;

    double value = 0.0;

    // Keyboard contribution
    if (rt_keyboard_is_down(VIPER_KEY_UP) || rt_keyboard_is_down(VIPER_KEY_W))
    {
        value -= 1.0;
    }
    if (rt_keyboard_is_down(VIPER_KEY_DOWN) || rt_keyboard_is_down(VIPER_KEY_S))
    {
        value += 1.0;
    }

    // Gamepad contribution
    for (int64_t i = 0; i < 4; i++)
    {
        if (rt_pad_is_connected(i))
        {
            double pad_y = rt_pad_left_y(i);
            if (pad_y < -0.1 || pad_y > 0.1)
            {
                if ((pad_y < 0 && pad_y < value) || (pad_y > 0 && pad_y > value))
                {
                    value = pad_y;
                }
            }
            if (rt_pad_is_down(i, VIPER_PAD_UP) && value > -1.0)
                value = -1.0;
            if (rt_pad_is_down(i, VIPER_PAD_DOWN) && value < 1.0)
                value = 1.0;
        }
    }

    if (value < -1.0)
        value = -1.0;
    if (value > 1.0)
        value = 1.0;

    return value;
}
