//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Purpose: GUI event dispatch system — routes keyboard, mouse, focus, and custom events to widgets.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/core/vg_event.c
//
//===----------------------------------------------------------------------===//
// vg_event.c - Event system implementation
#include "../../include/vg_event.h"
#include "../../include/vg_widget.h"
#include "vgfx.h"
#include <stdint.h>
#include <string.h>

#define VG_DOUBLE_CLICK_MAX_INTERVAL_MS 400
#define VG_DOUBLE_CLICK_MAX_DISTANCE_PX 5.0f

static bool event_has_widget_local_mouse_coords(vg_event_type_t type) {
    // NOTE: VG_EVENT_MOUSE_WHEEL is intentionally excluded. In vg_event_t, the
    // `mouse` and `wheel` payload structs share a union, so mouse.x/y alias
    // wheel.delta_x/y. If we localized wheel events, the widget-local writes
    // would destroy the scroll deltas before the widget's wheel handler could
    // read them — causing wheel scrolling to silently do nothing. Wheel events
    // carry screen_x/y for hit-test routing but do not need widget-local
    // coordinates, so we leave mouse.x/y untouched (== wheel deltas).
    return type == VG_EVENT_MOUSE_MOVE || type == VG_EVENT_MOUSE_DOWN ||
           type == VG_EVENT_MOUSE_UP || type == VG_EVENT_CLICK ||
           type == VG_EVENT_DOUBLE_CLICK;
}

static void event_localize_mouse_to_widget(vg_widget_t *widget, vg_event_t *event) {
    if (!widget || !event || !event_has_widget_local_mouse_coords(event->type))
        return;

    float sx = 0.0f;
    float sy = 0.0f;
    vg_widget_get_screen_bounds(widget, &sx, &sy, NULL, NULL);
    event->mouse.x = event->mouse.screen_x - sx;
    event->mouse.y = event->mouse.screen_y - sy;
}

static vg_widget_t *get_hovered_widget(void) {
    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    return state.hovered_widget;
}

static void set_hovered_widget(vg_widget_t *widget) {
    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    state.hovered_widget = widget;
    vg_widget_set_runtime_state(&state);
}

static void update_hovered_widget(vg_widget_t *widget) {
    vg_widget_t *previous = get_hovered_widget();
    if (previous == widget)
        return;

    if (previous) {
        vg_event_t leave = {0};
        leave.type = VG_EVENT_MOUSE_LEAVE;
        leave.target = previous;
        vg_event_send(previous, &leave);
    }

    set_hovered_widget(widget);

    if (widget) {
        vg_event_t enter = {0};
        enter.type = VG_EVENT_MOUSE_ENTER;
        enter.target = widget;
        vg_event_send(widget, &enter);
    }
}

static void clear_last_click_state(void) {
    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    state.last_click_widget = NULL;
    state.last_click_time_ms = 0;
    state.last_click_button = -1;
    state.last_click_screen_x = 0.0f;
    state.last_click_screen_y = 0.0f;
    vg_widget_set_runtime_state(&state);
}

static void remember_click(vg_widget_t *widget, const vg_event_t *event) {
    if (!widget || !event)
        return;

    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    state.last_click_widget = widget;
    state.last_click_time_ms = event->timestamp;
    state.last_click_button = (int32_t)event->mouse.button;
    state.last_click_screen_x = event->mouse.screen_x;
    state.last_click_screen_y = event->mouse.screen_y;
    vg_widget_set_runtime_state(&state);
}

static bool is_double_click_for_widget(vg_widget_t *widget, const vg_event_t *event) {
    if (!widget || !event || event->timestamp <= 0)
        return false;

    vg_widget_runtime_state_t state = {0};
    vg_widget_get_runtime_state(&state);
    if (state.last_click_widget != widget || state.last_click_button != (int32_t)event->mouse.button ||
        state.last_click_time_ms <= 0 || event->timestamp < state.last_click_time_ms) {
        return false;
    }

    if (event->timestamp - state.last_click_time_ms > VG_DOUBLE_CLICK_MAX_INTERVAL_MS)
        return false;

    float dx = event->mouse.screen_x - state.last_click_screen_x;
    float dy = event->mouse.screen_y - state.last_click_screen_y;
    float max_dist_sq = VG_DOUBLE_CLICK_MAX_DISTANCE_PX * VG_DOUBLE_CLICK_MAX_DISTANCE_PX;
    return dx * dx + dy * dy <= max_dist_sq;
}

