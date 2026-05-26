//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_action.c
// Purpose: Input action mapping system for Viper games. Provides named logical
//   actions (e.g. "jump", "fire") that are bound to one or more physical input
//   sources: keyboard keys, mouse buttons, mouse axes, scroll axes, gamepad
//   buttons, gamepad axes, or multi-key chords. Actions are polled once per
//   frame and expose pressed/released/held states and a normalized axis value.
//
// Key invariants:
//   - Actions are stored as a global singly-linked list; names must be unique.
//   - Each action may have multiple Binding records (any matching binding wins).
//   - Chord bindings (BIND_CHORD) require all keys held simultaneously; trigger
//     fires on the frame the last key is pressed.
//   - Axis actions accumulate contributions from all matching bindings each
//     frame; button-style bindings contribute a fixed `value` field.
//   - rt_action_update() must be called once per frame before any query.
//   - rt_action_destroy_all() resets global state; safe to call at shutdown.
//
// Ownership/Lifetime:
//   - Action and Binding nodes are heap-allocated with malloc/strdup; freed by
//     rt_action_destroy() or rt_action_destroy_all().
//   - Not GC-managed — caller is responsible for cleanup.
//
// Links: src/runtime/graphics/rt_action.h (public API),
//        src/runtime/graphics/rt_input.h (keyboard/mouse query layer),
//        src/runtime/graphics/rt_input_pad.h (gamepad query layer),
//        src/runtime/graphics/rt_keychord.h (chord detection primitives)
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
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

//=========================================================================
// Internal Data Structures
//=========================================================================

// Binding source types
typedef enum {
    BIND_NONE = 0,
    BIND_KEY,             // Keyboard key
    BIND_MOUSE_BUTTON,    // Mouse button
    BIND_MOUSE_X,         // Mouse X delta
    BIND_MOUSE_Y,         // Mouse Y delta
    BIND_SCROLL_X,        // Mouse scroll X
    BIND_SCROLL_Y,        // Mouse scroll Y
    BIND_PAD_BUTTON,      // Gamepad button
    BIND_PAD_AXIS,        // Gamepad axis
    BIND_PAD_BUTTON_AXIS, // Gamepad button as axis
    BIND_CHORD            // Multi-key chord (e.g., Ctrl+Shift+S)
} BindingType;

// Maximum number of keys in a chord
#define MAX_CHORD_KEYS 8

// A single input binding
typedef struct Binding {
    BindingType type;
    int64_t code;      // Key/button/axis code
    int64_t pad_index; // Controller index (-1 for any)
    double value;      // Axis value for key/button bindings, scale for analog
    // Chord data (only used when type == BIND_CHORD)
    int64_t chord_keys[MAX_CHORD_KEYS];
    int32_t chord_len; // Number of keys in the chord
    struct Binding *next;
} Binding;

// An action (button or axis)
typedef struct Action {
    char *name;
    int8_t is_axis;
    Binding *bindings;
    // Cached state (updated each frame)
    int8_t pressed;
    int8_t released;
    int8_t held;
    double axis_value;
    struct Action *next;
} Action;

// Global state
static Action *g_actions = NULL;
static int8_t g_initialized = 0;

//=========================================================================
// Internal Helpers
//=========================================================================

/// @brief Linear-scan the global action list by C-string name.
///
/// Returns NULL on miss. Used by the C-string-keyed setter/getter
/// surface (e.g., loading bindings from a config file).
static Action *find_action(const char *name) {
    if (!name)
        return NULL;
    Action *a = g_actions;
    while (a) {
        if (strcmp(a->name, name) == 0)
            return a;
        a = a->next;
    }
    return NULL;
}

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

/// @brief Allocate a new binding node populated with the given fields.
///
/// `chord_keys` and `chord_len` are left zero — chord bindings are
/// constructed directly by `BindChord` callers, not via this helper.
static Binding *create_binding(BindingType type, int64_t code, int64_t pad_index, double value) {
    Binding *b = (Binding *)malloc(sizeof(Binding));
    if (!b)
        return NULL;
    b->type = type;
    b->code = code;
    b->pad_index = pad_index;
    b->value = value;
    b->next = NULL;
    return b;
}

