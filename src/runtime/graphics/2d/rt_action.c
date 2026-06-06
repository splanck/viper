//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_action.c
// Purpose: Input action mapping core: named logical actions bound to keyboard, mouse,
//   gamepad, axis, and chord sources; per-frame polling and pressed/released/
//   held/axis queries. Owns the global action list and its lifecycle.
//
// Links: rt_action.h (public API), rt_action_internal.h (shared model),
//        rt_action_presets.c (built-in presets), rt_action_io.c (JSON save/load)
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
#include "rt_action_internal.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_json_stream.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <stdlib.h>
#include <string.h>

extern int64_t rt_unbox_i64(void *box);

// Global action registry (declared extern in rt_action_internal.h).
Action *g_actions = NULL;
int8_t g_initialized = 0;

/// @brief Linear-scan the global action list by `rt_string` name.
///
/// Specialized to compare lengths first then bytes — avoids a
/// `strdup` round-trip versus calling `find_action(rt_string_cstr(...))`.
static Action *find_action_str(rt_string name) {
    if (!name)
        return NULL;
    int64_t name_len = rt_str_len(name);
    const char *name_data = name->data;

    Action *a = g_actions;
    while (a) {
        size_t a_len = strlen(a->name);
        if ((int64_t)a_len == name_len && memcmp(a->name, name_data, a_len) == 0)
            return a;
        a = a->next;
    }
    return NULL;
}

/// @brief Heap-allocate a NUL-terminated C string from an `rt_string`.
///
/// Returns NULL on empty input or allocation failure. Used to capture
/// the action's name in our own buffer (the `rt_string` may go away).
static char *strdup_rt_string(rt_string s) {
    if (!s)
        return NULL;
    int64_t len = rt_str_len(s);
    if (len == 0)
        return NULL;
    char *result = (char *)malloc((size_t)len + 1);
    if (!result)
        return NULL;
    memcpy(result, s->data, (size_t)len);
    result[len] = '\0';
    return result;
}

/// @brief Walk and free a singly-linked binding list.
static void free_bindings(Binding *b) {
    while (b) {
        Binding *next = b->next;
        free(b);
        b = next;
    }
}

/// @brief Free an action's name + bindings + the action node itself.
static void free_action(Action *a) {
    if (a) {
        free(a->name);
        free_bindings(a->bindings);
        free(a);
    }
}

/// @brief Remove the first binding matching `(type, code, pad_index)`.
///
/// Returns 1 if a binding was removed, 0 if none matched. Doesn't
/// remove duplicates beyond the first match — callers wanting to
/// strip all matching bindings need to loop.
static int8_t remove_binding(Action *action, BindingType type, int64_t code, int64_t pad_index) {
    Binding **pp = &action->bindings;
    while (*pp) {
        Binding *b = *pp;
        if (b->type == type && b->code == code && b->pad_index == pad_index) {
            *pp = b->next;
            free(b);
            return 1;
        }
        pp = &b->next;
    }
    return 0;
}

/// @brief True if `key` is held down this frame.
static int8_t key_held(int64_t key) {
    return rt_keyboard_is_down(key);
}

/// @brief True if `key` was pressed (down-edge) this frame.
static int8_t key_pressed(int64_t key) {
    return rt_keyboard_was_pressed(key);
}

/// @brief True if `key` was released (up-edge) this frame.
static int8_t key_released(int64_t key) {
    return rt_keyboard_was_released(key);
}

/// @brief True if mouse `button` is held down this frame.
static int8_t mouse_held(int64_t button) {
    return rt_mouse_is_down(button);
}

/// @brief True if mouse `button` was pressed (down-edge) this frame.
static int8_t mouse_pressed(int64_t button) {
    return rt_mouse_was_pressed(button);
}

/// @brief True if mouse `button` was released (up-edge) this frame.
static int8_t mouse_released(int64_t button) {
    return rt_mouse_was_released(button);
}

