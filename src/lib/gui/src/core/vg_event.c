// vg_event.c - Event system implementation
#include "../../include/vg_event.h"
#include "../../include/vg_widget.h"
#include "vgfx.h"
#include <string.h>

//=============================================================================
// Event Creation Helpers
//=============================================================================

vg_event_t vg_event_mouse(
    vg_event_type_t type, float x, float y, vg_mouse_button_t button, uint32_t modifiers)
{
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

vg_event_t vg_event_key(vg_event_type_t type, vg_key_t key, uint32_t codepoint, uint32_t modifiers)
{
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
static vg_key_t translate_vgfx_key(int vgfx_key)
{
    // Printable ASCII keys are the same
    if (vgfx_key >= ' ' && vgfx_key <= '~')
    {
        return (vg_key_t)vgfx_key;
    }

    // Special keys need translation (vgfx codes -> vg codes)
    // VGFX: ESCAPE=256, ENTER=257, LEFT=258, RIGHT=259, UP=260, DOWN=261, BACKSPACE=262,
    // DELETE=263, TAB=264, HOME=265, END=266 VG:   ESCAPE=256, ENTER=257, TAB=258, BACKSPACE=259,
    // DELETE=261, RIGHT=262, LEFT=263, DOWN=264, UP=265, HOME=268, END=269
    switch (vgfx_key)
    {
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
        default:
            return (vg_key_t)vgfx_key;
    }
}

vg_event_t vg_event_from_platform(void *platform_event)
{
    vg_event_t event;
    memset(&event, 0, sizeof(event));

    if (!platform_event)
        return event;

    vgfx_event_t *pe = (vgfx_event_t *)platform_event;

    switch (pe->type)
    {
        case VGFX_EVENT_KEY_DOWN:
            event.type = VG_EVENT_KEY_DOWN;
            event.key.key = translate_vgfx_key(pe->data.key.key);
            event.key.repeat = pe->data.key.is_repeat != 0;
            break;

        case VGFX_EVENT_KEY_UP:
            event.type = VG_EVENT_KEY_UP;
            event.key.key = translate_vgfx_key(pe->data.key.key);
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

            // Note: vgfx doesn't have scroll events yet - will be added later

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

bool vg_event_dispatch(vg_widget_t *root, vg_event_t *event)
{
    if (!root || !event)
        return false;

    // Find target widget for mouse events
    if (event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
        event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK ||
        event->type == VG_EVENT_DOUBLE_CLICK || event->type == VG_EVENT_MOUSE_WHEEL)
    {
        // Check if a widget has captured input (e.g., open dropdown menu).
        // When capture is active, all mouse events route to the captured widget
        // regardless of hit testing. This allows dropdown menus to receive clicks
        // even though the dropdown renders outside the menubar's widget bounds.
        vg_widget_t *capture = vg_widget_get_input_capture();
        if (capture)
        {
            event->target = capture;

            // Convert to capture-widget-relative coordinates
            float sx, sy, sw, sh;
            vg_widget_get_screen_bounds(capture, &sx, &sy, &sw, &sh);
            event->mouse.x = event->mouse.screen_x - sx;
            event->mouse.y = event->mouse.screen_y - sy;

            // For MOUSE_UP, we need to synthesize a CLICK event because
            // vg_event_send()'s CLICK generation depends on contains_point(),
            // which fails for dropdown clicks outside the widget bounds.
            if (event->type == VG_EVENT_MOUSE_UP)
            {
                // Send MOUSE_UP first
                if (capture->vtable && capture->vtable->handle_event)
                    capture->vtable->handle_event(capture, event);

                // Synthesize and send CLICK
                vg_event_t click_event = *event;
                click_event.type = VG_EVENT_CLICK;
                if (capture->vtable && capture->vtable->handle_event)
                    capture->vtable->handle_event(capture, &click_event);
                return true;
            }

            // For other mouse events, call handle_event directly
            if (capture->vtable && capture->vtable->handle_event)
                return capture->vtable->handle_event(capture, event);
            return false;
        }

        vg_widget_t *target =
            vg_widget_hit_test(root, event->mouse.screen_x, event->mouse.screen_y);
        if (target)
        {
            event->target = target;

            // Convert to target-relative coordinates
            float sx, sy, sw, sh;
            vg_widget_get_screen_bounds(target, &sx, &sy, &sw, &sh);
            event->mouse.x = event->mouse.screen_x - sx;
            event->mouse.y = event->mouse.screen_y - sy;

            return vg_event_send(target, event);
        }
        return false;
    }

    // Keyboard events: route to captured widget first (for menu keyboard navigation),
    // then to focused widget, then to root.
    if (event->type == VG_EVENT_KEY_DOWN || event->type == VG_EVENT_KEY_UP ||
        event->type == VG_EVENT_KEY_CHAR)
    {
        vg_widget_t *capture = vg_widget_get_input_capture();
        if (capture)
        {
            event->target = capture;
            if (capture->vtable && capture->vtable->handle_event)
            {
                if (capture->vtable->handle_event(capture, event))
                    return true;
            }
            // If captured widget didn't handle it, fall through to focused widget
        }

        vg_widget_t *focused = vg_widget_get_focused(root);
        if (focused)
        {
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

bool vg_event_send(vg_widget_t *widget, vg_event_t *event)
{
    if (!widget || !event)
        return false;

    // Handle common state changes for mouse events
    if (event->type == VG_EVENT_MOUSE_ENTER)
    {
        widget->state |= VG_STATE_HOVERED;
        widget->needs_paint = true;
    }
    else if (event->type == VG_EVENT_MOUSE_LEAVE)
    {
        widget->state &= ~(VG_STATE_HOVERED | VG_STATE_PRESSED);
        widget->needs_paint = true;
    }
    else if (event->type == VG_EVENT_MOUSE_DOWN)
    {
        widget->state |= VG_STATE_PRESSED;
        widget->needs_paint = true;
        // Set focus on click if widget can accept focus
        if (widget->vtable && widget->vtable->can_focus && widget->vtable->can_focus(widget))
        {
            vg_widget_set_focus(widget);
        }
    }
    else if (event->type == VG_EVENT_MOUSE_UP)
    {
        bool was_pressed = (widget->state & VG_STATE_PRESSED) != 0;
        widget->state &= ~VG_STATE_PRESSED;
        widget->needs_paint = true;

        // Generate click event if mouse was pressed on this widget
        if (was_pressed &&
            vg_widget_contains_point(widget, event->mouse.screen_x, event->mouse.screen_y))
        {
            vg_event_t click_event = *event;
            click_event.type = VG_EVENT_CLICK;
            vg_event_send(widget, &click_event);
        }
    }

    // Call widget's event handler
    if (widget->vtable && widget->vtable->handle_event)
    {
        if (widget->vtable->handle_event(widget, event))
        {
            event->handled = true;
            return true;
        }
    }

    // Bubble up to parent if not handled
    if (!event->handled && widget->parent)
    {
        return vg_event_send(widget->parent, event);
    }

    return event->handled;
}
