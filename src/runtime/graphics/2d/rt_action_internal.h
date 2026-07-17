//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_action_internal.h
// Purpose: Shared data model and state for the input-action subsystem, split
//   across rt_action.c (core/runtime), rt_action_presets.c (built-in preset
//   bindings), and rt_action_io.c (JSON save/load). Defines the Action /
//   Binding records, the global action list, and the small list helpers used
//   by all three translation units.
//
// Key invariants:
//   - Actions live in a single global singly-linked list (g_actions); names
//     are unique.
//   - g_initialized guards one-time setup; reset by the core teardown path.
//   - List helpers are static inline so each translation unit gets an
//     internal-linkage copy (no exported symbol, no source duplication).
//
// Ownership/Lifetime:
//   - Action / Binding nodes are malloc/strdup-allocated and freed by the core
//     teardown functions; not GC-managed.
//
// Links: rt_action.c, rt_action_presets.c, rt_action_io.c, rt_action.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

// Global state (defined in rt_action.c)
extern Action *g_actions;
extern int8_t g_initialized;

/// @brief Linear-scan the global action list by C-string name. NULL on miss.
static inline Action *find_action(const char *name) {
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

/// @brief Allocate a new binding node populated with the given fields.
/// `chord_keys` / `chord_len` are left for BindChord callers to populate.
static inline Binding *create_binding(BindingType type, int64_t code, int64_t pad_index,
                                      double value) {
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

/// @brief Push a binding onto the head of an action's binding list (LIFO).
static inline void add_binding(Action *action, Binding *binding) {
    binding->next = action->bindings;
    action->bindings = binding;
}