/// @brief Pad button held query, with `pad_index < 0` = any connected pad.
///
/// Loops over pads 0..3 when `pad_index` is negative, returning true on
/// the first connected pad with the button held.
static int8_t pad_held(int64_t pad_index, int64_t button) {
    if (pad_index < 0) {
        // Any controller
        for (int64_t i = 0; i < 4; i++) {
            if (rt_pad_is_connected(i) && rt_pad_is_down(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_is_down(pad_index, button);
}

/// @brief Pad button down-edge query (any-pad fallback for `pad_index < 0`).
static int8_t pad_pressed(int64_t pad_index, int64_t button) {
    if (pad_index < 0) {
        for (int64_t i = 0; i < 4; i++) {
            if (rt_pad_is_connected(i) && rt_pad_was_pressed(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_was_pressed(pad_index, button);
}

/// @brief Pad button up-edge query (any-pad fallback for `pad_index < 0`).
static int8_t pad_released(int64_t pad_index, int64_t button) {
    if (pad_index < 0) {
        for (int64_t i = 0; i < 4; i++) {
            if (rt_pad_is_connected(i) && rt_pad_was_released(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_was_released(pad_index, button);
}

/// @brief Read a gamepad axis value (-1..1 sticks, 0..1 triggers).
///
/// `axis` is one of `VIPER_AXIS_*`. With `pad_index < 0`, returns the
/// first non-zero value across pads 0..3 — useful when you want
/// "any controller's left stick" without binding to a specific index.
static double pad_axis_value(int64_t pad_index, int64_t axis) {
    if (pad_index < 0) {
        // Return value from first connected controller with non-zero input
        for (int64_t i = 0; i < 4; i++) {
            if (!rt_pad_is_connected(i))
                continue;
            double v = 0.0;
            switch (axis) {
                case VIPER_AXIS_LEFT_X:
                    v = rt_pad_left_x(i);
                    break;
                case VIPER_AXIS_LEFT_Y:
                    v = rt_pad_left_y(i);
                    break;
                case VIPER_AXIS_RIGHT_X:
                    v = rt_pad_right_x(i);
                    break;
                case VIPER_AXIS_RIGHT_Y:
                    v = rt_pad_right_y(i);
                    break;
                case VIPER_AXIS_LEFT_TRIGGER:
                    v = rt_pad_left_trigger(i);
                    break;
                case VIPER_AXIS_RIGHT_TRIGGER:
                    v = rt_pad_right_trigger(i);
                    break;
            }
            if (v != 0.0)
                return v;
        }
        return 0.0;
    }

    if (!rt_pad_is_connected(pad_index))
        return 0.0;

    switch (axis) {
        case VIPER_AXIS_LEFT_X:
            return rt_pad_left_x(pad_index);
        case VIPER_AXIS_LEFT_Y:
            return rt_pad_left_y(pad_index);
        case VIPER_AXIS_RIGHT_X:
            return rt_pad_right_x(pad_index);
        case VIPER_AXIS_RIGHT_Y:
            return rt_pad_right_y(pad_index);
        case VIPER_AXIS_LEFT_TRIGGER:
            return rt_pad_left_trigger(pad_index);
        case VIPER_AXIS_RIGHT_TRIGGER:
            return rt_pad_right_trigger(pad_index);
        default:
            return 0.0;
    }
}

/// @brief Clamp `value` into `[-1, 1]` for axis output normalization.
static double clamp_axis(double value) {
    if (value < -1.0)
        return -1.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Initialize the global action mapping system.
/// @details Must be called once before any action operations. Clears all state.
///   Called automatically by the Viper runtime startup.
void rt_action_init(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_initialized)
        return;
    g_actions = NULL;
    g_initialized = 1;
}

/// @brief Shutdown the action mapping system.
///
/// Clears every action and binding via `rt_action_clear`, then marks
/// the system uninitialized. Safe to call multiple times. Called
/// during runtime teardown.
void rt_action_shutdown(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_action_clear();
    g_initialized = 0;
}

/// @brief Update all action states for the current frame.
/// @details Polls each registered action's bindings against the current input
///   state (keyboard, mouse, gamepad). Computes pressed/released/held flags
///   and axis values. Must be called exactly once per frame, AFTER rt_canvas_poll()
///   has processed input events. Action.Pressed(), Action.Held(), and Action.Axis()
///   return values computed by this function.
void rt_action_update(void) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_initialized)
        return;

    Action *a = g_actions;
    while (a) {
        int8_t was_held = a->held;
        a->pressed = 0;
        a->released = 0;
        a->held = 0;
        a->axis_value = 0.0;

        Binding *b = a->bindings;
        while (b) {
            switch (b->type) {
                case BIND_KEY:
                    if (a->is_axis) {
                        if (key_held(b->code))
                            a->axis_value += b->value;
                    } else {
                        if (key_pressed(b->code))
                            a->pressed = 1;
                        if (key_released(b->code))
                            a->released = 1;
                        if (key_held(b->code))
                            a->held = 1;
                    }
                    break;

                case BIND_MOUSE_BUTTON:
                    if (!a->is_axis) {
                        if (mouse_pressed(b->code))
                            a->pressed = 1;
                        if (mouse_released(b->code))
                            a->released = 1;
                        if (mouse_held(b->code))
                            a->held = 1;
                    }
                    break;

                case BIND_MOUSE_X:
                    if (a->is_axis)
                        a->axis_value += (double)rt_mouse_delta_x() * b->value;
                    break;

                case BIND_MOUSE_Y:
                    if (a->is_axis)
                        a->axis_value += (double)rt_mouse_delta_y() * b->value;
                    break;

                case BIND_SCROLL_X:
                    if (a->is_axis)
                        a->axis_value += rt_mouse_wheel_xf() * b->value;
                    break;

                case BIND_SCROLL_Y:
                    if (a->is_axis)
                        a->axis_value += rt_mouse_wheel_yf() * b->value;
                    break;

                case BIND_PAD_BUTTON:
                    if (!a->is_axis) {
                        if (pad_pressed(b->pad_index, b->code))
                            a->pressed = 1;
                        if (pad_released(b->pad_index, b->code))
                            a->released = 1;
                        if (pad_held(b->pad_index, b->code))
                            a->held = 1;
                    }
                    break;

                case BIND_PAD_AXIS:
                    if (a->is_axis)
                        a->axis_value += pad_axis_value(b->pad_index, b->code) * b->value;
                    break;

                case BIND_PAD_BUTTON_AXIS:
                    if (a->is_axis) {
                        if (pad_held(b->pad_index, b->code))
                            a->axis_value += b->value;
                    }
                    break;

                case BIND_CHORD:
                    if (!a->is_axis && b->chord_len > 0) {
                        // All chord keys must be held
                        int8_t all_held = 1;
                        int8_t any_pressed = 0;
                        int32_t i;
                        for (i = 0; i < b->chord_len; i++) {
                            if (!key_held(b->chord_keys[i])) {
                                all_held = 0;
                                break;
                            }
                            if (key_pressed(b->chord_keys[i]))
                                any_pressed = 1;
                        }
                        if (all_held) {
                            a->held = 1;
                            // Chord is "pressed" when all keys held and at least one
                            // was newly pressed this frame
                            if (any_pressed)
                                a->pressed = 1;
                        }
                    }
                    break;

                default:
                    break;
            }
            b = b->next;
        }

        if (was_held && !a->held)
            a->released = 1;

        a = a->next;
    }
}

/// @brief `Action.Clear` — destroy every action and binding.
///
/// Walks the global action list freeing each action (which in turn
/// frees its bindings). After clear the system is still initialized;
/// new actions can be defined immediately.
void rt_action_clear(void) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = g_actions;
    while (a) {
        Action *next = a->next;
        free_action(a);
        a = next;
    }
    g_actions = NULL;
}

/// @brief Register a new named action for input mapping.
/// @details Creates a button-style action (pressed/released/held). The name must
///   be unique. After defining, bind physical inputs with BindKey(), BindMouse(), etc.
/// @param name Action name string (e.g., "jump", "fire"). Max 63 characters.
/// @return 1 on success, 0 if name is empty, already exists, or allocation fails.
int8_t rt_action_define(rt_string name) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_initialized)
        rt_action_init();

    if (!name || rt_str_len(name) == 0)
        return 0;

    if (find_action_str(name))
        return 0; // Already exists

    Action *a = (Action *)malloc(sizeof(Action));
    if (!a)
        return 0;

    a->name = strdup_rt_string(name);
    if (!a->name) {
        free(a);
        return 0;
    }
    a->is_axis = 0;
    a->bindings = NULL;
    a->pressed = 0;
    a->released = 0;
    a->held = 0;
    a->axis_value = 0.0;
    a->next = g_actions;
    g_actions = a;
    return 1;
}

/// @brief `Action.DefineAxis(name)` — register an axis-style action.
///
/// Axis actions accumulate continuous values (-1..1) from analog
/// sources (sticks, mouse delta) or button bindings (each button
/// contributes its `value` field). Use `Axis()` to read the latest
/// frame's accumulated value.
int8_t rt_action_define_axis(rt_string name) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_initialized)
        rt_action_init();

    if (!name || rt_str_len(name) == 0)
        return 0;

    if (find_action_str(name))
        return 0; // Already exists

    Action *a = (Action *)malloc(sizeof(Action));
    if (!a)
        return 0;

    a->name = strdup_rt_string(name);
    if (!a->name) {
        free(a);
        return 0;
    }
    a->is_axis = 1;
    a->bindings = NULL;
    a->pressed = 0;
    a->released = 0;
    a->held = 0;
    a->axis_value = 0.0;
    a->next = g_actions;
    g_actions = a;
    return 1;
}

/// @brief `Action.Exists(name)` — true if an action with that name is defined.
int8_t rt_action_exists(rt_string name) {
    RT_ASSERT_MAIN_THREAD();
    return find_action_str(name) != NULL;
}

/// @brief `Action.IsAxis(name)` — true if the named action is an axis (vs. button).
int8_t rt_action_is_axis(rt_string name) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(name);
    return a ? a->is_axis : 0;
}

/// @brief `Action.Remove(name)` — destroy a single action by name.
///
/// Walks the linked list with a back-pointer, unlinks on match, frees
/// the action + bindings. Returns 1 on success, 0 if not found.
int8_t rt_action_remove(rt_string name) {
    RT_ASSERT_MAIN_THREAD();
    if (!name)
        return 0;

    int64_t name_len = rt_str_len(name);
    const char *name_data = name->data;

    Action **pp = &g_actions;
    while (*pp) {
        Action *a = *pp;
        size_t a_len = strlen(a->name);
        if ((int64_t)a_len == name_len && memcmp(a->name, name_data, a_len) == 0) {
            *pp = a->next;
            free_action(a);
            return 1;
        }
        pp = &a->next;
    }
    return 0;
}

/// @brief `Action.BindKey(action, key)` — add a button-style key binding.
///
/// `key` is a `VIPER_KEY_*` constant. Fails if action doesn't exist,
/// is an axis action, or allocation fails.
int8_t rt_action_bind_key(rt_string action, int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_KEY, key, 0, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.BindKeyAxis(action, key, value)` — add a key→axis-contribution binding.
///
/// When `key` is held, `value` is added to the axis. Use opposite-
/// signed values for opposite directions (e.g., `Left` → -1.0,
/// `Right` → +1.0 for a horizontal-axis "MoveX" action).
int8_t rt_action_bind_key_axis(rt_string action, int64_t key, double value) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_KEY, key, 0, value);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.UnbindKey(action, key)` — remove a key binding.
int8_t rt_action_unbind_key(rt_string action, int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_KEY, key, 0);
}

/// @brief `Action.BindChord(action, keys)` — bind a multi-key chord.
///
/// `keys` is a `seq<int>` of `VIPER_KEY_*` codes. The action is
/// "pressed" the frame all keys in the chord are simultaneously held
/// AND at least one was newly pressed. Caps at `MAX_CHORD_KEYS` (8).
/// Useful for hotkey-style actions like Ctrl+Shift+S.
int8_t rt_action_bind_chord(rt_string action, void *keys) {
    RT_ASSERT_MAIN_THREAD();
    int64_t len, i;
    Binding *b;
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    if (!keys)
        return 0;

    len = rt_seq_len(keys);
    if (len < 2 || len > MAX_CHORD_KEYS)
        return 0;

    b = create_binding(BIND_CHORD, 0, 0, 1.0);
    if (!b)
        return 0;

    b->chord_len = (int32_t)len;
    for (i = 0; i < len; i++)
        b->chord_keys[i] = rt_unbox_i64(rt_seq_get(keys, i));

    add_binding(a, b);
    return 1;
}

/// @brief `Action.UnbindChord(action, keys)` — remove a chord binding.
///
/// Match is exact: same length, same keys in the same order. Returns
/// 1 on success, 0 if no chord matches.
int8_t rt_action_unbind_chord(rt_string action, void *keys) {
    RT_ASSERT_MAIN_THREAD();
    int64_t len, i;
    Binding **pp;
    Action *a = find_action_str(action);
    if (!a || !keys)
        return 0;

    len = rt_seq_len(keys);
    if (len < 2 || len > MAX_CHORD_KEYS)
        return 0;

    pp = &a->bindings;
    while (*pp) {
        Binding *b = *pp;
        if (b->type == BIND_CHORD && b->chord_len == (int32_t)len) {
            int8_t match = 1;
            for (i = 0; i < len; i++) {
                if (b->chord_keys[i] != rt_unbox_i64(rt_seq_get(keys, i))) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                *pp = b->next;
                free(b);
                return 1;
            }
        }
        pp = &b->next;
    }
    return 0;
}

/// @brief `Action.ChordCount(action)` — number of chord bindings on this action.
int64_t rt_action_chord_count(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    int64_t count = 0;
    Binding *b;
    Action *a = find_action_str(action);
    if (!a)
        return 0;

    b = a->bindings;
    while (b) {
        if (b->type == BIND_CHORD)
            count++;
        b = b->next;
    }
    return count;
}

/// @brief `Action.BindMouse(action, button)` — bind a mouse button to a button action.
int8_t rt_action_bind_mouse(rt_string action, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_BUTTON, button, 0, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.UnbindMouse(action, button)` — remove a mouse-button binding.
int8_t rt_action_unbind_mouse(rt_string action, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_MOUSE_BUTTON, button, 0);
}

/// @brief `Action.BindMouseX(action, sensitivity)` — bind mouse X-delta to an axis.
///
/// Per-frame mouse delta (in pixels) is multiplied by `sensitivity`
/// and added to the axis. Typical mouselook setup uses ~0.001-0.01.
int8_t rt_action_bind_mouse_x(rt_string action, double sensitivity) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_X, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.BindMouseY(action, sensitivity)` — bind mouse Y-delta to an axis.
int8_t rt_action_bind_mouse_y(rt_string action, double sensitivity) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_Y, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.BindScrollX(action, sensitivity)` — bind horizontal scroll wheel to an axis.
int8_t rt_action_bind_scroll_x(rt_string action, double sensitivity) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_SCROLL_X, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.BindScrollY(action, sensitivity)` — bind vertical scroll wheel to an axis.
int8_t rt_action_bind_scroll_y(rt_string action, double sensitivity) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_SCROLL_Y, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.BindPadButton(action, padIndex, button)` — bind a gamepad button to a button
/// action.
int8_t rt_action_bind_pad_button(rt_string action, int64_t pad_index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_PAD_BUTTON, button, pad_index, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.UnbindPadButton(action, padIndex, button)` — remove a pad-button binding.
int8_t rt_action_unbind_pad_button(rt_string action, int64_t pad_index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_PAD_BUTTON, button, pad_index);
}

/// @brief `Action.BindPadAxis(action, padIndex, axis, scale)` — bind a gamepad analog axis.
///
/// `axis` is `VIPER_AXIS_*`. `scale` multiplies the raw axis value
/// (typically 1.0 or -1.0 to invert).
int8_t rt_action_bind_pad_axis(rt_string action, int64_t pad_index, int64_t axis, double scale) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_PAD_AXIS, axis, pad_index, scale);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief `Action.UnbindPadAxis(action, padIndex, axis)` — remove a pad-axis binding.
int8_t rt_action_unbind_pad_axis(rt_string action, int64_t pad_index, int64_t axis) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_PAD_AXIS, axis, pad_index);
}

/// @brief `Action.BindPadButtonAxis(action, padIndex, button, value)` — bind pad button as axis
/// contribution.
///
/// Like `BindKeyAxis` but for gamepad buttons — useful for D-pad
/// directions on an axis (D-pad-Left → -1.0, D-pad-Right → +1.0).
int8_t rt_action_bind_pad_button_axis(rt_string action,
                                      int64_t pad_index,
                                      int64_t button,
                                      double value) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_PAD_BUTTON_AXIS, button, pad_index, value);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