//=============================================================================
// Event Creation Helpers
//=============================================================================

vg_event_t vg_event_mouse(
    vg_event_type_t type, float x, float y, vg_mouse_button_t button, uint32_t modifiers) {
    vg_event_t event;
    memset(&event, 0, sizeof(event));

    event.type = type;
    event.modifiers = modifiers;
    event.mouse.x = x;
    event.mouse.y = y;
    event.mouse.screen_x = x;
    event.mouse.screen_y = y;
    event.mouse.button = button;
    event.mouse.click_count = 1;

    return event;
}

vg_event_t vg_event_key(vg_event_type_t type,
                        vg_key_t key,
                        uint32_t codepoint,
                        uint32_t modifiers) {
    vg_event_t event;
    memset(&event, 0, sizeof(event));

    event.type = type;
    event.modifiers = modifiers;
    event.key.key = key;
    event.key.codepoint = codepoint;
    event.key.repeat = false;

    return event;
}

//=============================================================================
// Platform Event Translation
//=============================================================================

// Translate vgfx key codes to vg key codes (they use different numbering for special keys)
static vg_key_t translate_vgfx_key(int vgfx_key) {
    // Printable ASCII keys are the same
    if (vgfx_key >= ' ' && vgfx_key <= '~') {
        return (vg_key_t)vgfx_key;
    }

    // Special keys need translation (vgfx codes -> vg codes)
    // VGFX: ESCAPE=256, ENTER=257, LEFT=258, RIGHT=259, UP=260, DOWN=261, BACKSPACE=262,
    // DELETE=263, TAB=264, HOME=265, END=266, PAGE_UP=267, PAGE_DOWN=268.
    // VG:   ESCAPE=256, ENTER=257, TAB=258, BACKSPACE=259, DELETE=261, RIGHT=262,
    // LEFT=263, DOWN=264, UP=265, PAGE_UP=266, PAGE_DOWN=267, HOME=268, END=269.
    switch (vgfx_key) {
        case 256:
            return VG_KEY_ESCAPE; // VGFX_KEY_ESCAPE
        case 257:
            return VG_KEY_ENTER; // VGFX_KEY_ENTER
        case 258:
            return VG_KEY_LEFT; // VGFX_KEY_LEFT
        case 259:
            return VG_KEY_RIGHT; // VGFX_KEY_RIGHT
        case 260:
            return VG_KEY_UP; // VGFX_KEY_UP
        case 261:
            return VG_KEY_DOWN; // VGFX_KEY_DOWN
        case 262:
            return VG_KEY_BACKSPACE; // VGFX_KEY_BACKSPACE
        case 263:
            return VG_KEY_DELETE; // VGFX_KEY_DELETE
        case 264:
            return VG_KEY_TAB; // VGFX_KEY_TAB
        case 265:
            return VG_KEY_HOME; // VGFX_KEY_HOME
        case 266:
            return VG_KEY_END; // VGFX_KEY_END
        case 267:
            return VG_KEY_PAGE_UP; // VGFX_KEY_PAGE_UP
        case 268:
            return VG_KEY_PAGE_DOWN; // VGFX_KEY_PAGE_DOWN
        default:
            return (vg_key_t)vgfx_key;
    }
}

static uint32_t translate_vgfx_modifiers(int vgfx_modifiers) {
    uint32_t modifiers = VG_MOD_NONE;
    if (vgfx_modifiers & VGFX_MOD_SHIFT)
        modifiers |= VG_MOD_SHIFT;
    if (vgfx_modifiers & VGFX_MOD_CTRL)
        modifiers |= VG_MOD_CTRL;
    if (vgfx_modifiers & VGFX_MOD_ALT)
        modifiers |= VG_MOD_ALT;
    if (vgfx_modifiers & VGFX_MOD_CMD)
        modifiers |= VG_MOD_SUPER;
    return modifiers;
}

