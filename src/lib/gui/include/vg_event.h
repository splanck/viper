// vg_event.h - Event system for widget interaction
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Forward Declarations
    //=============================================================================

    typedef struct vg_widget vg_widget_t;

    //=============================================================================
    // Event Types
    //=============================================================================

    typedef enum vg_event_type
    {
        // Mouse events
        VG_EVENT_MOUSE_MOVE,
        VG_EVENT_MOUSE_DOWN,
        VG_EVENT_MOUSE_UP,
        VG_EVENT_MOUSE_ENTER,
        VG_EVENT_MOUSE_LEAVE,
        VG_EVENT_MOUSE_WHEEL,
        VG_EVENT_CLICK,
        VG_EVENT_DOUBLE_CLICK,

        // Keyboard events
        VG_EVENT_KEY_DOWN,
        VG_EVENT_KEY_UP,
        VG_EVENT_KEY_CHAR, // Character input (after key translation)

        // Focus events
        VG_EVENT_FOCUS_IN,
        VG_EVENT_FOCUS_OUT,

        // Widget-specific events
        VG_EVENT_VALUE_CHANGED, // Slider, checkbox, etc.
        VG_EVENT_TEXT_CHANGED,  // Text input
        VG_EVENT_SELECTION_CHANGED,
        VG_EVENT_SUBMIT, // Enter pressed in text input
        VG_EVENT_CANCEL, // Escape pressed

        // Window events (bubbled to root)
        VG_EVENT_RESIZE,
        VG_EVENT_CLOSE,
    } vg_event_type_t;

    //=============================================================================
    // Mouse Buttons
    //=============================================================================

    typedef enum vg_mouse_button
    {
        VG_MOUSE_LEFT = 0,
        VG_MOUSE_RIGHT = 1,
        VG_MOUSE_MIDDLE = 2,
    } vg_mouse_button_t;

    //=============================================================================
    // Modifier Keys
    //=============================================================================

    typedef enum vg_modifiers
    {
        VG_MOD_NONE = 0,
        VG_MOD_SHIFT = 1 << 0,
        VG_MOD_CTRL = 1 << 1,
        VG_MOD_ALT = 1 << 2,
        VG_MOD_SUPER = 1 << 3, // Cmd on Mac, Win on Windows
    } vg_modifiers_t;

    //=============================================================================
    // Key Codes (Compatible with VGFX key codes)
    //=============================================================================

    typedef enum vg_key
    {
        VG_KEY_UNKNOWN = -1,

        // Printable ASCII
        VG_KEY_SPACE = 32,
        VG_KEY_APOSTROPHE = 39,
        VG_KEY_COMMA = 44,
        VG_KEY_MINUS = 45,
        VG_KEY_PERIOD = 46,
        VG_KEY_SLASH = 47,
        VG_KEY_0 = 48,
        VG_KEY_1 = 49,
        VG_KEY_2 = 50,
        VG_KEY_3 = 51,
        VG_KEY_4 = 52,
        VG_KEY_5 = 53,
        VG_KEY_6 = 54,
        VG_KEY_7 = 55,
        VG_KEY_8 = 56,
        VG_KEY_9 = 57,
        VG_KEY_SEMICOLON = 59,
        VG_KEY_EQUAL = 61,
        VG_KEY_A = 65,
        VG_KEY_B = 66,
        VG_KEY_C = 67,
        VG_KEY_D = 68,
        VG_KEY_E = 69,
        VG_KEY_F = 70,
        VG_KEY_G = 71,
        VG_KEY_H = 72,
        VG_KEY_I = 73,
        VG_KEY_J = 74,
        VG_KEY_K = 75,
        VG_KEY_L = 76,
        VG_KEY_M = 77,
        VG_KEY_N = 78,
        VG_KEY_O = 79,
        VG_KEY_P = 80,
        VG_KEY_Q = 81,
        VG_KEY_R = 82,
        VG_KEY_S = 83,
        VG_KEY_T = 84,
        VG_KEY_U = 85,
        VG_KEY_V = 86,
        VG_KEY_W = 87,
        VG_KEY_X = 88,
        VG_KEY_Y = 89,
        VG_KEY_Z = 90,
        VG_KEY_LEFT_BRACKET = 91,
        VG_KEY_BACKSLASH = 92,
        VG_KEY_RIGHT_BRACKET = 93,
        VG_KEY_GRAVE = 96,

        // Function keys
        VG_KEY_ESCAPE = 256,
        VG_KEY_ENTER = 257,
        VG_KEY_TAB = 258,
        VG_KEY_BACKSPACE = 259,
        VG_KEY_INSERT = 260,
        VG_KEY_DELETE = 261,
        VG_KEY_RIGHT = 262,
        VG_KEY_LEFT = 263,
        VG_KEY_DOWN = 264,
        VG_KEY_UP = 265,
        VG_KEY_PAGE_UP = 266,
        VG_KEY_PAGE_DOWN = 267,
        VG_KEY_HOME = 268,
        VG_KEY_END = 269,
        VG_KEY_CAPS_LOCK = 280,
        VG_KEY_SCROLL_LOCK = 281,
        VG_KEY_NUM_LOCK = 282,
        VG_KEY_PRINT_SCREEN = 283,
        VG_KEY_PAUSE = 284,
        VG_KEY_F1 = 290,
        VG_KEY_F2 = 291,
        VG_KEY_F3 = 292,
        VG_KEY_F4 = 293,
        VG_KEY_F5 = 294,
        VG_KEY_F6 = 295,
        VG_KEY_F7 = 296,
        VG_KEY_F8 = 297,
        VG_KEY_F9 = 298,
        VG_KEY_F10 = 299,
        VG_KEY_F11 = 300,
        VG_KEY_F12 = 301,
        VG_KEY_LEFT_SHIFT = 340,
        VG_KEY_LEFT_CONTROL = 341,
        VG_KEY_LEFT_ALT = 342,
        VG_KEY_LEFT_SUPER = 343,
        VG_KEY_RIGHT_SHIFT = 344,
        VG_KEY_RIGHT_CONTROL = 345,
        VG_KEY_RIGHT_ALT = 346,
        VG_KEY_RIGHT_SUPER = 347,
    } vg_key_t;

    //=============================================================================
    // Event Structure
    //=============================================================================

    typedef struct vg_event
    {
        vg_event_type_t type;
        vg_widget_t *target; // Widget that generated the event
        bool handled;        // Set to true to stop propagation
        uint32_t modifiers;  // Modifier key state
        uint64_t timestamp;  // Event timestamp (milliseconds)

        union
        {
            // Mouse events
            struct
            {
                float x, y;               // Position relative to target
                float screen_x, screen_y; // Screen coordinates
                vg_mouse_button_t button;
                int click_count;
            } mouse;

            // Mouse wheel
            struct
            {
                float delta_x;
                float delta_y;
            } wheel;

            // Keyboard events
            struct
            {
                vg_key_t key;       // Key code
                uint32_t codepoint; // Unicode codepoint (for KEY_CHAR)
                bool repeat;        // Key repeat
            } key;

            // Value changed
            struct
            {
                int int_value;
                float float_value;
                bool bool_value;
            } value;

            // Resize
            struct
            {
                int width;
                int height;
            } resize;
        };
    } vg_event_t;

    //=============================================================================
    // Event Dispatch
    //=============================================================================

    /// Dispatch event to widget tree (with bubbling)
    bool vg_event_dispatch(vg_widget_t *root, vg_event_t *event);

    /// Send event directly to widget (no bubbling)
    bool vg_event_send(vg_widget_t *widget, vg_event_t *event);

    /// Create a mouse event
    vg_event_t vg_event_mouse(
        vg_event_type_t type, float x, float y, vg_mouse_button_t button, uint32_t modifiers);

    /// Create a key event
    vg_event_t vg_event_key(vg_event_type_t type,
                            vg_key_t key,
                            uint32_t codepoint,
                            uint32_t modifiers);

    /// Translate platform event to GUI event (from vgfx_event_t)
    vg_event_t vg_event_from_platform(void *platform_event);

#ifdef __cplusplus
}
#endif
