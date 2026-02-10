//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Action Mapping System Implementation
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_json_stream.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <stdlib.h>
#include <string.h>

//=========================================================================
// Internal Data Structures
//=========================================================================

// Binding source types
typedef enum
{
    BIND_NONE = 0,
    BIND_KEY,            // Keyboard key
    BIND_MOUSE_BUTTON,   // Mouse button
    BIND_MOUSE_X,        // Mouse X delta
    BIND_MOUSE_Y,        // Mouse Y delta
    BIND_SCROLL_X,       // Mouse scroll X
    BIND_SCROLL_Y,       // Mouse scroll Y
    BIND_PAD_BUTTON,     // Gamepad button
    BIND_PAD_AXIS,       // Gamepad axis
    BIND_PAD_BUTTON_AXIS // Gamepad button as axis
} BindingType;

// A single input binding
typedef struct Binding
{
    BindingType type;
    int64_t code;      // Key/button/axis code
    int64_t pad_index; // Controller index (-1 for any)
    double value;      // Axis value for key/button bindings, scale for analog
    struct Binding *next;
} Binding;

// An action (button or axis)
typedef struct Action
{
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

static Action *find_action(const char *name)
{
    if (!name)
        return NULL;
    Action *a = g_actions;
    while (a)
    {
        if (strcmp(a->name, name) == 0)
            return a;
        a = a->next;
    }
    return NULL;
}

static Action *find_action_str(rt_string name)
{
    if (!name)
        return NULL;
    int64_t name_len = rt_str_len(name);
    const char *name_data = name->data;

    Action *a = g_actions;
    while (a)
    {
        size_t a_len = strlen(a->name);
        if ((int64_t)a_len == name_len && memcmp(a->name, name_data, a_len) == 0)
            return a;
        a = a->next;
    }
    return NULL;
}

static char *strdup_rt_string(rt_string s)
{
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

static void free_bindings(Binding *b)
{
    while (b)
    {
        Binding *next = b->next;
        free(b);
        b = next;
    }
}

static void free_action(Action *a)
{
    if (a)
    {
        free(a->name);
        free_bindings(a->bindings);
        free(a);
    }
}

static Binding *create_binding(BindingType type, int64_t code, int64_t pad_index, double value)
{
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

static void add_binding(Action *action, Binding *binding)
{
    binding->next = action->bindings;
    action->bindings = binding;
}

// Remove a binding matching type/code/pad_index
static int8_t remove_binding(Action *action, BindingType type, int64_t code, int64_t pad_index)
{
    Binding **pp = &action->bindings;
    while (*pp)
    {
        Binding *b = *pp;
        if (b->type == type && b->code == code && b->pad_index == pad_index)
        {
            *pp = b->next;
            free(b);
            return 1;
        }
        pp = &b->next;
    }
    return 0;
}

// Check if a key is down this frame
static int8_t key_held(int64_t key)
{
    return rt_keyboard_is_down(key);
}

static int8_t key_pressed(int64_t key)
{
    return rt_keyboard_was_pressed(key);
}

static int8_t key_released(int64_t key)
{
    return rt_keyboard_was_released(key);
}

// Check if a mouse button is down
static int8_t mouse_held(int64_t button)
{
    return rt_mouse_is_down(button);
}

static int8_t mouse_pressed(int64_t button)
{
    return rt_mouse_was_pressed(button);
}

static int8_t mouse_released(int64_t button)
{
    return rt_mouse_was_released(button);
}

// Check if a pad button is down
static int8_t pad_held(int64_t pad_index, int64_t button)
{
    if (pad_index < 0)
    {
        // Any controller
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_is_down(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_is_down(pad_index, button);
}

static int8_t pad_pressed(int64_t pad_index, int64_t button)
{
    if (pad_index < 0)
    {
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_was_pressed(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_was_pressed(pad_index, button);
}

static int8_t pad_released(int64_t pad_index, int64_t button)
{
    if (pad_index < 0)
    {
        for (int64_t i = 0; i < 4; i++)
        {
            if (rt_pad_is_connected(i) && rt_pad_was_released(i, button))
                return 1;
        }
        return 0;
    }
    return rt_pad_was_released(pad_index, button);
}

// Get gamepad axis value
static double pad_axis_value(int64_t pad_index, int64_t axis)
{
    if (pad_index < 0)
    {
        // Return value from first connected controller with non-zero input
        for (int64_t i = 0; i < 4; i++)
        {
            if (!rt_pad_is_connected(i))
                continue;
            double v = 0.0;
            switch (axis)
            {
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

    switch (axis)
    {
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

// Clamp value to -1.0 to 1.0
static double clamp_axis(double value)
{
    if (value < -1.0)
        return -1.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

//=========================================================================
// Action System Lifecycle
//=========================================================================

void rt_action_init(void)
{
    if (g_initialized)
        return;
    g_actions = NULL;
    g_initialized = 1;
}

void rt_action_shutdown(void)
{
    rt_action_clear();
    g_initialized = 0;
}

void rt_action_update(void)
{
    if (!g_initialized)
        return;

    Action *a = g_actions;
    while (a)
    {
        a->pressed = 0;
        a->released = 0;
        a->held = 0;
        a->axis_value = 0.0;

        Binding *b = a->bindings;
        while (b)
        {
            switch (b->type)
            {
                case BIND_KEY:
                    if (a->is_axis)
                    {
                        if (key_held(b->code))
                            a->axis_value += b->value;
                    }
                    else
                    {
                        if (key_pressed(b->code))
                            a->pressed = 1;
                        if (key_released(b->code))
                            a->released = 1;
                        if (key_held(b->code))
                            a->held = 1;
                    }
                    break;

                case BIND_MOUSE_BUTTON:
                    if (!a->is_axis)
                    {
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
                        a->axis_value += (double)rt_mouse_wheel_x() * b->value;
                    break;

                case BIND_SCROLL_Y:
                    if (a->is_axis)
                        a->axis_value += (double)rt_mouse_wheel_y() * b->value;
                    break;

                case BIND_PAD_BUTTON:
                    if (!a->is_axis)
                    {
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
                    if (a->is_axis)
                    {
                        if (pad_held(b->pad_index, b->code))
                            a->axis_value += b->value;
                    }
                    break;

                default:
                    break;
            }
            b = b->next;
        }

        a = a->next;
    }
}

void rt_action_clear(void)
{
    Action *a = g_actions;
    while (a)
    {
        Action *next = a->next;
        free_action(a);
        a = next;
    }
    g_actions = NULL;
}

//=========================================================================
// Action Definition
//=========================================================================

int8_t rt_action_define(rt_string name)
{
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
    if (!a->name)
    {
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

int8_t rt_action_define_axis(rt_string name)
{
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
    if (!a->name)
    {
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

int8_t rt_action_exists(rt_string name)
{
    return find_action_str(name) != NULL;
}

int8_t rt_action_is_axis(rt_string name)
{
    Action *a = find_action_str(name);
    return a ? a->is_axis : 0;
}

int8_t rt_action_remove(rt_string name)
{
    if (!name)
        return 0;

    int64_t name_len = rt_str_len(name);
    const char *name_data = name->data;

    Action **pp = &g_actions;
    while (*pp)
    {
        Action *a = *pp;
        size_t a_len = strlen(a->name);
        if ((int64_t)a_len == name_len && memcmp(a->name, name_data, a_len) == 0)
        {
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

int8_t rt_action_bind_key(rt_string action, int64_t key)
{
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_KEY, key, 0, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_bind_key_axis(rt_string action, int64_t key, double value)
{
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_KEY, key, 0, value);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_unbind_key(rt_string action, int64_t key)
{
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_KEY, key, 0);
}

//=========================================================================
// Mouse Bindings
//=========================================================================

int8_t rt_action_bind_mouse(rt_string action, int64_t button)
{
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_BUTTON, button, 0, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_unbind_mouse(rt_string action, int64_t button)
{
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_MOUSE_BUTTON, button, 0);
}

int8_t rt_action_bind_mouse_x(rt_string action, double sensitivity)
{
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_X, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_bind_mouse_y(rt_string action, double sensitivity)
{
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_MOUSE_Y, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_bind_scroll_x(rt_string action, double sensitivity)
{
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_SCROLL_X, 0, 0, sensitivity);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_bind_scroll_y(rt_string action, double sensitivity)
{
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

int8_t rt_action_bind_pad_button(rt_string action, int64_t pad_index, int64_t button)
{
    Action *a = find_action_str(action);
    if (!a || a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_PAD_BUTTON, button, pad_index, 1.0);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_unbind_pad_button(rt_string action, int64_t pad_index, int64_t button)
{
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_PAD_BUTTON, button, pad_index);
}

int8_t rt_action_bind_pad_axis(rt_string action, int64_t pad_index, int64_t axis, double scale)
{
    Action *a = find_action_str(action);
    if (!a || !a->is_axis)
        return 0;
    Binding *b = create_binding(BIND_PAD_AXIS, axis, pad_index, scale);
    if (!b)
        return 0;
    add_binding(a, b);
    return 1;
}

int8_t rt_action_unbind_pad_axis(rt_string action, int64_t pad_index, int64_t axis)
{
    Action *a = find_action_str(action);
    if (!a)
        return 0;
    return remove_binding(a, BIND_PAD_AXIS, axis, pad_index);
}

int8_t rt_action_bind_pad_button_axis(rt_string action,
                                      int64_t pad_index,
                                      int64_t button,
                                      double value)
{
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

int8_t rt_action_pressed(rt_string action)
{
    Action *a = find_action_str(action);
    return a ? a->pressed : 0;
}

int8_t rt_action_released(rt_string action)
{
    Action *a = find_action_str(action);
    return a ? a->released : 0;
}

int8_t rt_action_held(rt_string action)
{
    Action *a = find_action_str(action);
    return a ? a->held : 0;
}

double rt_action_strength(rt_string action)
{
    Action *a = find_action_str(action);
    return a && a->held ? 1.0 : 0.0;
}

//=========================================================================
// Axis Action Queries
//=========================================================================

double rt_action_axis(rt_string action)
{
    Action *a = find_action_str(action);
    return a ? clamp_axis(a->axis_value) : 0.0;
}

double rt_action_axis_raw(rt_string action)
{
    Action *a = find_action_str(action);
    return a ? a->axis_value : 0.0;
}

//=========================================================================
// Binding Introspection
//=========================================================================

void *rt_action_list(void)
{
    void *seq = rt_seq_new();
    Action *a = g_actions;
    while (a)
    {
        rt_string name = rt_string_from_bytes(a->name, strlen(a->name));
        rt_seq_push(seq, (void *)name);
        a = a->next;
    }
    return seq;
}

rt_string rt_action_bindings_str(rt_string action)
{
    Action *a = find_action_str(action);
    if (!a)
        return rt_str_empty();

    // Build a description of all bindings
    char buffer[1024];
    buffer[0] = '\0';
    int pos = 0;
    int first = 1;

    Binding *b = a->bindings;
    while (b && pos < 1000)
    {
        if (!first && pos < 998)
        {
            buffer[pos++] = ',';
            buffer[pos++] = ' ';
        }
        first = 0;

        const char *desc = "";
        char temp[64];

        switch (b->type)
        {
            case BIND_KEY:
            {
                rt_string key_name = rt_keyboard_key_name(b->code);
                int64_t key_len = rt_str_len(key_name);
                if (key_len > 0 && key_len < 60)
                {
                    memcpy(temp, key_name->data, (size_t)key_len);
                    temp[key_len] = '\0';
                    desc = temp;
                }
                else
                {
                    desc = "Key";
                }
                break;
            }
            case BIND_MOUSE_BUTTON:
                switch (b->code)
                {
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
                switch (b->code)
                {
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
                switch (b->code)
                {
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
            default:
                desc = "Unknown";
                break;
        }

        size_t len = strlen(desc);
        if (pos + (int)len < 1000)
        {
            memcpy(buffer + pos, desc, len);
            pos += (int)len;
        }

        b = b->next;
    }
    buffer[pos] = '\0';

    return rt_string_from_bytes(buffer, (size_t)pos);
}

int64_t rt_action_binding_count(rt_string action)
{
    Action *a = find_action_str(action);
    if (!a)
        return 0;

    int64_t count = 0;
    Binding *b = a->bindings;
    while (b)
    {
        count++;
        b = b->next;
    }
    return count;
}

//=========================================================================
// Conflict Detection
//=========================================================================

rt_string rt_action_key_bound_to(int64_t key)
{
    Action *a = g_actions;
    while (a)
    {
        Binding *b = a->bindings;
        while (b)
        {
            if (b->type == BIND_KEY && b->code == key)
                return rt_string_from_bytes(a->name, strlen(a->name));
            b = b->next;
        }
        a = a->next;
    }
    return rt_str_empty();
}

rt_string rt_action_mouse_bound_to(int64_t button)
{
    Action *a = g_actions;
    while (a)
    {
        Binding *b = a->bindings;
        while (b)
        {
            if (b->type == BIND_MOUSE_BUTTON && b->code == button)
                return rt_string_from_bytes(a->name, strlen(a->name));
            b = b->next;
        }
        a = a->next;
    }
    return rt_str_empty();
}

rt_string rt_action_pad_button_bound_to(int64_t pad_index, int64_t button)
{
    Action *a = g_actions;
    while (a)
    {
        Binding *b = a->bindings;
        while (b)
        {
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
// Axis Constant Getters
//=========================================================================

int64_t rt_action_axis_left_x(void)
{
    return VIPER_AXIS_LEFT_X;
}

int64_t rt_action_axis_left_y(void)
{
    return VIPER_AXIS_LEFT_Y;
}

int64_t rt_action_axis_right_x(void)
{
    return VIPER_AXIS_RIGHT_X;
}

int64_t rt_action_axis_right_y(void)
{
    return VIPER_AXIS_RIGHT_Y;
}

int64_t rt_action_axis_left_trigger(void)
{
    return VIPER_AXIS_LEFT_TRIGGER;
}

int64_t rt_action_axis_right_trigger(void)
{
    return VIPER_AXIS_RIGHT_TRIGGER;
}

//=========================================================================
// Persistence (Save/Load)
//=========================================================================

static const char *binding_type_name(BindingType type)
{
    switch (type)
    {
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
        default:
            return "unknown";
    }
}

static BindingType binding_type_from_name(const char *name)
{
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
    return BIND_NONE;
}

static void sb_append_json_string(rt_string_builder *sb, const char *str)
{
    rt_sb_append_cstr(sb, "\"");
    while (*str)
    {
        char c = *str++;
        switch (c)
        {
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

rt_string rt_action_save(void)
{
    rt_string_builder sb;
    int8_t first_action;
    rt_sb_init(&sb);

    rt_sb_append_cstr(&sb, "{\"actions\":[");

    first_action = 1;
    {
        Action *a = g_actions;
        while (a)
        {
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
                while (b)
                {
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

int8_t rt_action_load(rt_string json)
{
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
    while (tok == RT_JSON_TOK_OBJECT_START)
    {
        char action_name[256];
        int8_t is_axis = 0;
        action_name[0] = '\0';

        /* Parse action fields */
        tok = rt_json_stream_next(parser);
        while (tok == RT_JSON_TOK_KEY)
        {
            rt_string key = rt_json_stream_string_value(parser);
            const char *key_cstr = rt_string_cstr(key);

            if (strcmp(key_cstr, "name") == 0)
            {
                tok = rt_json_stream_next(parser);
                if (tok == RT_JSON_TOK_STRING)
                {
                    rt_string val = rt_json_stream_string_value(parser);
                    const char *val_cstr = rt_string_cstr(val);
                    size_t len = strlen(val_cstr);
                    if (len >= sizeof(action_name))
                        len = sizeof(action_name) - 1;
                    memcpy(action_name, val_cstr, len);
                    action_name[len] = '\0';
                }
            }
            else if (strcmp(key_cstr, "type") == 0)
            {
                tok = rt_json_stream_next(parser);
                if (tok == RT_JSON_TOK_STRING)
                {
                    rt_string val = rt_json_stream_string_value(parser);
                    is_axis = (strcmp(rt_string_cstr(val), "axis") == 0) ? 1 : 0;
                }
            }
            else if (strcmp(key_cstr, "bindings") == 0)
            {
                /* Define the action first */
                if (action_name[0] != '\0')
                {
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
                while (tok == RT_JSON_TOK_OBJECT_START)
                {
                    BindingType btype = BIND_NONE;
                    int64_t code = 0;
                    int64_t pad = 0;
                    double value = 0.0;

                    /* Parse binding fields */
                    tok = rt_json_stream_next(parser);
                    while (tok == RT_JSON_TOK_KEY)
                    {
                        rt_string bkey = rt_json_stream_string_value(parser);
                        const char *bkey_cstr = rt_string_cstr(bkey);

                        tok = rt_json_stream_next(parser);
                        if (strcmp(bkey_cstr, "type") == 0 && tok == RT_JSON_TOK_STRING)
                        {
                            rt_string bval = rt_json_stream_string_value(parser);
                            btype = binding_type_from_name(rt_string_cstr(bval));
                        }
                        else if (strcmp(bkey_cstr, "code") == 0 && tok == RT_JSON_TOK_NUMBER)
                        {
                            code = (int64_t)rt_json_stream_number_value(parser);
                        }
                        else if (strcmp(bkey_cstr, "pad") == 0 && tok == RT_JSON_TOK_NUMBER)
                        {
                            pad = (int64_t)rt_json_stream_number_value(parser);
                        }
                        else if (strcmp(bkey_cstr, "value") == 0 && tok == RT_JSON_TOK_NUMBER)
                        {
                            value = rt_json_stream_number_value(parser);
                        }
                        tok = rt_json_stream_next(parser);
                    }
                    /* tok should be OBJECT_END for the binding */

                    /* Add binding to action */
                    if (btype != BIND_NONE && action_name[0] != '\0')
                    {
                        Action *a = find_action(action_name);
                        if (a)
                        {
                            Binding *b = create_binding(btype, code, pad, value);
                            if (b)
                                add_binding(a, b);
                        }
                    }

                    tok = rt_json_stream_next(parser);
                }
                /* tok should be ARRAY_END for bindings */
            }
            else
            {
                /* Skip unknown field */
                rt_json_stream_next(parser);
            }

            tok = rt_json_stream_next(parser);
        }
        /* tok should be OBJECT_END for the action */

        tok = rt_json_stream_next(parser);
    }
    /* tok should be ARRAY_END for actions */

    return 1;
}