/// @brief Push a binding onto the head of an action's binding list.
///
/// LIFO insert (most-recently-added is matched first by the update
/// loop). Order doesn't affect correctness — any matching binding
/// satisfies a button query.
static void add_binding(Action *action, Binding *binding) {
    binding->next = action->bindings;
    action->bindings = binding;
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

// Per-source query thunks — wrap the `rt_keyboard_*` / `rt_mouse_*` /
// `rt_pad_*` getters with the thin signatures the binding update loop
// expects. Pad helpers also implement the `pad_index < 0` "any
// connected controller" fallback.

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

//=========================================================================
// Action System Lifecycle
//=========================================================================

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

//=========================================================================
// Action Definition
//=========================================================================

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

//=========================================================================
// Keyboard Bindings
//=========================================================================

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

//=========================================================================
// Key Chord/Combo Bindings
//=========================================================================

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

//=========================================================================
// Mouse Bindings
//=========================================================================

// Mouse-binding API — buttons are button-style only; XY/scroll are
// axis-only. Each helper validates that the action's kind matches and
// returns 0 otherwise.

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

//=========================================================================
// Gamepad Bindings
//=========================================================================

// Gamepad-binding API — analogous to mouse/keyboard but with a
// `pad_index` parameter (use -1 for "any connected controller").

/// @brief `Action.BindPadButton(action, padIndex, button)` — bind a gamepad button to a button action.
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

/// @brief `Action.BindPadButtonAxis(action, padIndex, button, value)` — bind pad button as axis contribution.
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

//=========================================================================
// Button Action State Queries
//=========================================================================

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

//=========================================================================
// Axis Action Queries
//=========================================================================

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

//=========================================================================
// Binding Introspection
//=========================================================================

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

//=========================================================================
// Conflict Detection
//=========================================================================

// Conflict-detection helpers — given a physical input, return the
// name of the first action bound to it. Useful for binding-config
// UIs to warn about double-binding.

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

//=========================================================================
// Preset Helpers (internal — work with C strings, no rt_string allocation)
//=========================================================================

// Internal preset helpers — work directly with C strings (no
// `rt_string` allocation), so the preset loaders below can use string
// literals and stay compact. Skip silently if an action already exists,
// so calling multiple presets that share names is safe.

/// @brief Internal: define a button or axis action by C-string name.
static Action *define_action_cstr(const char *name, int8_t is_axis) {
    if (find_action(name))
        return NULL; /* Already exists — skip silently */
    Action *a = (Action *)malloc(sizeof(Action));
    if (!a)
        return NULL;
    a->name = strdup(name);
    if (!a->name) {
        free(a);
        return NULL;
    }
    a->is_axis = is_axis;
    a->bindings = NULL;
    a->pressed = 0;
    a->released = 0;
    a->held = 0;
    a->axis_value = 0.0;
    a->next = g_actions;
    g_actions = a;
    return a;
}

/// @brief Internal: bind a key to a button action by C-string name.
static void bind_key_to(const char *name, int64_t key) {
    Action *a = find_action(name);
    if (!a || a->is_axis)
        return;
    Binding *b = create_binding(BIND_KEY, key, 0, 1.0);
    if (b)
        add_binding(a, b);
}

/// @brief Internal: bind a key to an axis action with the given contribution.
static void bind_key_axis_to(const char *name, int64_t key, double value) {
    Action *a = find_action(name);
    if (!a || !a->is_axis)
        return;
    Binding *b = create_binding(BIND_KEY, key, 0, value);
    if (b)
        add_binding(a, b);
}

/// @brief Internal: bind any-pad button to a button action.
static void bind_pad_to(const char *name, int64_t button) {
    Action *a = find_action(name);
    if (!a || a->is_axis)
        return;
    Binding *b = create_binding(BIND_PAD_BUTTON, button, -1, 1.0);
    if (b)
        add_binding(a, b);
}

/// @brief Internal: bind any-pad analog axis to an axis action.
static void bind_pad_axis_to(const char *name, int64_t axis, double scale) {
    Action *a = find_action(name);
    if (!a || !a->is_axis)
        return;
    Binding *b = create_binding(BIND_PAD_AXIS, axis, -1, scale);
    if (b)
        add_binding(a, b);
}

/// @brief Internal: bind any-pad button to an axis with a fixed contribution.
static void bind_pad_button_axis_to(const char *name, int64_t button, double value) {
    Action *a = find_action(name);
    if (!a || !a->is_axis)
        return;
    Binding *b = create_binding(BIND_PAD_BUTTON_AXIS, button, -1, value);
    if (b)
        add_binding(a, b);
}

//=========================================================================
// Preset: standard_movement
//   Button: move_up, move_down, move_left, move_right
//   Axis:   move_x (-1..+1), move_y (-1..+1)
//   Keys:   WASD + arrows    Pad: D-pad + left stick
//=========================================================================

/// @brief Preset: WASD/arrows/D-pad/left-stick → `move_*` actions.
///
/// Defines six actions: 4 button (`move_up/down/left/right`) and 2 axis
/// (`move_x`, `move_y`). Binds the standard physical inputs to all of
/// them so a script can use whichever style fits its needs.
static void load_preset_standard_movement(void) {
    /* Button actions */
    define_action_cstr("move_up", 0);
    define_action_cstr("move_down", 0);
    define_action_cstr("move_left", 0);
    define_action_cstr("move_right", 0);

    /* Axis actions */
    define_action_cstr("move_x", 1);
    define_action_cstr("move_y", 1);

    /* WASD */
    bind_key_to("move_up", VIPER_KEY_W);
    bind_key_to("move_down", VIPER_KEY_S);
    bind_key_to("move_left", VIPER_KEY_A);
    bind_key_to("move_right", VIPER_KEY_D);

    /* Arrow keys */
    bind_key_to("move_up", VIPER_KEY_UP);
    bind_key_to("move_down", VIPER_KEY_DOWN);
    bind_key_to("move_left", VIPER_KEY_LEFT);
    bind_key_to("move_right", VIPER_KEY_RIGHT);

    /* D-pad (any controller) */
    bind_pad_to("move_up", VIPER_PAD_UP);
    bind_pad_to("move_down", VIPER_PAD_DOWN);
    bind_pad_to("move_left", VIPER_PAD_LEFT);
    bind_pad_to("move_right", VIPER_PAD_RIGHT);

    /* Axis: WASD as digital */
    bind_key_axis_to("move_x", VIPER_KEY_A, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_D, 1.0);
    bind_key_axis_to("move_x", VIPER_KEY_LEFT, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_RIGHT, 1.0);
    bind_key_axis_to("move_y", VIPER_KEY_W, -1.0);
    bind_key_axis_to("move_y", VIPER_KEY_S, 1.0);
    bind_key_axis_to("move_y", VIPER_KEY_UP, -1.0);
    bind_key_axis_to("move_y", VIPER_KEY_DOWN, 1.0);

    /* Axis: left stick */
    bind_pad_axis_to("move_x", VIPER_AXIS_LEFT_X, 1.0);
    bind_pad_axis_to("move_y", VIPER_AXIS_LEFT_Y, 1.0);

    /* Axis: D-pad as digital axis */
    bind_pad_button_axis_to("move_x", VIPER_PAD_LEFT, -1.0);
    bind_pad_button_axis_to("move_x", VIPER_PAD_RIGHT, 1.0);
    bind_pad_button_axis_to("move_y", VIPER_PAD_UP, -1.0);
    bind_pad_button_axis_to("move_y", VIPER_PAD_DOWN, 1.0);
}

//=========================================================================
// Preset: menu_navigation
//   Button: menu_up, menu_down, menu_left, menu_right, confirm, back
//   Keys:   arrows+WASD, Enter+Space, Escape    Pad: D-pad, A, B
//=========================================================================

/// @brief Preset: arrow/D-pad navigation + Enter/Space + Esc → `menu_*`/`confirm`/`back`.
static void load_preset_menu_navigation(void) {
    define_action_cstr("menu_up", 0);
    define_action_cstr("menu_down", 0);
    define_action_cstr("menu_left", 0);
    define_action_cstr("menu_right", 0);
    define_action_cstr("confirm", 0);
    define_action_cstr("back", 0);

    /* Arrow keys */
    bind_key_to("menu_up", VIPER_KEY_UP);
    bind_key_to("menu_down", VIPER_KEY_DOWN);
    bind_key_to("menu_left", VIPER_KEY_LEFT);
    bind_key_to("menu_right", VIPER_KEY_RIGHT);

    /* WASD */
    bind_key_to("menu_up", VIPER_KEY_W);
    bind_key_to("menu_down", VIPER_KEY_S);
    bind_key_to("menu_left", VIPER_KEY_A);
    bind_key_to("menu_right", VIPER_KEY_D);

    /* Confirm / Back */
    bind_key_to("confirm", VIPER_KEY_ENTER);
    bind_key_to("confirm", VIPER_KEY_SPACE);
    bind_key_to("back", VIPER_KEY_ESCAPE);

    /* D-pad (any controller) */
    bind_pad_to("menu_up", VIPER_PAD_UP);
    bind_pad_to("menu_down", VIPER_PAD_DOWN);
    bind_pad_to("menu_left", VIPER_PAD_LEFT);
    bind_pad_to("menu_right", VIPER_PAD_RIGHT);

    /* Gamepad confirm / back */
    bind_pad_to("confirm", VIPER_PAD_A);
    bind_pad_to("back", VIPER_PAD_B);
}

//=========================================================================
// Preset: platformer
//   Button: move_left, move_right, jump, shoot, pause
//   Axis:   move_x (-1..+1)
//   Keys:   A/D+arrows, Space/W/Up, J/X, Escape
//   Pad:    D-pad, A, X, Start + left stick
//=========================================================================

/// @brief Preset: 2D platformer controls — `move_left/right`, `jump`, `shoot`, `pause` + `move_x` axis.
static void load_preset_platformer(void) {
    define_action_cstr("move_left", 0);
    define_action_cstr("move_right", 0);
    define_action_cstr("jump", 0);
    define_action_cstr("shoot", 0);
    define_action_cstr("pause", 0);
    define_action_cstr("move_x", 1);

    /* Keys */
    bind_key_to("move_left", VIPER_KEY_A);
    bind_key_to("move_left", VIPER_KEY_LEFT);
    bind_key_to("move_right", VIPER_KEY_D);
    bind_key_to("move_right", VIPER_KEY_RIGHT);
    bind_key_to("jump", VIPER_KEY_SPACE);
    bind_key_to("jump", VIPER_KEY_W);
    bind_key_to("jump", VIPER_KEY_UP);
    bind_key_to("shoot", VIPER_KEY_J);
    bind_key_to("shoot", VIPER_KEY_X);
    bind_key_to("pause", VIPER_KEY_ESCAPE);

    /* Gamepad */
    bind_pad_to("move_left", VIPER_PAD_LEFT);
    bind_pad_to("move_right", VIPER_PAD_RIGHT);
    bind_pad_to("jump", VIPER_PAD_A);
    bind_pad_to("shoot", VIPER_PAD_X);
    bind_pad_to("pause", VIPER_PAD_START);

    /* Axis: left/right */
    bind_key_axis_to("move_x", VIPER_KEY_A, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_D, 1.0);
    bind_key_axis_to("move_x", VIPER_KEY_LEFT, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_RIGHT, 1.0);
    bind_pad_axis_to("move_x", VIPER_AXIS_LEFT_X, 1.0);
    bind_pad_button_axis_to("move_x", VIPER_PAD_LEFT, -1.0);
    bind_pad_button_axis_to("move_x", VIPER_PAD_RIGHT, 1.0);
}

//=========================================================================
// Preset: topdown
//   Button: move_up, move_down, move_left, move_right, fire, pause
//   Axis:   move_x, move_y (-1..+1)
//   Keys:   WASD+arrows, Space/J, Escape
//   Pad:    D-pad + left stick, A, Start
//=========================================================================

/// @brief Preset: top-down shooter controls — 4-directional movement + `fire`/`pause`.
static void load_preset_topdown(void) {
    define_action_cstr("move_up", 0);
    define_action_cstr("move_down", 0);
    define_action_cstr("move_left", 0);
    define_action_cstr("move_right", 0);
    define_action_cstr("fire", 0);
    define_action_cstr("pause", 0);
    define_action_cstr("move_x", 1);
    define_action_cstr("move_y", 1);

    /* WASD */
    bind_key_to("move_up", VIPER_KEY_W);
    bind_key_to("move_down", VIPER_KEY_S);
    bind_key_to("move_left", VIPER_KEY_A);
    bind_key_to("move_right", VIPER_KEY_D);

    /* Arrow keys */
    bind_key_to("move_up", VIPER_KEY_UP);
    bind_key_to("move_down", VIPER_KEY_DOWN);
    bind_key_to("move_left", VIPER_KEY_LEFT);
    bind_key_to("move_right", VIPER_KEY_RIGHT);

    /* Fire / Pause */
    bind_key_to("fire", VIPER_KEY_SPACE);
    bind_key_to("fire", VIPER_KEY_J);
    bind_key_to("pause", VIPER_KEY_ESCAPE);

    /* D-pad (any controller) */
    bind_pad_to("move_up", VIPER_PAD_UP);
    bind_pad_to("move_down", VIPER_PAD_DOWN);
    bind_pad_to("move_left", VIPER_PAD_LEFT);
    bind_pad_to("move_right", VIPER_PAD_RIGHT);
    bind_pad_to("fire", VIPER_PAD_A);
    bind_pad_to("pause", VIPER_PAD_START);

    /* Axis: WASD + arrows as digital */
    bind_key_axis_to("move_x", VIPER_KEY_A, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_D, 1.0);
    bind_key_axis_to("move_x", VIPER_KEY_LEFT, -1.0);
    bind_key_axis_to("move_x", VIPER_KEY_RIGHT, 1.0);
    bind_key_axis_to("move_y", VIPER_KEY_W, -1.0);
    bind_key_axis_to("move_y", VIPER_KEY_S, 1.0);
    bind_key_axis_to("move_y", VIPER_KEY_UP, -1.0);
    bind_key_axis_to("move_y", VIPER_KEY_DOWN, 1.0);

    /* Axis: left stick */
    bind_pad_axis_to("move_x", VIPER_AXIS_LEFT_X, 1.0);
    bind_pad_axis_to("move_y", VIPER_AXIS_LEFT_Y, 1.0);

    /* Axis: D-pad as digital axis */
    bind_pad_button_axis_to("move_x", VIPER_PAD_LEFT, -1.0);
    bind_pad_button_axis_to("move_x", VIPER_PAD_RIGHT, 1.0);
    bind_pad_button_axis_to("move_y", VIPER_PAD_UP, -1.0);
    bind_pad_button_axis_to("move_y", VIPER_PAD_DOWN, 1.0);
}

//=========================================================================
// Public Preset API
//=========================================================================

/// @brief `Action.LoadPreset(name)` — apply a pre-configured action set.
///
/// Defines actions and binds standard keyboard + gamepad inputs in one
/// call. Available presets:
///   - `"standard_movement"` — 4-directional move + axis variant.
///   - `"menu_navigation"` — menu_up/down/left/right + confirm + back.
///   - `"platformer"` — left/right + jump + shoot + pause + axis.
///   - `"topdown"` — 4-directional + fire + pause + axis variant.
/// Auto-initializes the action system if not already done. Returns
/// 0 if `name` doesn't match any preset.
int8_t rt_action_load_preset(rt_string preset_name) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_initialized)
        rt_action_init();

    if (!preset_name || rt_str_len(preset_name) == 0)
        return 0;

    int64_t len = rt_str_len(preset_name);
    const char *data = preset_name->data;

    if (len == 17 && memcmp(data, "standard_movement", 17) == 0) {
        load_preset_standard_movement();
        return 1;
    }
    if (len == 15 && memcmp(data, "menu_navigation", 15) == 0) {
        load_preset_menu_navigation();
        return 1;
    }
    if (len == 10 && memcmp(data, "platformer", 10) == 0) {
        load_preset_platformer();
        return 1;
    }
    if (len == 7 && memcmp(data, "topdown", 7) == 0) {
        load_preset_topdown();
        return 1;
    }

    return 0;
}

