//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_scale.c
// Purpose: Implement wl_output tracking and wl_surface buffer scale updates.
// Key invariants: See vgfx_wayland_scale.h.
// Ownership/Lifetime: See vgfx_wayland_scale.h.
// Links: src/lib/graphics/src/vgfx_wayland_scale.h
//
//===----------------------------------------------------------------------===//

#include "vgfx_wayland_scale.h"

#include <string.h>

enum {
    WL_OUTPUT_RELEASE = 0,
    WL_OUTPUT_MODE_CURRENT = 1,
    WL_SURFACE_SET_BUFFER_SCALE = 8,
    WP_VIEWPORTER_GET_VIEWPORT = 1,
    WP_VIEWPORT_SET_DESTINATION = 2,
    WP_FRACTIONAL_MANAGER_GET_SCALE = 1,
    WP_FRACTIONAL_SCALE_DESTROY = 0,
    WP_VIEWPORT_DESTROY = 0,
    WL_MARSHAL_FLAG_DESTROY = 1,
};

typedef struct {
    void (*geometry)(void *, struct wl_proxy *, int32_t, int32_t, int32_t, int32_t, int32_t,
                     const char *, const char *, int32_t);
    void (*mode)(void *, struct wl_proxy *, uint32_t, int32_t, int32_t, int32_t);
    void (*done)(void *, struct wl_proxy *);
    void (*scale)(void *, struct wl_proxy *, int32_t);
    void (*name)(void *, struct wl_proxy *, const char *);
    void (*description)(void *, struct wl_proxy *, const char *);
} vgfx_wl_output_listener_t;

typedef struct {
    void (*preferred_scale)(void *, struct wp_fractional_scale_v1 *, uint32_t scale_120);
} vgfx_wp_fractional_scale_listener_t;

static vgfx_wayland_output_t *vgfx_scale_output(vgfx_wayland_scale_t *scale,
                                                struct wl_proxy *proxy) {
    for (uint32_t i = 0; scale && i < scale->output_count; ++i)
        if (scale->outputs[i].proxy == proxy)
            return &scale->outputs[i];
    return NULL;
}

static void vgfx_scale_recompute(vgfx_wayland_scale_t *scale) {
    if (!scale || !scale->shell || !scale->shell->surface)
        return;
    int32_t factor = vgfx_wayland_scale_select_factor(scale->outputs, scale->output_count);
    if (factor == scale->scale)
        return;
    scale->scale = factor;
    scale->generation++;
    if (!scale->fractional_scale &&
        scale->connection->api.proxy_get_version((struct wl_proxy *)scale->shell->surface) >= 3)
        (void)scale->connection->api.proxy_marshal_flags(
            (struct wl_proxy *)scale->shell->surface,
            WL_SURFACE_SET_BUFFER_SCALE,
            NULL,
            scale->connection->api.proxy_get_version((struct wl_proxy *)scale->shell->surface),
            0,
            factor);
}

static void vgfx_fractional_preferred(void *data,
                                      struct wp_fractional_scale_v1 *fractional_scale,
                                      uint32_t scale_120) {
    (void)fractional_scale;
    vgfx_wayland_scale_t *scale = data;
    if (!scale || scale_120 == 0 || scale->preferred_scale_120 == scale_120)
        return;
    scale->preferred_scale_120 = scale_120;
    scale->generation++;
    if (scale->connection->api.proxy_get_version((struct wl_proxy *)scale->shell->surface) >= 3)
        (void)scale->connection->api.proxy_marshal_flags(
            (struct wl_proxy *)scale->shell->surface,
            WL_SURFACE_SET_BUFFER_SCALE,
            NULL,
            scale->connection->api.proxy_get_version((struct wl_proxy *)scale->shell->surface),
            0,
            1);
}

static const vgfx_wp_fractional_scale_listener_t g_fractional_listener = {
    vgfx_fractional_preferred};

int32_t vgfx_wayland_scale_select_factor(const vgfx_wayland_output_t *outputs, uint32_t count) {
    int32_t factor = 1;
    for (uint32_t i = 0; outputs && i < count; ++i)
        if (outputs[i].entered && outputs[i].scale > factor)
            factor = outputs[i].scale;
    return factor;
}

