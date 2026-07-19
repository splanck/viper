//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_wayland_loader.c
// Purpose: Validate atomic, header-free loading of the system Wayland client ABI.
// Key invariants:
//   - A missing library produces a stable diagnostic and an empty table.
//   - A successful load exposes all core interfaces needed by the backend.
// Ownership/Lifetime: Test binary owns and closes every loader table it opens.
// Links: src/lib/graphics/src/vgfx_wayland_loader.c
//
//===----------------------------------------------------------------------===//

#include "test_harness.h"
#include "vgfx_wayland_activation.h"
#include "vgfx_wayland_loader.h"
#include "vgfx_wayland_connection.h"
#include "vgfx_wayland_data.h"
#include "vgfx_wayland_input.h"
#include "vgfx_wayland_relative.h"
#include "vgfx_wayland_protocol.h"
#include "vgfx_wayland_shell.h"
#include "vgfx_wayland_scale.h"
#include "vgfx_wayland_shm.h"
#include "vgfx_wayland_text_input.h"

#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

static void test_missing_wayland_library_fails_atomically(void) {
    TEST_BEGIN("Wayland loader: missing library fails atomically");
    vgfx_wayland_client_api_t api;
    char error[256];
    ASSERT_EQ(vgfx_wayland_loader_open(
                  &api, "libzanna-wayland-intentionally-missing.so", error, sizeof(error)),
              0);
    ASSERT_NULL(api.library);
    ASSERT_NULL(api.display_connect);
    ASSERT_TRUE(strstr(error, "Wayland backend unavailable:") == error);
    TEST_END();
}

static void test_system_wayland_library_loads_complete_core(void) {
    TEST_BEGIN("Wayland loader: system client ABI is complete");
    vgfx_wayland_client_api_t api;
    char error[256];
    if (!vgfx_wayland_loader_open(&api, NULL, error, sizeof(error))) {
        printf("[  SKIPPED ] optional Wayland runtime is unavailable: %s\n", error);
        TEST_END();
        return;
    }
    ASSERT_NOT_NULL(api.library);
    ASSERT_NOT_NULL(api.display_connect);
    ASSERT_NOT_NULL(api.proxy_marshal_flags);
    ASSERT_NOT_NULL(api.registry_interface);
    ASSERT_NOT_NULL(api.compositor_interface);
    ASSERT_NOT_NULL(api.subcompositor_interface);
    ASSERT_NOT_NULL(api.subsurface_interface);
    ASSERT_NOT_NULL(api.shm_interface);
    ASSERT_NOT_NULL(api.seat_interface);
    ASSERT_NOT_NULL(api.data_device_manager_interface);
    vgfx_wayland_loader_close(&api);
    ASSERT_NULL(api.library);
    ASSERT_NULL(api.display_connect);
    TEST_END();
}

static void assert_protocol_object_types(const struct wl_interface *interface) {
    const struct wl_message *groups[] = {interface->methods, interface->events};
    const int counts[] = {interface->method_count, interface->event_count};
    for (int group = 0; group < 2; ++group) {
        for (int message_index = 0; message_index < counts[group]; ++message_index) {
            const struct wl_message *message = &groups[group][message_index];
            size_t argument_index = 0;
            for (const char *cursor = message->signature; cursor && *cursor; ++cursor) {
                if (*cursor >= '0' && *cursor <= '9')
                    continue;
                if (*cursor == '?')
                    continue;
                if (*cursor == 'o' || *cursor == 'n') {
                    ASSERT_NOT_NULL(message->types);
                    if (message->types)
                        ASSERT_NOT_NULL(message->types[argument_index]);
                }
                ++argument_index;
            }
        }
    }
}

static void test_protocol_object_metadata_is_complete(void) {
    TEST_BEGIN("Wayland protocol: every object argument has interface metadata");
    const struct wl_interface *interfaces[] = {
        &vgfx_xdg_wm_base_interface,
        &vgfx_xdg_positioner_interface,
        &vgfx_xdg_surface_interface,
        &vgfx_xdg_toplevel_interface,
        &vgfx_xdg_popup_interface,
        &vgfx_zwp_text_input_manager_v3_interface,
        &vgfx_zwp_text_input_v3_interface,
        &vgfx_wp_viewporter_interface,
        &vgfx_wp_viewport_interface,
        &vgfx_wp_fractional_scale_manager_v1_interface,
        &vgfx_wp_fractional_scale_v1_interface,
        &vgfx_zxdg_decoration_manager_v1_interface,
        &vgfx_zxdg_toplevel_decoration_v1_interface,
        &vgfx_zwp_relative_pointer_manager_v1_interface,
        &vgfx_zwp_relative_pointer_v1_interface,
        &vgfx_zwp_pointer_constraints_v1_interface,
        &vgfx_zwp_locked_pointer_v1_interface,
        &vgfx_xdg_activation_v1_interface,
        &vgfx_xdg_activation_token_v1_interface,
    };
    for (size_t i = 0; i < sizeof(interfaces) / sizeof(interfaces[0]); ++i)
        assert_protocol_object_types(interfaces[i]);
    TEST_END();
}