//=========================================================================
// Axis Constant Getters
//=========================================================================

// Constant getters — expose the `VIPER_AXIS_*` enum values to Zia
// callers (which can't read C `#define`s directly).

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

//=========================================================================
// Persistence (Save/Load)
//=========================================================================

/// @brief Map a `BindingType` enum to the JSON serialization tag.
static const char *binding_type_name(BindingType type) {
    switch (type) {
        case BIND_KEY:
            return "key";
        case BIND_MOUSE_BUTTON:
            return "mouse";
        case BIND_MOUSE_X:
            return "mouse_x";
        case BIND_MOUSE_Y:
            return "mouse_y";
        case BIND_SCROLL_X:
            return "scroll_x";
        case BIND_SCROLL_Y:
            return "scroll_y";
        case BIND_PAD_BUTTON:
            return "pad_button";
        case BIND_PAD_AXIS:
            return "pad_axis";
        case BIND_PAD_BUTTON_AXIS:
            return "pad_button_axis";
        case BIND_CHORD:
            return "chord";
        default:
            return "unknown";
    }
}

/// @brief Reverse map: JSON tag string → `BindingType` enum.
///
/// Returns `BIND_NONE` for unknown tags so the loader can skip them.
static BindingType binding_type_from_name(const char *name) {
    if (strcmp(name, "key") == 0)
        return BIND_KEY;
    if (strcmp(name, "mouse") == 0)
        return BIND_MOUSE_BUTTON;
    if (strcmp(name, "mouse_x") == 0)
        return BIND_MOUSE_X;
    if (strcmp(name, "mouse_y") == 0)
        return BIND_MOUSE_Y;
    if (strcmp(name, "scroll_x") == 0)
        return BIND_SCROLL_X;
    if (strcmp(name, "scroll_y") == 0)
        return BIND_SCROLL_Y;
    if (strcmp(name, "pad_button") == 0)
        return BIND_PAD_BUTTON;
    if (strcmp(name, "pad_axis") == 0)
        return BIND_PAD_AXIS;
    if (strcmp(name, "pad_button_axis") == 0)
        return BIND_PAD_BUTTON_AXIS;
    if (strcmp(name, "chord") == 0)
        return BIND_CHORD;
    return BIND_NONE;
}