vg_event_t vg_event_from_platform(void *platform_event) {
    vg_event_t event;
    memset(&event, 0, sizeof(event));

    if (!platform_event)
        return event;

    vgfx_event_t *pe = (vgfx_event_t *)platform_event;
    event.timestamp = pe->time_ms;

    switch (pe->type) {
        case VGFX_EVENT_KEY_DOWN:
            event.type = VG_EVENT_KEY_DOWN;
            event.key.key = translate_vgfx_key(pe->data.key.key);
            event.key.repeat = pe->data.key.is_repeat != 0;
            event.modifiers = translate_vgfx_modifiers(pe->data.key.modifiers);
            break;

        case VGFX_EVENT_KEY_UP:
            event.type = VG_EVENT_KEY_UP;
            event.key.key = translate_vgfx_key(pe->data.key.key);
            event.modifiers = translate_vgfx_modifiers(pe->data.key.modifiers);
            break;

        case VGFX_EVENT_TEXT_INPUT:
            event.type = VG_EVENT_KEY_CHAR;
            event.key.key = VG_KEY_UNKNOWN;
            event.key.codepoint = pe->data.text.codepoint;
            event.modifiers = translate_vgfx_modifiers(pe->data.text.modifiers);
            break;

        case VGFX_EVENT_MOUSE_MOVE:
            event.type = VG_EVENT_MOUSE_MOVE;
            event.mouse.x = (float)pe->data.mouse_move.x;
            event.mouse.y = (float)pe->data.mouse_move.y;
            event.mouse.screen_x = (float)pe->data.mouse_move.x;
            event.mouse.screen_y = (float)pe->data.mouse_move.y;
            break;

        case VGFX_EVENT_MOUSE_DOWN:
            event.type = VG_EVENT_MOUSE_DOWN;
            event.mouse.x = (float)pe->data.mouse_button.x;
            event.mouse.y = (float)pe->data.mouse_button.y;
            event.mouse.screen_x = (float)pe->data.mouse_button.x;
            event.mouse.screen_y = (float)pe->data.mouse_button.y;
            event.mouse.button = (vg_mouse_button_t)pe->data.mouse_button.button;
            break;

        case VGFX_EVENT_MOUSE_UP:
            event.type = VG_EVENT_MOUSE_UP;
            event.mouse.x = (float)pe->data.mouse_button.x;
            event.mouse.y = (float)pe->data.mouse_button.y;
            event.mouse.screen_x = (float)pe->data.mouse_button.x;
            event.mouse.screen_y = (float)pe->data.mouse_button.y;
            event.mouse.button = (vg_mouse_button_t)pe->data.mouse_button.button;
            break;

        case VGFX_EVENT_SCROLL:
            event.type = VG_EVENT_MOUSE_WHEEL;
            event.wheel.delta_x = pe->data.scroll.delta_x;
            event.wheel.delta_y = pe->data.scroll.delta_y;
            /* Also populate mouse.screen_x/y for hit-test routing in vg_event_dispatch */
            event.mouse.screen_x = (float)pe->data.scroll.x;
            event.mouse.screen_y = (float)pe->data.scroll.y;
            break;

        case VGFX_EVENT_RESIZE:
            event.type = VG_EVENT_RESIZE;
            event.resize.width = pe->data.resize.width;
            event.resize.height = pe->data.resize.height;
            break;

        case VGFX_EVENT_CLOSE:
            event.type = VG_EVENT_CLOSE;
            break;

        default:
            // Unknown event type
            break;
    }

    return event;
}

//=============================================================================
// Event Dispatch
//=============================================================================