static void test_wayland_connection_rejects_missing_library(void) {
    TEST_BEGIN("Wayland connection: loader failure leaves empty state");
    vgfx_wayland_connection_t connection;
    char error[256];
    ASSERT_EQ(vgfx_wayland_connection_open(&connection,
                                           NULL,
                                           "libzanna-wayland-intentionally-missing.so",
                                           error,
                                           sizeof(error)),
              0);
    ASSERT_NULL(connection.api.library);
    ASSERT_NULL(connection.display);
    ASSERT_NULL(connection.registry);
    ASSERT_EQ(connection.globals, 0);
    ASSERT_TRUE(strstr(error, "Wayland backend unavailable:") == error);
    vgfx_wayland_connection_close(&connection);
    TEST_END();
}

static void test_wayland_input_translation_is_stable(void) {
    TEST_BEGIN("Wayland input: fixed coordinates and evdev keys translate deterministically");
    ASSERT_EQ(vgfx_wayland_fixed_to_pixel(0), 0);
    ASSERT_EQ(vgfx_wayland_fixed_to_pixel(384), 2);
    ASSERT_EQ(vgfx_wayland_fixed_to_pixel(-384), -2);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(30), VGFX_KEY_A);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(44), VGFX_KEY_Z);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(11), VGFX_KEY_0);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(103), VGFX_KEY_UP);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(111), VGFX_KEY_DELETE);
    ASSERT_EQ(vgfx_wayland_translate_evdev_key(0xFFFFFFFFu), VGFX_KEY_UNKNOWN);
    ASSERT_TRUE(vgfx_wayland_relative_fixed(384) > 1.499);
    ASSERT_TRUE(vgfx_wayland_relative_fixed(384) < 1.501);
    vgfx_wayland_input_t input = {0};
    ASSERT_EQ(vgfx_wayland_input_clamp_timeout(&input, 250, 1000), 250);
    input.repeat_active = 1;
    input.repeat_rate = 25;
    input.repeat_at_ms = 1040;
    input.pointer_serial = 17;
    ASSERT_EQ(vgfx_wayland_activation_serial(&input), 17);
    input.keyboard_serial = 23;
    ASSERT_EQ(vgfx_wayland_activation_serial(&input), 23);
    ASSERT_EQ(vgfx_wayland_input_clamp_timeout(&input, 250, 1000), 40);
    ASSERT_EQ(vgfx_wayland_input_clamp_timeout(&input, -1, 1000), 40);
    ASSERT_EQ(vgfx_wayland_input_clamp_timeout(&input, 250, 1050), 0);
    TEST_END();
}

static void test_wayland_uri_list_transfer_emits_file_drop(void) {
    TEST_BEGIN("Wayland data: URI-list pipe emits decoded file drops");
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 16;
    params.height = 16;
    vgfx_window_t window = vgfx_create_window(&params);
    ASSERT_NOT_NULL(window);

    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    const char *payload = "# ignored\r\nfile:///tmp/Hello%20Wayland.zia\r\n";
    ASSERT_EQ(write(descriptors[1], payload, strlen(payload)), (ssize_t)strlen(payload));
    close(descriptors[1]);

    vgfx_wayland_data_t data = {0};
    data.window = window;
    for (size_t i = 0; i < sizeof(data.transfers) / sizeof(data.transfers[0]); ++i)
        data.transfers[i].fd = -1;
    data.transfers[0].fd = descriptors[0];
    data.transfers[0].uri_list = 1;
    vgfx_wayland_data_tick(&data);
    vgfx_wayland_data_tick(&data);

    vgfx_event_t event;
    ASSERT_EQ(vgfx_poll_event(window, &event), 1);
    ASSERT_EQ(event.type, VGFX_EVENT_FILE_DROP);
    ASSERT_TRUE(strcmp(event.data.file_drop.path, "/tmp/Hello Wayland.zia") == 0);
    vgfx_destroy_window(window);
    TEST_END();
}