/// @brief Append a JSON-quoted-string-literal version of `str` to a builder.
///
/// Handles the standard JSON escape characters (`"`, `\\`, `\\n`,
/// `\\r`, `\\t`). Non-ASCII bytes pass through as-is — the persistence
/// format assumes UTF-8 throughout.
static void sb_append_json_string(rt_string_builder *sb, const char *str) {
    rt_sb_append_cstr(sb, "\"");
    while (*str) {
        char c = *str++;
        switch (c) {
            case '"':
                rt_sb_append_cstr(sb, "\\\"");
                break;
            case '\\':
                rt_sb_append_cstr(sb, "\\\\");
                break;
            case '\n':
                rt_sb_append_cstr(sb, "\\n");
                break;
            case '\r':
                rt_sb_append_cstr(sb, "\\r");
                break;
            case '\t':
                rt_sb_append_cstr(sb, "\\t");
                break;
            default:
                rt_sb_append_bytes(sb, &c, 1);
                break;
        }
    }
    rt_sb_append_cstr(sb, "\"");
}

/// @brief `Action.Save` — serialize all action+binding state to JSON.
///
/// Format: `{"actions":[{"name","type","bindings":[{"type","code","pad","value","keys":[...]}]}]}`.
/// `keys` is only present for chord bindings. The result is a fresh
/// `rt_string` that can be saved to disk and round-tripped via
/// `rt_action_load`.
rt_string rt_action_save(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_string_builder sb;
    int8_t first_action;
    rt_sb_init(&sb);

    rt_sb_append_cstr(&sb, "{\"actions\":[");

    first_action = 1;
    {
        Action *a = g_actions;
        while (a) {
            int8_t first_binding;
            if (!first_action)
                rt_sb_append_cstr(&sb, ",");
            first_action = 0;

            rt_sb_append_cstr(&sb, "{\"name\":");
            sb_append_json_string(&sb, a->name);
            rt_sb_append_cstr(&sb, ",\"type\":");
            rt_sb_append_cstr(&sb, a->is_axis ? "\"axis\"" : "\"button\"");
            rt_sb_append_cstr(&sb, ",\"bindings\":[");

            first_binding = 1;
            {
                Binding *b = a->bindings;
                while (b) {
                    if (!first_binding)
                        rt_sb_append_cstr(&sb, ",");
                    first_binding = 0;

                    rt_sb_append_cstr(&sb, "{\"type\":");
                    sb_append_json_string(&sb, binding_type_name(b->type));
                    rt_sb_append_cstr(&sb, ",\"code\":");
                    rt_sb_append_int(&sb, b->code);
                    rt_sb_append_cstr(&sb, ",\"pad\":");
                    rt_sb_append_int(&sb, b->pad_index);
                    rt_sb_append_cstr(&sb, ",\"value\":");
                    rt_sb_append_double(&sb, b->value);
                    if (b->type == BIND_CHORD && b->chord_len > 0) {
                        int32_t ci;
                        rt_sb_append_cstr(&sb, ",\"keys\":[");
                        for (ci = 0; ci < b->chord_len; ci++) {
                            if (ci > 0)
                                rt_sb_append_cstr(&sb, ",");
                            rt_sb_append_int(&sb, b->chord_keys[ci]);
                        }
                        rt_sb_append_cstr(&sb, "]");
                    }
                    rt_sb_append_cstr(&sb, "}");
                    b = b->next;
                }
            }

            rt_sb_append_cstr(&sb, "]}");
            a = a->next;
        }
    }

    rt_sb_append_cstr(&sb, "]}");

    {
        rt_string result = rt_string_from_bytes(sb.data, sb.len);
        rt_sb_free(&sb);
        return result;
    }
}