/// @brief Check if an action was just pressed this frame (edge-triggered).
/// @details Returns 1 only on the single frame where the action transitions
///   from not-held to held. Use this for jump, shoot, menu confirm — actions
///   that should trigger once per press, not continuously.
/// @param action Name of the action (e.g., "jump"). Must be defined first.
/// @return 1 if pressed this frame, 0 otherwise.
int8_t rt_action_pressed(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a ? a->pressed : 0;
}

/// @brief Check if an action was just released this frame (edge-triggered).
/// @details Returns 1 only on the single frame where the action transitions
///   from held to not-held. Use for "on key up" behaviors.
/// @param action Action name.
/// @return 1 if released this frame, 0 otherwise.
int8_t rt_action_released(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a ? a->released : 0;
}

/// @brief Check if an action is currently held down (level-triggered).
/// @details Returns 1 every frame the action is active. Use for movement,
///   charging, or any continuous action.
/// @param action Action name.
/// @return 1 if held, 0 if not.
int8_t rt_action_held(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a ? a->held : 0;
}

/// @brief Get the strength of a button action (0.0 or 1.0 for digital inputs).
/// @details For digital inputs (keyboard/buttons), returns 1.0 when held, 0.0
///   when released. For analog inputs (gamepad triggers), returns the axis value.
/// @param action Action name.
/// @return Strength value in [0.0, 1.0].
double rt_action_strength(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a && a->held ? 1.0 : 0.0;
}