static void vgfx_output_geometry(void *data,
                                 struct wl_proxy *output,
                                 int32_t x,
                                 int32_t y,
                                 int32_t physical_width,
                                 int32_t physical_height,
                                 int32_t subpixel,
                                 const char *make,
                                 const char *model,
                                 int32_t transform) {
    (void)data; (void)output; (void)x; (void)y; (void)physical_width; (void)physical_height;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void vgfx_output_mode(void *data,
                             struct wl_proxy *output,
                             uint32_t flags,
                             int32_t width,
                             int32_t height,
                             int32_t refresh) {
    (void)refresh;
    vgfx_wayland_output_t *item = vgfx_scale_output(data, output);
    if (item && (flags & WL_OUTPUT_MODE_CURRENT)) {
        item->mode_width = width;
        item->mode_height = height;
    }
}

static void vgfx_output_done(void *data, struct wl_proxy *output) {
    (void)output;
    vgfx_scale_recompute(data);
}

static void vgfx_output_scale(void *data, struct wl_proxy *output, int32_t factor) {
    vgfx_wayland_scale_t *scale = data;
    vgfx_wayland_output_t *item = vgfx_scale_output(scale, output);
    if (item)
        item->scale = factor > 0 ? factor : 1;
    vgfx_scale_recompute(scale);
}

static void vgfx_output_name(void *data, struct wl_proxy *output, const char *name) {
    (void)data; (void)output; (void)name;
}
static void vgfx_output_description(void *data, struct wl_proxy *output, const char *description) {
    (void)data; (void)output; (void)description;
}

static const vgfx_wl_output_listener_t g_output_listener = {
    vgfx_output_geometry, vgfx_output_mode, vgfx_output_done,
    vgfx_output_scale, vgfx_output_name, vgfx_output_description};

static void vgfx_scale_bind(vgfx_wayland_scale_t *scale, uint32_t name, uint32_t version) {
    if (!scale || scale->output_count >= 16)
        return;
    for (uint32_t i = 0; i < scale->output_count; ++i)
        if (scale->outputs[i].name == name)
            return;
    struct wl_proxy *proxy = vgfx_wayland_registry_bind(&scale->connection->api,
                                                        scale->connection->registry,
                                                        name,
                                                        scale->connection->api.output_interface,
                                                        version,
                                                        4);
    if (!proxy)
        return;
    vgfx_wayland_output_t *item = &scale->outputs[scale->output_count++];
    memset(item, 0, sizeof(*item));
    item->proxy = proxy;
    item->name = name;
    item->scale = 1;
    if (scale->connection->api.proxy_add_listener(
            proxy, (void (**)(void))(void *)&g_output_listener, scale) != 0) {
        scale->output_count--;
        scale->connection->api.proxy_destroy(proxy);
    }
}

static void vgfx_scale_global(void *data,
                              uint32_t name,
                              const char *interface,
                              uint32_t version) {
    if (interface && strcmp(interface, "wl_output") == 0)
        vgfx_scale_bind(data, name, version);
}

static void vgfx_scale_remove(void *data, uint32_t name) {
    vgfx_wayland_scale_t *scale = data;
    if (!scale)
        return;
    for (uint32_t i = 0; i < scale->output_count; ++i) {
        if (scale->outputs[i].name != name)
            continue;
        struct wl_proxy *proxy = scale->outputs[i].proxy;
        uint32_t version = scale->connection->api.proxy_get_version(proxy);
        if (version >= 3)
            (void)scale->connection->api.proxy_marshal_flags(
                proxy, WL_OUTPUT_RELEASE, NULL, version, WL_MARSHAL_FLAG_DESTROY);
        else
            scale->connection->api.proxy_destroy(proxy);
        scale->output_count--;
        scale->outputs[i] = scale->outputs[scale->output_count];
        break;
    }
    vgfx_scale_recompute(scale);
}

static void vgfx_scale_surface(void *data, struct wl_proxy *output, int32_t entered) {
    vgfx_wayland_scale_t *scale = data;
    vgfx_wayland_output_t *item = vgfx_scale_output(scale, output);
    if (item)
        item->entered = entered ? 1 : 0;
    vgfx_scale_recompute(scale);
}

int vgfx_wayland_scale_open(vgfx_wayland_scale_t *scale,
                            vgfx_wayland_connection_t *connection,
                            vgfx_wayland_shell_t *shell) {
    if (!scale || !connection || !shell)
        return 0;
    memset(scale, 0, sizeof(*scale));
    scale->connection = connection;
    scale->shell = shell;
    scale->scale = 1;
    connection->global_observer = vgfx_scale_global;
    connection->global_remove_observer = vgfx_scale_remove;
    connection->global_observer_data = scale;
    shell->output_observer = vgfx_scale_surface;
    shell->output_observer_data = scale;
    uint32_t count = connection->output_count;
    uint32_t names[16];
    uint32_t versions[16];
    memcpy(names, connection->output_names, count * sizeof(*names));
    memcpy(versions, connection->output_versions, count * sizeof(*versions));
    for (uint32_t i = 0; i < count; ++i)
        vgfx_scale_bind(scale, names[i], versions[i]);
    if (connection->viewporter && connection->fractional_scale_manager_v1) {
        scale->viewport = (struct wp_viewport *)connection->api.proxy_marshal_flags(
            (struct wl_proxy *)connection->viewporter,
            WP_VIEWPORTER_GET_VIEWPORT,
            &vgfx_wp_viewport_interface,
            1,
            0,
            NULL,
            shell->surface);
        scale->fractional_scale =
            (struct wp_fractional_scale_v1 *)connection->api.proxy_marshal_flags(
                (struct wl_proxy *)connection->fractional_scale_manager_v1,
                WP_FRACTIONAL_MANAGER_GET_SCALE,
                &vgfx_wp_fractional_scale_v1_interface,
                1,
                0,
                NULL,
                shell->surface);
        if (!scale->viewport || !scale->fractional_scale ||
            connection->api.proxy_add_listener(
                (struct wl_proxy *)scale->fractional_scale,
                (void (**)(void))(void *)&g_fractional_listener,
                scale) != 0) {
            if (scale->fractional_scale)
                (void)connection->api.proxy_marshal_flags(
                    (struct wl_proxy *)scale->fractional_scale,
                    WP_FRACTIONAL_SCALE_DESTROY,
                    NULL,
                    1,
                    WL_MARSHAL_FLAG_DESTROY);
            if (scale->viewport)
                (void)connection->api.proxy_marshal_flags((struct wl_proxy *)scale->viewport,
                                                          WP_VIEWPORT_DESTROY,
                                                          NULL,
                                                          1,
                                                          WL_MARSHAL_FLAG_DESTROY);
            scale->fractional_scale = NULL;
            scale->viewport = NULL;
        }
    }
    return connection->api.display_roundtrip(connection->display) >= 0;
}

void vgfx_wayland_scale_close(vgfx_wayland_scale_t *scale) {
    if (!scale)
        return;
    if (scale->connection && scale->connection->global_observer_data == scale) {
        scale->connection->global_observer = NULL;
        scale->connection->global_remove_observer = NULL;
        scale->connection->global_observer_data = NULL;
    }
    if (scale->shell && scale->shell->output_observer_data == scale) {
        scale->shell->output_observer = NULL;
        scale->shell->output_observer_data = NULL;
    }
    if (scale->fractional_scale && scale->connection)
        (void)scale->connection->api.proxy_marshal_flags(
            (struct wl_proxy *)scale->fractional_scale,
            WP_FRACTIONAL_SCALE_DESTROY,
            NULL,
            1,
            WL_MARSHAL_FLAG_DESTROY);
    if (scale->viewport && scale->connection)
        (void)scale->connection->api.proxy_marshal_flags((struct wl_proxy *)scale->viewport,
                                                        WP_VIEWPORT_DESTROY,
                                                        NULL,
                                                        1,
                                                        WL_MARSHAL_FLAG_DESTROY);
    while (scale->output_count > 0)
        vgfx_scale_remove(scale, scale->outputs[0].name);
    memset(scale, 0, sizeof(*scale));
}

float vgfx_wayland_scale_factor(const vgfx_wayland_scale_t *scale) {
    if (scale && scale->fractional_scale && scale->preferred_scale_120 > 0)
        return (float)scale->preferred_scale_120 / 120.0f;
    return (float)(scale && scale->scale > 0 ? scale->scale : 1);
}

void vgfx_wayland_scale_set_logical_size(vgfx_wayland_scale_t *scale,
                                         int32_t width,
                                         int32_t height) {
    if (!scale || !scale->viewport || width <= 0 || height <= 0 ||
        (scale->logical_width == width && scale->logical_height == height))
        return;
    scale->logical_width = width;
    scale->logical_height = height;
    (void)scale->connection->api.proxy_marshal_flags((struct wl_proxy *)scale->viewport,
                                                     WP_VIEWPORT_SET_DESTINATION,
                                                     NULL,
                                                     1,
                                                     0,
                                                     width,
                                                     height);
}

int vgfx_wayland_scale_monitor_size(const vgfx_wayland_scale_t *scale,
                                    int32_t *width,
                                    int32_t *height) {
    if (!scale)
        return 0;
    for (uint32_t i = 0; i < scale->output_count; ++i) {
        if (scale->outputs[i].entered && scale->outputs[i].mode_width > 0 &&
            scale->outputs[i].mode_height > 0) {
            if (width) *width = scale->outputs[i].mode_width;
            if (height) *height = scale->outputs[i].mode_height;
            return 1;
        }
    }
    return 0;
}