static void test_wayland_text_input_bounds_surrounding_text(void) {
    TEST_BEGIN("Wayland text input: surrounding state is bounded around cursor");
    char surrounding[6001];
    memset(surrounding, 'x', sizeof(surrounding) - 1);
    surrounding[sizeof(surrounding) - 1] = '\0';
    vgfx_wayland_text_input_t text_input = {0};
    vgfx_text_input_state_t state = {.surrounding_text = surrounding,
                                     .cursor_byte = 5000,
                                     .anchor_byte = 100,
                                     .cursor_x = 1,
                                     .cursor_y = 2,
                                     .cursor_width = 1,
                                     .cursor_height = 16,
                                     .purpose = VGFX_TEXT_INPUT_NORMAL};
    ASSERT_EQ(vgfx_wayland_text_input_set_state(&text_input, &state), 1);
    ASSERT_NOT_NULL(text_input.surrounding);
    ASSERT_TRUE(strlen(text_input.surrounding) <= 4000);
    ASSERT_EQ(text_input.cursor_byte, 2000);
    ASSERT_EQ(text_input.anchor_byte, text_input.cursor_byte);
    vgfx_wayland_text_input_close(&text_input);
    TEST_END();
}

static void test_wayland_output_scale_policy(void) {
    TEST_BEGIN("Wayland output: entered outputs choose maximum integer scale");
    vgfx_wayland_output_t outputs[3] = {{.scale = 1, .entered = 1},
                                        {.scale = 3, .entered = 0},
                                        {.scale = 2, .entered = 1}};
    ASSERT_EQ(vgfx_wayland_scale_select_factor(outputs, 3), 2);
    outputs[1].entered = 1;
    ASSERT_EQ(vgfx_wayland_scale_select_factor(outputs, 3), 3);
    outputs[0].entered = outputs[1].entered = outputs[2].entered = 0;
    ASSERT_EQ(vgfx_wayland_scale_select_factor(outputs, 3), 1);
    vgfx_wayland_scale_t fractional = {
        .fractional_scale = (struct wp_fractional_scale_v1 *)(uintptr_t)1,
        .preferred_scale_120 = 150,
        .scale = 2,
    };
    ASSERT_TRUE(vgfx_wayland_scale_factor(&fractional) > 1.249f);
    ASSERT_TRUE(vgfx_wayland_scale_factor(&fractional) < 1.251f);
    TEST_END();
}

static void test_active_wayland_compositor_advertises_required_globals(void) {
    TEST_BEGIN("Wayland connection: active compositor advertises required globals");
    if (!getenv("WAYLAND_DISPLAY")) {
        printf("[  SKIPPED ] WAYLAND_DISPLAY is not set\n");
        TEST_END();
        return;
    }
    vgfx_wayland_connection_t connection;
    char error[256];
    ASSERT_EQ(vgfx_wayland_connection_open(&connection, NULL, NULL, error, sizeof(error)), 1);
    ASSERT_EQ(connection.globals & VGFX_WAYLAND_REQUIRED_GLOBALS, VGFX_WAYLAND_REQUIRED_GLOBALS);
    ASSERT_TRUE(connection.compositor_version >= 1);
    ASSERT_TRUE(connection.shm_version >= 1);
    ASSERT_TRUE(connection.seat_version >= 1);
    ASSERT_TRUE(connection.xdg_wm_base_version >= 1);
    ASSERT_NOT_NULL(connection.compositor);
    ASSERT_NOT_NULL(connection.shm);
    ASSERT_NOT_NULL(connection.seat);
    ASSERT_NOT_NULL(connection.xdg_wm_base);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_DATA_DEVICE_MANAGER) {
        ASSERT_TRUE(connection.data_device_manager_version >= 1);
        ASSERT_NOT_NULL(connection.data_device_manager);
    }
    if (connection.globals & VGFX_WAYLAND_GLOBAL_SUBCOMPOSITOR)
        ASSERT_NOT_NULL(connection.subcompositor);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_VIEWPORTER)
        ASSERT_NOT_NULL(connection.viewporter);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_FRACTIONAL_SCALE_V1)
        ASSERT_NOT_NULL(connection.fractional_scale_manager_v1);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_XDG_DECORATION_V1)
        ASSERT_NOT_NULL(connection.decoration_manager_v1);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_RELATIVE_POINTER_V1)
        ASSERT_NOT_NULL(connection.relative_pointer_manager_v1);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_POINTER_CONSTRAINTS_V1)
        ASSERT_NOT_NULL(connection.pointer_constraints_v1);
    if (connection.globals & VGFX_WAYLAND_GLOBAL_XDG_ACTIVATION_V1)
        ASSERT_NOT_NULL(connection.activation_v1);
    vgfx_wayland_connection_close(&connection);
    ASSERT_NULL(connection.display);
    ASSERT_NULL(connection.registry);
    TEST_END();
}