/// @brief Get the clamped axis value for an axis-type action.
/// @details Returns a value in [-1.0, 1.0] for axis inputs (gamepad sticks,
///   mouse movement). Button bindings contribute their configured `value` field
///   (typically ±1.0). The result is clamped to [-1.0, 1.0].
/// @param action Action name (must be defined as axis with DefineAxis).
/// @return Axis value clamped to [-1.0, 1.0], or 0.0 if action not found.
double rt_action_axis(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a ? clamp_axis(a->axis_value) : 0.0;
}

/// @brief Get the raw (unclamped) axis value for an axis-type action.
/// @details Like rt_action_axis() but without clamping. Raw values can exceed
///   [-1.0, 1.0] when multiple bindings contribute simultaneously.
/// @param action Action name.
/// @return Raw accumulated axis value, or 0.0 if not found.
double rt_action_axis_raw(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    return a ? a->axis_value : 0.0;
}

/// @brief `Action.List` — return a `seq<str>` of every defined action's name.
///
/// Useful for binding-config UIs and serialization. Order matches
/// the internal linked-list order (newest-first).
void *rt_action_list(void) {
    RT_ASSERT_MAIN_THREAD();
    void *seq = rt_seq_new();
    Action *a = g_actions;
    while (a) {
        rt_string name = rt_string_from_bytes(a->name, strlen(a->name));
        rt_seq_push(seq, (void *)name);
        a = a->next;
    }
    return seq;
}

