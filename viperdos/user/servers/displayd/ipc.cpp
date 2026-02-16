//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file ipc.cpp
 * @brief IPC protocol handlers for displayd.
 */

#include "ipc.hpp"
#include "compositor.hpp"
#include "events.hpp"
#include "state.hpp"
#include "surface.hpp"

namespace displayd {

void handle_create_surface(int32_t client_channel,
                           const uint8_t *data,
                           size_t len,
                           const uint32_t *handles,
                           uint32_t handle_count) {
    if (len < sizeof(CreateSurfaceRequest))
        return;
    auto *req = reinterpret_cast<const CreateSurfaceRequest *>(data);

    CreateSurfaceReply reply;
    reply.type = DISP_CREATE_SURFACE_REPLY;
    reply.request_id = req->request_id;

    // Find free surface slot
    Surface *surf = nullptr;
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (!g_surfaces[i].in_use) {
            surf = &g_surfaces[i];
            break;
        }
    }

    if (!surf) {
        reply.status = -1;
        reply.surface_id = 0;
        reply.stride = 0;
        sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Allocate shared memory for surface pixels
    uint32_t stride = req->width * 4;
    uint64_t size = static_cast<uint64_t>(stride) * req->height;

    auto shm_result = sys::shm_create(size);
    if (shm_result.error != 0) {
        reply.status = -2;
        reply.surface_id = 0;
        reply.stride = 0;
        sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Initialize surface
    surf->id = g_next_surface_id++;
    surf->width = req->width;
    surf->height = req->height;
    surf->stride = stride;
    // Cascade windows with better spread (10 positions before repeating)
    uint32_t cascade_idx = g_next_surface_id % 10;
    surf->x = static_cast<int32_t>(SCREEN_BORDER_WIDTH + 40 + cascade_idx * 30);
    surf->y = static_cast<int32_t>(SCREEN_BORDER_WIDTH + TITLE_BAR_HEIGHT + 40 + cascade_idx * 25);
    surf->visible = true;
    surf->in_use = true;
    surf->shm_handle = shm_result.handle;
    surf->pixels = reinterpret_cast<uint32_t *>(shm_result.virt_addr);
    surf->event_channel = -1; // No event channel until client subscribes
    surf->event_queue.init();
    surf->flags = req->flags;
    // SYSTEM surfaces (like desktop/workbench) stay at z-order 0 (always behind)
    // Other windows get highest z-order (on top)
    if (surf->flags & SURFACE_FLAG_SYSTEM) {
        surf->z_order = 0;
    } else {
        surf->z_order = g_next_z_order++;
    }
    surf->minimized = false;
    surf->maximized = false;

    // Initialize scrollbar state
    surf->vscroll.enabled = false;
    surf->vscroll.content_size = 0;
    surf->vscroll.viewport_size = 0;
    surf->vscroll.scroll_pos = 0;
    surf->hscroll.enabled = false;
    surf->hscroll.content_size = 0;
    surf->hscroll.viewport_size = 0;
    surf->hscroll.scroll_pos = 0;

    debug_print("[displayd] Created surface id=");
    debug_print_dec(surf->id);
    debug_print(" flags=");
    debug_print_dec(surf->flags);
    debug_print(" at ");
    debug_print_dec(surf->x);
    debug_print(",");
    debug_print_dec(surf->y);
    debug_print("\n");
    surf->saved_x = surf->x;
    surf->saved_y = surf->y;
    surf->saved_width = surf->width;
    surf->saved_height = surf->height;

    // Copy title
    for (int i = 0; i < 63 && req->title[i]; i++) {
        surf->title[i] = req->title[i];
    }
    surf->title[63] = '\0';

    // Clear surface to desktop color (avoids white flash before client renders)
    for (uint32_t y = 0; y < surf->height; y++) {
        for (uint32_t x = 0; x < surf->width; x++) {
            surf->pixels[y * (stride / 4) + x] = COLOR_DESKTOP;
        }
    }

    // Set focus to new surface (unless it's a SYSTEM surface like desktop)
    if (!(surf->flags & SURFACE_FLAG_SYSTEM)) {
        g_focused_surface = surf->id;
    }

    // If client passed an event channel with CREATE_SURFACE, store it now.
    // This eliminates the race between surface creation and event subscription.
    if (handle_count > 0) {
        surf->event_channel = static_cast<int32_t>(handles[0]);
        flush_events(surf); // Drain any events queued during creation
    }

    reply.status = 0;
    reply.surface_id = surf->id;
    reply.stride = stride;

    // Transfer SHM handle to client
    uint32_t send_handles[1] = {shm_result.handle};
    sys::channel_send(client_channel, &reply, sizeof(reply), send_handles, 1);

    debug_print("[displayd] Created surface ");
    debug_print_dec(surf->id);
    debug_print(" (");
    debug_print_dec(surf->width);
    debug_print("x");
    debug_print_dec(surf->height);
    debug_print(")\n");

    // Recomposite
    composite();
}

void handle_request(int32_t client_channel,
                    const uint8_t *data,
                    size_t len,
                    const uint32_t *handles,
                    uint32_t handle_count) {
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type) {
        case DISP_GET_INFO: {
            debug_print("[displayd] Handling DISP_GET_INFO, client_channel=");
            debug_print_dec(client_channel);
            debug_print("\n");

            if (len < sizeof(GetInfoRequest))
                return;
            auto *req = reinterpret_cast<const GetInfoRequest *>(data);

            GetInfoReply reply;
            reply.type = DISP_INFO_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.width = g_fb_width;
            reply.height = g_fb_height;
            reply.format = 0x34325258; // XRGB8888

            int64_t send_result =
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            debug_print("[displayd] DISP_GET_INFO reply sent, result=");
            debug_print_dec(send_result);
            debug_print("\n");
            break;
        }

        case DISP_CREATE_SURFACE:
            handle_create_surface(client_channel, data, len, handles, handle_count);
            break;

        case DISP_DESTROY_SURFACE: {
            if (len < sizeof(DestroySurfaceRequest))
                return;
            auto *req = reinterpret_cast<const DestroySurfaceRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
                    sys::shm_close(g_surfaces[i].shm_handle);
                    // Close event channel if subscribed
                    if (g_surfaces[i].event_channel >= 0) {
                        sys::channel_close(g_surfaces[i].event_channel);
                        g_surfaces[i].event_channel = -1;
                    }
                    g_surfaces[i].in_use = false;
                    g_surfaces[i].pixels = nullptr;
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_PRESENT: {
            if (len < sizeof(PresentRequest))
                return;

            // Recomposite
            composite();

            // Only send reply if client provided a reply channel
            if (client_channel >= 0) {
                auto *req = reinterpret_cast<const PresentRequest *>(data);
                GenericReply reply;
                reply.type = DISP_GENERIC_REPLY;
                reply.request_id = req->request_id;
                reply.status = 0;
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            }
            break;
        }

        case DISP_SET_GEOMETRY: {
            if (len < sizeof(SetGeometryRequest))
                return;
            auto *req = reinterpret_cast<const SetGeometryRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
                    g_surfaces[i].x = req->x;
                    int32_t new_y = req->y;
                    // Clamp Y so title bar stays below menu bar (for decorated windows)
                    if (!(g_surfaces[i].flags & SURFACE_FLAG_NO_DECORATIONS) &&
                        new_y < MIN_WINDOW_Y) {
                        new_y = MIN_WINDOW_Y;
                    }
                    g_surfaces[i].y = new_y;
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_SET_VISIBLE: {
            if (len < sizeof(SetVisibleRequest))
                return;
            auto *req = reinterpret_cast<const SetVisibleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
                    g_surfaces[i].visible = (req->visible != 0);
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_SET_TITLE: {
            if (len < sizeof(SetTitleRequest))
                return;
            auto *req = reinterpret_cast<const SetTitleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                // Copy new title (safely)
                for (int i = 0; i < 63 && req->title[i]; i++) {
                    surf->title[i] = req->title[i];
                }
                surf->title[63] = '\0';
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite(); // Redraw to update title bar
            break;
        }

        case DISP_SUBSCRIBE_EVENTS: {
            if (len < sizeof(SubscribeEventsRequest))
                return;
            auto *req = reinterpret_cast<const SubscribeEventsRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            // Find the surface and store the event channel
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf && handle_count > 0) {
                // Close old event channel if any
                if (surf->event_channel >= 0) {
                    sys::channel_close(surf->event_channel);
                }
                // Store the new event channel (write endpoint from client)
                surf->event_channel = static_cast<int32_t>(handles[0]);
                reply.status = 0;
                flush_events(surf); // Drain any events queued before subscription

                debug_print("[displayd] Subscribed events for surface ");
                debug_print_dec(surf->id);
                debug_print(" channel=");
                debug_print_dec(surf->event_channel);
                debug_print("\n");
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_POLL_EVENT: {
            if (len < sizeof(PollEventRequest))
                return;
            auto *req = reinterpret_cast<const PollEventRequest *>(data);

            PollEventReply reply;
            reply.type = DISP_POLL_EVENT_REPLY;
            reply.request_id = req->request_id;
            reply.has_event = 0;
            reply.event_type = 0;

            // Find the surface and check for events
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                QueuedEvent ev;
                if (surf->event_queue.pop(&ev)) {
                    reply.has_event = 1;
                    reply.event_type = ev.event_type;

                    // Copy event data based on type
                    switch (ev.event_type) {
                        case DISP_EVENT_KEY:
                            reply.key = ev.key;
                            break;
                        case DISP_EVENT_MOUSE:
                            reply.mouse = ev.mouse;
                            break;
                        case DISP_EVENT_FOCUS:
                            reply.focus = ev.focus;
                            break;
                        case DISP_EVENT_CLOSE:
                            reply.close = ev.close;
                            break;
                    }
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_LIST_WINDOWS: {
            if (len < sizeof(ListWindowsRequest))
                return;
            auto *req = reinterpret_cast<const ListWindowsRequest *>(data);

            ListWindowsReply reply;
            reply.type = DISP_LIST_WINDOWS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.window_count = 0;

            // Collect all non-system windows
            for (uint32_t i = 0; i < MAX_SURFACES && reply.window_count < 16; i++) {
                Surface *surf = &g_surfaces[i];
                if (!surf->in_use)
                    continue;
                if (surf->flags & SURFACE_FLAG_SYSTEM)
                    continue;

                WindowInfo &info = reply.windows[reply.window_count];
                info.surface_id = surf->id;
                info.flags = surf->flags;
                info.minimized = surf->minimized ? 1 : 0;
                info.maximized = surf->maximized ? 1 : 0;
                info.focused = (g_focused_surface == surf->id) ? 1 : 0;

                // Copy title
                for (int j = 0; j < 63 && surf->title[j]; j++) {
                    info.title[j] = surf->title[j];
                }
                info.title[63] = '\0';

                reply.window_count++;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_RESTORE_WINDOW: {
            if (len < sizeof(RestoreWindowRequest))
                return;
            auto *req = reinterpret_cast<const RestoreWindowRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                surf->minimized = false;
                bring_to_front(surf);
                g_focused_surface = surf->id;
                composite();
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_SET_SCROLLBAR: {
            if (len < sizeof(SetScrollbarRequest))
                return;
            auto *req = reinterpret_cast<const SetScrollbarRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                if (req->vertical) {
                    surf->vscroll.enabled = req->enabled != 0;
                    surf->vscroll.content_size = req->content_size;
                    surf->vscroll.viewport_size = req->viewport_size;
                    surf->vscroll.scroll_pos = req->scroll_pos;
                } else {
                    surf->hscroll.enabled = req->enabled != 0;
                    surf->hscroll.content_size = req->content_size;
                    surf->hscroll.viewport_size = req->viewport_size;
                    surf->hscroll.scroll_pos = req->scroll_pos;
                }
                composite();
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_REQUEST_FOCUS: {
            if (len < sizeof(RequestFocusRequest))
                return;
            auto *req = reinterpret_cast<const RequestFocusRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf && !(surf->flags & SURFACE_FLAG_SYSTEM)) {
                g_focused_surface = surf->id;
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_SET_MENU: {
            // Handle global menu bar registration (Amiga/Mac style)
            if (len < sizeof(SetMenuRequest)) {
                return;
            }
            auto *req = reinterpret_cast<const SetMenuRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                // Store menu definitions for this surface
                surf->menu_count = req->menu_count;
                if (surf->menu_count > MAX_MENUS) {
                    surf->menu_count = MAX_MENUS;
                }

                // Copy menu data
                for (uint8_t i = 0; i < surf->menu_count; i++) {
                    surf->menus[i] = req->menus[i];
                }

                reply.status = 0;
                composite(); // Redraw menu bar
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[displayd] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

} // namespace displayd
