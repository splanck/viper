//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file events.cpp
 * @brief Event queuing for displayd.
 */

#include "events.hpp"
#include "state.hpp"

namespace displayd {

void queue_mouse_event(Surface *surf,
                       uint8_t event_type,
                       int32_t local_x,
                       int32_t local_y,
                       int32_t dx,
                       int32_t dy,
                       uint8_t buttons,
                       uint8_t button) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_MOUSE;
    ev.mouse.type = DISP_EVENT_MOUSE;
    ev.mouse.surface_id = surf->id;
    ev.mouse.x = local_x;
    ev.mouse.y = local_y;
    ev.mouse.dx = dx;
    ev.mouse.dy = dy;
    ev.mouse.buttons = buttons;
    ev.mouse.event_type = event_type;
    ev.mouse.button = button;
    ev.mouse._pad = 0;

    // If client has event channel, send directly (preferred path)
    if (surf->event_channel >= 0) {
        debug_print("[evt] send mouse to ch ");
        debug_print_dec(surf->event_channel);
        debug_print("\n");
        sys::channel_send(surf->event_channel, &ev.mouse, sizeof(ev.mouse), nullptr, 0);
    } else {
        debug_print("[evt] queue mouse (no channel)\n");
        // Fall back to queue for legacy poll-based clients
        if (!surf->event_queue.push(ev)) {
            // Overflow - event dropped (don't spam logs for mouse moves)
        }
    }
}

void queue_scroll_event(Surface *surf, int32_t new_position, bool vertical) {
    ScrollEvent ev;
    ev.type = DISP_EVENT_SCROLL;
    ev.surface_id = surf->id;
    ev.new_position = new_position;
    ev.vertical = vertical ? 1 : 0;
    ev._pad[0] = 0;
    ev._pad[1] = 0;
    ev._pad[2] = 0;

    // Send scroll event to client
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev, sizeof(ev), nullptr, 0);
    }
}

void queue_focus_event(Surface *surf, bool gained) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_FOCUS;
    ev.focus.type = DISP_EVENT_FOCUS;
    ev.focus.surface_id = surf->id;
    ev.focus.gained = gained ? 1 : 0;
    ev.focus._pad[0] = 0;
    ev.focus._pad[1] = 0;
    ev.focus._pad[2] = 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.focus, sizeof(ev.focus), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - focus event dropped
        }
    }
}

void queue_close_event(Surface *surf) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_CLOSE;
    ev.close.type = DISP_EVENT_CLOSE;
    ev.close.surface_id = surf->id;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.close, sizeof(ev.close), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - close event dropped
        }
    }
}

void queue_key_event(Surface *surf, uint16_t keycode, uint8_t modifiers, bool pressed) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_KEY;
    ev.key.type = DISP_EVENT_KEY;
    ev.key.surface_id = surf->id;
    ev.key.keycode = keycode;
    ev.key.modifiers = modifiers;
    ev.key.pressed = pressed ? 1 : 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.key, sizeof(ev.key), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - key event dropped
        }
    }
}

void queue_menu_event(Surface *surf,
                      uint8_t menu_index,
                      uint8_t item_index,
                      uint8_t action) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_MENU;
    ev.menu.type = DISP_EVENT_MENU;
    ev.menu.surface_id = surf->id;
    ev.menu.menu_index = menu_index;
    ev.menu.item_index = item_index;
    ev.menu.action = action;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.menu, sizeof(ev.menu), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - menu event dropped
        }
    }
}

void flush_events(Surface *surf) {
    // Flush any pending events from the legacy queue to the event channel
    if (surf->event_channel < 0)
        return;

    QueuedEvent ev;
    while (surf->event_queue.pop(&ev)) {
        switch (ev.event_type) {
        case DISP_EVENT_KEY:
            sys::channel_send(surf->event_channel, &ev.key, sizeof(ev.key), nullptr, 0);
            break;
        case DISP_EVENT_MOUSE:
            sys::channel_send(surf->event_channel, &ev.mouse, sizeof(ev.mouse), nullptr, 0);
            break;
        case DISP_EVENT_FOCUS:
            sys::channel_send(surf->event_channel, &ev.focus, sizeof(ev.focus), nullptr, 0);
            break;
        case DISP_EVENT_CLOSE:
            sys::channel_send(surf->event_channel, &ev.close, sizeof(ev.close), nullptr, 0);
            break;
        case DISP_EVENT_MENU:
            sys::channel_send(surf->event_channel, &ev.menu, sizeof(ev.menu), nullptr, 0);
            break;
        default:
            break;
        }
    }
}

} // namespace displayd