/// @brief `Action.BindingsStr(action)` — human-readable description of all bindings.
///
/// Comma-separated list like "Space, Mouse Left, Pad A, Ctrl+S".
/// Useful for "press X to jump"-style on-screen prompts. Capped at
/// a 1024-byte buffer; bindings beyond that are silently truncated.
rt_string rt_action_bindings_str(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return rt_str_empty();

    // Build a description of all bindings
    char buffer[1024];
    buffer[0] = '\0';
    int pos = 0;
    int first = 1;

    Binding *b = a->bindings;
    while (b && pos < 1000) {
        if (!first && pos < 998) {
            buffer[pos++] = ',';
            buffer[pos++] = ' ';
        }
        first = 0;

        const char *desc = "";
        char temp[64];

        switch (b->type) {
            case BIND_KEY: {
                rt_string key_name = rt_keyboard_key_name(b->code);
                int64_t key_len = rt_str_len(key_name);
                if (key_len > 0 && key_len < 60) {
                    memcpy(temp, key_name->data, (size_t)key_len);
                    temp[key_len] = '\0';
                    desc = temp;
                } else {
                    desc = "Key";
                }
                break;
            }
            case BIND_MOUSE_BUTTON:
                switch (b->code) {
                    case VIPER_MOUSE_BUTTON_LEFT:
                        desc = "Mouse Left";
                        break;
                    case VIPER_MOUSE_BUTTON_RIGHT:
                        desc = "Mouse Right";
                        break;
                    case VIPER_MOUSE_BUTTON_MIDDLE:
                        desc = "Mouse Middle";
                        break;
                    default:
                        desc = "Mouse Button";
                        break;
                }
                break;
            case BIND_MOUSE_X:
                desc = "Mouse X";
                break;
            case BIND_MOUSE_Y:
                desc = "Mouse Y";
                break;
            case BIND_SCROLL_X:
                desc = "Scroll X";
                break;
            case BIND_SCROLL_Y:
                desc = "Scroll Y";
                break;
            case BIND_PAD_BUTTON:
            case BIND_PAD_BUTTON_AXIS:
                switch (b->code) {
                    case VIPER_PAD_A:
                        desc = "Pad A";
                        break;
                    case VIPER_PAD_B:
                        desc = "Pad B";
                        break;
                    case VIPER_PAD_X:
                        desc = "Pad X";
                        break;
                    case VIPER_PAD_Y:
                        desc = "Pad Y";
                        break;
                    case VIPER_PAD_LB:
                        desc = "Pad LB";
                        break;
                    case VIPER_PAD_RB:
                        desc = "Pad RB";
                        break;
                    case VIPER_PAD_UP:
                        desc = "Pad Up";
                        break;
                    case VIPER_PAD_DOWN:
                        desc = "Pad Down";
                        break;
                    case VIPER_PAD_LEFT:
                        desc = "Pad Left";
                        break;
                    case VIPER_PAD_RIGHT:
                        desc = "Pad Right";
                        break;
                    case VIPER_PAD_START:
                        desc = "Pad Start";
                        break;
                    case VIPER_PAD_BACK:
                        desc = "Pad Back";
                        break;
                    default:
                        desc = "Pad Button";
                        break;
                }
                break;
            case BIND_PAD_AXIS:
                switch (b->code) {
                    case VIPER_AXIS_LEFT_X:
                        desc = "Left Stick X";
                        break;
                    case VIPER_AXIS_LEFT_Y:
                        desc = "Left Stick Y";
                        break;
                    case VIPER_AXIS_RIGHT_X:
                        desc = "Right Stick X";
                        break;
                    case VIPER_AXIS_RIGHT_Y:
                        desc = "Right Stick Y";
                        break;
                    case VIPER_AXIS_LEFT_TRIGGER:
                        desc = "Left Trigger";
                        break;
                    case VIPER_AXIS_RIGHT_TRIGGER:
                        desc = "Right Trigger";
                        break;
                    default:
                        desc = "Pad Axis";
                        break;
                }
                break;
            case BIND_CHORD: {
                // Build "Key1+Key2+Key3" style description
                int ci;
                int tpos = 0;
                temp[0] = '\0';
                for (ci = 0; ci < b->chord_len && tpos < 58; ci++) {
                    if (ci > 0 && tpos < 57)
                        temp[tpos++] = '+';
                    rt_string kn = rt_keyboard_key_name(b->chord_keys[ci]);
                    int64_t kl = rt_str_len(kn);
                    if (kl > 0 && tpos + kl < 60) {
                        memcpy(temp + tpos, kn->data, (size_t)kl);
                        tpos += (int)kl;
                    }
                }
                temp[tpos] = '\0';
                desc = temp;
                break;
            }
            default:
                desc = "Unknown";
                break;
        }

        size_t len = strlen(desc);
        if (pos + (int)len < 1000) {
            memcpy(buffer + pos, desc, len);
            pos += (int)len;
        }

        b = b->next;
    }
    buffer[pos] = '\0';

    return rt_string_from_bytes(buffer, (size_t)pos);
}