static void test_active_wayland_compositor_configures_xdg_toplevel(void) {
    TEST_BEGIN("Wayland shell: active compositor configures xdg toplevel");
    if (!getenv("WAYLAND_DISPLAY")) {
        printf("[  SKIPPED ] WAYLAND_DISPLAY is not set\n");
        TEST_END();
        return;
    }
    vgfx_wayland_connection_t connection;
    vgfx_wayland_shell_t shell;
    char error[256];
    ASSERT_EQ(vgfx_wayland_connection_open(&connection, NULL, NULL, error, sizeof(error)), 1);
    ASSERT_EQ(vgfx_wayland_shell_open(
                  &shell, &connection, "Zanna Wayland Test", "org.zanna.test", error, sizeof(error)),
              1);
    ASSERT_NOT_NULL(shell.surface);
    ASSERT_NOT_NULL(shell.xdg_surface);
    ASSERT_NOT_NULL(shell.toplevel);
    ASSERT_EQ(shell.configured, 1);
    if (connection.decoration_manager_v1) {
        ASSERT_NOT_NULL(shell.decoration);
        ASSERT_TRUE(shell.decoration_mode == 1 || shell.decoration_mode == 2);
    }
    vgfx_wayland_shell_close(&shell);
    ASSERT_NULL(shell.surface);
    ASSERT_NULL(shell.xdg_surface);
    ASSERT_NULL(shell.toplevel);
    vgfx_wayland_connection_close(&connection);
    TEST_END();
}

static void test_active_wayland_compositor_accepts_shm_frame(void) {
    TEST_BEGIN("Wayland shm: active compositor accepts software frame");
    if (!getenv("WAYLAND_DISPLAY")) {
        printf("[  SKIPPED ] WAYLAND_DISPLAY is not set\n");
        TEST_END();
        return;
    }
    enum { WIDTH = 64, HEIGHT = 48 };
    uint8_t rgba[WIDTH * HEIGHT * 4];
    for (size_t i = 0; i < sizeof(rgba); i += 4) {
        rgba[i] = 0x22;
        rgba[i + 1] = 0x66;
        rgba[i + 2] = 0xCC;
        rgba[i + 3] = 0xFF;
    }
    vgfx_wayland_connection_t connection;
    vgfx_wayland_shell_t shell;
    vgfx_wayland_shm_presenter_t presenter;
    char error[256];
    ASSERT_EQ(vgfx_wayland_connection_open(&connection, NULL, NULL, error, sizeof(error)), 1);
    ASSERT_EQ(vgfx_wayland_shell_open(
                  &shell, &connection, "Zanna SHM Test", "org.zanna.test", error, sizeof(error)),
              1);
    ASSERT_EQ(vgfx_wayland_shm_open(&presenter, &shell, WIDTH, HEIGHT, error, sizeof(error)), 1);
    ASSERT_EQ(vgfx_wayland_shm_present(&presenter, rgba, sizeof(rgba)), 1);
    ASSERT_EQ(presenter.generation, 1);
    ASSERT_NOT_NULL(presenter.frame_callback);
    ASSERT_EQ(vgfx_wayland_shm_present(&presenter, rgba, sizeof(rgba)), 1);
    ASSERT_EQ(presenter.generation, 1);
    ASSERT_EQ(presenter.queued, 1);
    ASSERT_TRUE(connection.api.display_roundtrip(connection.display) >= 0);
    if (presenter.generation < 2) {
        struct pollfd descriptor = {
            .fd = connection.api.display_get_fd(connection.display),
            .events = POLLIN,
            .revents = 0,
        };
        if (poll(&descriptor, 1, 1000) > 0 && (descriptor.revents & POLLIN))
            ASSERT_TRUE(connection.api.display_dispatch(connection.display) >= 0);
    }
    ASSERT_TRUE(presenter.generation >= 2);
    vgfx_wayland_shm_close(&presenter);
    vgfx_wayland_shell_close(&shell);
    vgfx_wayland_connection_close(&connection);
    TEST_END();
}

int main(void) {
    test_missing_wayland_library_fails_atomically();
    test_system_wayland_library_loads_complete_core();
    test_protocol_object_metadata_is_complete();
    test_wayland_connection_rejects_missing_library();
    test_wayland_input_translation_is_stable();
    test_wayland_uri_list_transfer_emits_file_drop();
    test_wayland_text_input_bounds_surrounding_text();
    test_wayland_output_scale_policy();
    test_active_wayland_compositor_advertises_required_globals();
    test_active_wayland_compositor_configures_xdg_toplevel();
    test_active_wayland_compositor_accepts_shm_frame();
    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