bool vg_event_dispatch(vg_widget_t *root, vg_event_t *event) {
    if (!root || !event)
        return false;

    // Find target widget for mouse events
    if (event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
        event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK ||
        event->type == VG_EVENT_DOUBLE_CLICK || event->type == VG_EVENT_MOUSE_WHEEL) {
        // Check if a widget has captured input (e.g., open dropdown menu).
        // When capture is active, all mouse events route to the captured widget
        // regardless of hit testing. This allows dropdown menus to receive clicks
        // even though the dropdown renders outside the menubar's widget bounds.
        vg_widget_t *capture = vg_widget_get_input_capture();
        if (capture) {
            update_hovered_widget(capture);
            event->target = capture;
            event_localize_mouse_to_widget(capture, event);

            // For MOUSE_UP, we need to synthesize a CLICK event because
            // vg_event_send()'s CLICK generation depends on contains_point(),
            // which fails for dropdown clicks outside the widget bounds.
            if (event->type == VG_EVENT_MOUSE_UP) {
                bool handled = vg_event_send(capture, event);
                if (!vg_widget_contains_point(
                        capture, event->mouse.screen_x, event->mouse.screen_y)) {
                    vg_event_t click_event = *event;
                    click_event.type = VG_EVENT_CLICK;
                    event_localize_mouse_to_widget(capture, &click_event);
                    handled |= vg_event_send(capture, &click_event);
                }
                if (vg_widget_get_input_capture() != capture) {
                    vg_widget_t *modal = vg_widget_get_modal_root();
                    vg_widget_t *hit_root = (modal && modal->visible) ? modal : root;
                    vg_widget_t *target =
                        vg_widget_hit_test(hit_root, event->mouse.screen_x, event->mouse.screen_y);
                    update_hovered_widget(target);
                }
                return handled;
            }

            bool handled = vg_event_send(capture, event);
            if (vg_widget_get_input_capture() != capture) {
                vg_widget_t *modal = vg_widget_get_modal_root();
                vg_widget_t *hit_root = (modal && modal->visible) ? modal : root;
                vg_widget_t *target =
                    vg_widget_hit_test(hit_root, event->mouse.screen_x, event->mouse.screen_y);
                update_hovered_widget(target);
            }
            return handled;
        }

        // When a modal root is active, restrict hit-testing to its subtree so
        // that clicks on widgets behind the dialog are swallowed.
        vg_widget_t *modal = vg_widget_get_modal_root();
        vg_widget_t *hit_root = (modal && modal->visible) ? modal : root;

        vg_widget_t *target =
            vg_widget_hit_test(hit_root, event->mouse.screen_x, event->mouse.screen_y);
        update_hovered_widget(target);
        if (!target && modal && modal->visible) {
            // Click landed outside the modal dialog: swallow the event silently.
            return true;
        }
        if (target) {
            event->target = target;
            // Don't localize here — vg_event_send is the single owner of the
            // screen→widget-relative transform. Calling event_localize_mouse_to_widget
            // both here and inside vg_event_send caused mouse coordinates to be
            // offset by (widget_screen_x, widget_screen_y) twice, breaking
            // bounds-check-based mouse handlers like vg_codeeditor's wheel scroll.
            return vg_event_send(target, event);
        }
        return false;
    }

    // Keyboard events: route to captured widget first (for menu keyboard navigation),
    // then to focused widget, then to root.
    if (event->type == VG_EVENT_KEY_DOWN || event->type == VG_EVENT_KEY_UP ||
        event->type == VG_EVENT_KEY_CHAR) {
        vg_widget_t *capture = vg_widget_get_input_capture();
        if (capture) {
            event->target = capture;
            if (capture->vtable && capture->vtable->handle_event) {
                if (capture->vtable->handle_event(capture, event))
                    return true;
            }
            // If captured widget didn't handle it, fall through to focused widget
        }

        vg_widget_t *focused = vg_widget_get_focused(root);

        // When a modal is active, redirect keyboard events to the modal if the
        // focused widget is outside the modal's subtree.
        vg_widget_t *modal_kb = vg_widget_get_modal_root();
        if (modal_kb && modal_kb->visible) {
            if (!focused) {
                focused = modal_kb;
            } else {
                bool inside = false;
                for (vg_widget_t *w = focused; w; w = w->parent) {
                    if (w == modal_kb) {
                        inside = true;
                        break;
                    }
                }
                if (!inside)
                    focused = modal_kb;
            }
        }

        vg_widget_t *focus_root = (modal_kb && modal_kb->visible) ? modal_kb : root;

        if (event->type == VG_EVENT_KEY_DOWN && event->key.key == VG_KEY_TAB &&
            (event->modifiers & (VG_MOD_CTRL | VG_MOD_ALT | VG_MOD_SUPER)) == 0) {
            if (focused) {
                event->target = focused;
                if (vg_event_send(focused, event))
                    return true;
            }

            if (event->modifiers & VG_MOD_SHIFT)
                vg_widget_focus_prev(focus_root);
            else
                vg_widget_focus_next(focus_root);
            return true;
        }

        if (focused) {
            event->target = focused;
            return vg_event_send(focused, event);
        }

        // If no focused widget, try to dispatch to root
        event->target = root;
        return vg_event_send(root, event);
    }

    // Other events go directly to root
    event->target = root;
    return vg_event_send(root, event);
}