/// @brief `Action.BindingCount(action)` — total number of bindings on this action.
int64_t rt_action_binding_count(rt_string action) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = find_action_str(action);
    if (!a)
        return 0;

    int64_t count = 0;
    Binding *b = a->bindings;
    while (b) {
        count++;
        b = b->next;
    }
    return count;
}

/// @brief `Action.KeyBoundTo(key)` — name of the first action bound to `key`, or "".
rt_string rt_action_key_bound_to(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = g_actions;
    while (a) {
        Binding *b = a->bindings;
        while (b) {
            if (b->type == BIND_KEY && b->code == key)
                return rt_string_from_bytes(a->name, strlen(a->name));
            b = b->next;
        }
        a = a->next;
    }
    return rt_str_empty();
}

/// @brief `Action.MouseBoundTo(button)` — name of action bound to mouse button, or "".
rt_string rt_action_mouse_bound_to(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = g_actions;
    while (a) {
        Binding *b = a->bindings;
        while (b) {
            if (b->type == BIND_MOUSE_BUTTON && b->code == button)
                return rt_string_from_bytes(a->name, strlen(a->name));
            b = b->next;
        }
        a = a->next;
    }
    return rt_str_empty();
}

/// @brief `Action.PadButtonBoundTo(padIndex, button)` — name of action bound to a pad button.
///
/// Matches both regular pad-button bindings and pad-button-axis
/// bindings. `pad_index = -1` ("any pad") always matches.
rt_string rt_action_pad_button_bound_to(int64_t pad_index, int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    Action *a = g_actions;
    while (a) {
        Binding *b = a->bindings;
        while (b) {
            if ((b->type == BIND_PAD_BUTTON || b->type == BIND_PAD_BUTTON_AXIS) &&
                b->code == button && (b->pad_index == pad_index || b->pad_index == -1))
                return rt_string_from_bytes(a->name, strlen(a->name));
            b = b->next;
        }
        a = a->next;
    }
    return rt_str_empty();
}

/// @brief `Axis.LeftX` — gamepad left stick X-axis constant.
int64_t rt_action_axis_left_x(void) {
    return VIPER_AXIS_LEFT_X;
}

/// @brief `Axis.LeftY` — gamepad left stick Y-axis constant.
int64_t rt_action_axis_left_y(void) {
    return VIPER_AXIS_LEFT_Y;
}

/// @brief `Axis.RightX` — gamepad right stick X-axis constant.
int64_t rt_action_axis_right_x(void) {
    return VIPER_AXIS_RIGHT_X;
}

/// @brief `Axis.RightY` — gamepad right stick Y-axis constant.
int64_t rt_action_axis_right_y(void) {
    return VIPER_AXIS_RIGHT_Y;
}

/// @brief `Axis.LeftTrigger` — gamepad left analog trigger constant.
int64_t rt_action_axis_left_trigger(void) {
    return VIPER_AXIS_LEFT_TRIGGER;
}

/// @brief `Axis.RightTrigger` — gamepad right analog trigger constant.
int64_t rt_action_axis_right_trigger(void) {
    return VIPER_AXIS_RIGHT_TRIGGER;
}