/// @brief `Action.Load(json)` — restore action+binding state from a JSON string.
///
/// Inverse of `Save`. Clears existing actions before loading. Uses
/// the streaming JSON parser (`rt_json_stream`) so giant configs
/// don't allocate a parse tree. Returns 0 on any structural error
/// (existing actions stay cleared in that case — fix the JSON before
/// calling again).
int8_t rt_action_load(rt_string json) {
    RT_ASSERT_MAIN_THREAD();
    void *parser;
    int64_t tok;

    if (!json)
        return 0;

    parser = rt_json_stream_new(json);
    if (!parser)
        return 0;

    /* Expect { */
    tok = rt_json_stream_next(parser);
    if (tok != RT_JSON_TOK_OBJECT_START)
        return 0;

    /* Expect key "actions" */
    tok = rt_json_stream_next(parser);
    if (tok != RT_JSON_TOK_KEY)
        return 0;

    /* Expect [ */
    tok = rt_json_stream_next(parser);
    if (tok != RT_JSON_TOK_ARRAY_START)
        return 0;

    /* Clear existing actions before loading */
    rt_action_clear();
    if (!g_initialized)
        rt_action_init();

    /* Parse each action object */
    tok = rt_json_stream_next(parser);
    while (tok == RT_JSON_TOK_OBJECT_START) {
        char action_name[256];
        int8_t is_axis = 0;
        action_name[0] = '\0';

        /* Parse action fields */
        tok = rt_json_stream_next(parser);
        while (tok == RT_JSON_TOK_KEY) {
            rt_string key = rt_json_stream_string_value(parser);
            const char *key_cstr = rt_string_cstr(key);

            if (strcmp(key_cstr, "name") == 0) {
                tok = rt_json_stream_next(parser);
                if (tok == RT_JSON_TOK_STRING) {
                    rt_string val = rt_json_stream_string_value(parser);
                    const char *val_cstr = rt_string_cstr(val);
                    size_t len = strlen(val_cstr);
                    if (len >= sizeof(action_name))
                        len = sizeof(action_name) - 1;
                    memcpy(action_name, val_cstr, len);
                    action_name[len] = '\0';
                }
            } else if (strcmp(key_cstr, "type") == 0) {
                tok = rt_json_stream_next(parser);
                if (tok == RT_JSON_TOK_STRING) {
                    rt_string val = rt_json_stream_string_value(parser);
                    is_axis = (strcmp(rt_string_cstr(val), "axis") == 0) ? 1 : 0;
                }
            } else if (strcmp(key_cstr, "bindings") == 0) {
                /* Define the action first */
                if (action_name[0] != '\0') {
                    rt_string name_str = rt_const_cstr(action_name);
                    if (is_axis)
                        rt_action_define_axis(name_str);
                    else
                        rt_action_define(name_str);
                }

                /* Parse bindings array */
                tok = rt_json_stream_next(parser);
                if (tok != RT_JSON_TOK_ARRAY_START)
                    return 0;

                tok = rt_json_stream_next(parser);
                while (tok == RT_JSON_TOK_OBJECT_START) {
                    BindingType btype = BIND_NONE;
                    int64_t code = 0;
                    int64_t pad = 0;
                    double value = 0.0;
                    int64_t chord_keys[MAX_CHORD_KEYS];
                    int32_t chord_len = 0;

                    /* Parse binding fields */
                    tok = rt_json_stream_next(parser);
                    while (tok == RT_JSON_TOK_KEY) {
                        rt_string bkey = rt_json_stream_string_value(parser);
                        const char *bkey_cstr = rt_string_cstr(bkey);

                        tok = rt_json_stream_next(parser);
                        if (strcmp(bkey_cstr, "type") == 0 && tok == RT_JSON_TOK_STRING) {
                            rt_string bval = rt_json_stream_string_value(parser);
                            btype = binding_type_from_name(rt_string_cstr(bval));
                        } else if (strcmp(bkey_cstr, "code") == 0 && tok == RT_JSON_TOK_NUMBER) {
                            code = (int64_t)rt_json_stream_number_value(parser);
                        } else if (strcmp(bkey_cstr, "pad") == 0 && tok == RT_JSON_TOK_NUMBER) {
                            pad = (int64_t)rt_json_stream_number_value(parser);
                        } else if (strcmp(bkey_cstr, "value") == 0 && tok == RT_JSON_TOK_NUMBER) {
                            value = rt_json_stream_number_value(parser);
                        } else if (strcmp(bkey_cstr, "keys") == 0 &&
                                   tok == RT_JSON_TOK_ARRAY_START) {
                            /* Parse chord keys array */
                            chord_len = 0;
                            tok = rt_json_stream_next(parser);
                            while (tok == RT_JSON_TOK_NUMBER && chord_len < MAX_CHORD_KEYS) {
                                chord_keys[chord_len++] =
                                    (int64_t)rt_json_stream_number_value(parser);
                                tok = rt_json_stream_next(parser);
                            }
                            /* tok should be ARRAY_END */
                        }
                        /* For unknown fields with nested containers, drain before advancing */
                        rt_json_stream_skip(parser);
                        tok = rt_json_stream_next(parser);
                    }
                    /* tok should be OBJECT_END for the binding */

                    /* Add binding to action */
                    if (btype != BIND_NONE && action_name[0] != '\0') {
                        Action *a = find_action(action_name);
                        if (a) {
                            Binding *b = create_binding(btype, code, pad, value);
                            if (b) {
                                if (btype == BIND_CHORD) {
                                    int32_t ci;
                                    b->chord_len = chord_len;
                                    for (ci = 0; ci < chord_len; ci++)
                                        b->chord_keys[ci] = chord_keys[ci];
                                }
                                add_binding(a, b);
                            }
                        }
                    }

                    tok = rt_json_stream_next(parser);
                }
                /* tok should be ARRAY_END for bindings */
            } else {
                /* Skip unknown field value — handles nested objects/arrays */
                rt_json_stream_next(parser);
                rt_json_stream_skip(parser);
            }

            tok = rt_json_stream_next(parser);
        }
        /* tok should be OBJECT_END for the action */

        tok = rt_json_stream_next(parser);
    }
    /* tok should be ARRAY_END for actions */

    return 1;
}