bool vg_event_send(vg_widget_t *widget, vg_event_t *event) {
    if (!widget || !event)
        return false;

    const bool restore_mouse =
        event_has_widget_local_mouse_coords(event->type) && event->target != NULL;
    const float target_mouse_x = event->mouse.x;
    const float target_mouse_y = event->mouse.y;
    bool handled = event->handled;

    event_localize_mouse_to_widget(widget, event);

    // Handle common state changes for mouse events
    if (event->type == VG_EVENT_MOUSE_ENTER) {
        widget->state |= VG_STATE_HOVERED;
        widget->needs_paint = true;
    } else if (event->type == VG_EVENT_MOUSE_LEAVE) {
        widget->state &= ~(VG_STATE_HOVERED | VG_STATE_PRESSED);
        widget->needs_paint = true;
    } else if (event->type == VG_EVENT_MOUSE_DOWN) {
        widget->state |= VG_STATE_PRESSED;
        widget->needs_paint = true;
        // Set focus on click if widget can accept focus
        if (widget->vtable && widget->vtable->can_focus && widget->vtable->can_focus(widget)) {
            vg_widget_set_focus(widget);
        }
    } else if (event->type == VG_EVENT_MOUSE_UP) {
        bool was_pressed = (widget->state & VG_STATE_PRESSED) != 0;
        widget->state &= ~VG_STATE_PRESSED;
        widget->needs_paint = true;

        // Generate click event if mouse was pressed on this widget
        if (was_pressed &&
            vg_widget_contains_point(widget, event->mouse.screen_x, event->mouse.screen_y)) {
            vg_event_t click_event = *event;
            click_event.type = VG_EVENT_CLICK;
            click_event.mouse.click_count = 1;
            handled |= vg_event_send(widget, &click_event);

            if (is_double_click_for_widget(widget, event)) {
                vg_event_t double_click_event = *event;
                double_click_event.type = VG_EVENT_DOUBLE_CLICK;
                double_click_event.mouse.click_count = 2;
                handled |= vg_event_send(widget, &double_click_event);
                clear_last_click_state();
            } else {
                remember_click(widget, event);
            }
        } else if (was_pressed) {
            clear_last_click_state();
        }
    }

    // Call widget's event handler
    if (widget->vtable && widget->vtable->handle_event) {
        if (widget->vtable->handle_event(widget, event)) {
            event->handled = true;
            if (restore_mouse) {
                event->mouse.x = target_mouse_x;
                event->mouse.y = target_mouse_y;
            }
            return true;
        }
    }

    // Bubble up the parent chain iteratively (avoids stack overflow on deep trees)
    vg_widget_t *current = widget->parent;
    while (!event->handled && current) {
        event_localize_mouse_to_widget(current, event);
        if (current->vtable && current->vtable->handle_event) {
            if (current->vtable->handle_event(current, event)) {
                event->handled = true;
                if (restore_mouse) {
                    event->mouse.x = target_mouse_x;
                    event->mouse.y = target_mouse_y;
                }
                return true;
            }
        }
        current = current->parent;
    }

    if (handled)
        event->handled = true;
    if (restore_mouse) {
        event->mouse.x = target_mouse_x;
        event->mouse.y = target_mouse_y;
    }

    return event->handled || handled;
}
