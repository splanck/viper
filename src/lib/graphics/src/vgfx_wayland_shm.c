//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_shm.c
// Purpose: Present Zanna's software framebuffer through bounded Wayland shared memory.
// Key invariants:
//   - XRGB8888 bytes are emitted in native little-endian B, G, R, X order.
//   - Backing storage remains mapped until all local buffer proxies are destroyed.
// Ownership/Lifetime: See vgfx_wayland_shm.h.
// Links: Wayland wl_shm, wl_buffer, and wl_surface core protocol definitions.
//
//===----------------------------------------------------------------------===//

#define _POSIX_C_SOURCE 200809L

#include "vgfx_wayland_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    VGFX_WL_SHM_CREATE_POOL = 0,
    VGFX_WL_SHM_POOL_CREATE_BUFFER = 0,
    VGFX_WL_SHM_POOL_DESTROY = 1,
    VGFX_WL_BUFFER_DESTROY = 0,
    VGFX_WL_SHM_FORMAT_XRGB8888 = 1,
    VGFX_WL_SURFACE_DAMAGE_BUFFER = 9,
};

typedef struct vgfx_wl_buffer_listener {
    void (*release)(void *data, struct wl_proxy *buffer);
} vgfx_wl_buffer_listener_t;

typedef struct vgfx_wl_callback_listener {
    void (*done)(void *data, struct wl_proxy *callback, uint32_t callback_data);
} vgfx_wl_callback_listener_t;

static int vgfx_wayland_shm_drain(vgfx_wayland_shm_presenter_t *presenter);

static void vgfx_wayland_shm_error(char *error, uint32_t size, const char *detail) {
    if (error && size > 0)
        (void)snprintf(error, (size_t)size, "Wayland shared-memory setup failed: %s", detail);
}

static void vgfx_wayland_buffer_release(void *data, struct wl_proxy *buffer) {
    (void)buffer;
    vgfx_wayland_shm_slot_t *slot = (vgfx_wayland_shm_slot_t *)data;
    if (slot) {
        slot->busy = 0;
        (void)vgfx_wayland_shm_drain(slot->presenter);
    }
}

static const vgfx_wl_buffer_listener_t g_vgfx_wayland_buffer_listener = {
    .release = vgfx_wayland_buffer_release,
};

static void vgfx_wayland_frame_done(void *data,
                                    struct wl_proxy *callback,
                                    uint32_t callback_data) {
    (void)callback_data;
    vgfx_wayland_shm_presenter_t *presenter = (vgfx_wayland_shm_presenter_t *)data;
    if (!presenter || !presenter->shell)
        return;
    presenter->shell->connection->api.proxy_destroy(callback);
    if (presenter->frame_callback == callback)
        presenter->frame_callback = NULL;
    (void)vgfx_wayland_shm_drain(presenter);
}

static const vgfx_wl_callback_listener_t g_vgfx_wayland_frame_listener = {
    .done = vgfx_wayland_frame_done,
};

static int vgfx_wayland_create_anonymous_file(size_t size) {
    if (size == 0 || size > (size_t)INT64_MAX)
        return -1;
    char path[] = "/tmp/zanna-wayland-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;
    (void)unlink(path);
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0 ||
        ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static uint32_t vgfx_wayland_shm_proxy_version(vgfx_wayland_shm_presenter_t *presenter,
                                               struct wl_proxy *proxy) {
    return presenter->shell->connection->api.proxy_get_version(proxy);
}

static void vgfx_wayland_shm_destroy(vgfx_wayland_shm_presenter_t *presenter,
                                     struct wl_proxy *proxy,
                                     uint32_t opcode) {
    if (!presenter || !presenter->shell || !proxy)
        return;
    (void)presenter->shell->connection->api.proxy_marshal_flags(
        proxy,
        opcode,
        NULL,
        vgfx_wayland_shm_proxy_version(presenter, proxy),
        VGFX_WL_MARSHAL_FLAG_DESTROY);
}

void vgfx_wayland_shm_close(vgfx_wayland_shm_presenter_t *presenter) {
    if (!presenter)
        return;
    if (presenter->frame_callback && presenter->shell)
        presenter->shell->connection->api.proxy_destroy(presenter->frame_callback);
    for (size_t i = 0; i < sizeof(presenter->slots) / sizeof(presenter->slots[0]); ++i) {
        if (presenter->slots[i].buffer)
            vgfx_wayland_shm_destroy(presenter, presenter->slots[i].buffer, VGFX_WL_BUFFER_DESTROY);
    }
    if (presenter->pool)
        vgfx_wayland_shm_destroy(presenter, presenter->pool, VGFX_WL_SHM_POOL_DESTROY);
    if (presenter->mapping && presenter->mapping_size > 0)
        (void)munmap(presenter->mapping, presenter->mapping_size);
    memset(presenter, 0, sizeof(*presenter));
}

int vgfx_wayland_shm_open(vgfx_wayland_shm_presenter_t *presenter,
                          vgfx_wayland_shell_t *shell,
                          int32_t width,
                          int32_t height,
                          char *error,
                          uint32_t error_size) {
    return vgfx_wayland_shm_open_surface(
        presenter, shell, shell ? shell->surface : NULL, width, height, error, error_size);
}

int vgfx_wayland_shm_open_surface(vgfx_wayland_shm_presenter_t *presenter,
                                  vgfx_wayland_shell_t *shell,
                                  struct wl_surface *surface,
                                  int32_t width,
                                  int32_t height,
                                  char *error,
                                  uint32_t error_size) {
    if (error && error_size > 0)
        error[0] = '\0';
    if (!presenter || !shell || !surface || !shell->connection || !shell->connection->shm || width <= 0 ||
        height <= 0 || width > INT32_MAX / 4) {
        vgfx_wayland_shm_error(error, error_size, "invalid dimensions or shell");
        return 0;
    }
    int32_t stride = width * 4;
    size_t buffer_size = (size_t)stride * (size_t)height;
    if (buffer_size == 0 || buffer_size > (size_t)INT32_MAX || buffer_size > SIZE_MAX / 2) {
        vgfx_wayland_shm_error(error, error_size, "buffer size exceeds protocol limits");
        return 0;
    }
    memset(presenter, 0, sizeof(*presenter));
    presenter->shell = shell;
    presenter->surface = surface;
    presenter->width = width;
    presenter->height = height;
    presenter->stride = stride;
    presenter->buffer_size = buffer_size;
    presenter->mapping_size = buffer_size * 2;

    int fd = vgfx_wayland_create_anonymous_file(presenter->mapping_size);
    if (fd < 0) {
        vgfx_wayland_shm_close(presenter);
        vgfx_wayland_shm_error(error, error_size, "could not allocate anonymous file");
        return 0;
    }
    presenter->mapping =
        mmap(NULL, presenter->mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (presenter->mapping != MAP_FAILED) {
        presenter->pool = shell->connection->api.proxy_marshal_flags(
            shell->connection->shm,
            VGFX_WL_SHM_CREATE_POOL,
            shell->connection->api.shm_pool_interface,
            shell->connection->api.proxy_get_version(shell->connection->shm),
            0,
            NULL,
            fd,
            (int32_t)presenter->mapping_size);
    } else {
        presenter->mapping = NULL;
    }
    close(fd);
    if (!presenter->mapping || !presenter->pool) {
        vgfx_wayland_shm_close(presenter);
        vgfx_wayland_shm_error(error, error_size, "could not create shared-memory pool");
        return 0;
    }

    for (size_t i = 0; i < sizeof(presenter->slots) / sizeof(presenter->slots[0]); ++i) {
        size_t offset = i * buffer_size;
        presenter->slots[i].pixels = (uint8_t *)presenter->mapping + offset;
        presenter->slots[i].presenter = presenter;
        presenter->slots[i].buffer = shell->connection->api.proxy_marshal_flags(
            presenter->pool,
            VGFX_WL_SHM_POOL_CREATE_BUFFER,
            shell->connection->api.buffer_interface,
            vgfx_wayland_shm_proxy_version(presenter, presenter->pool),
            0,
            NULL,
            (int32_t)offset,
            width,
            height,
            stride,
            VGFX_WL_SHM_FORMAT_XRGB8888);
        if (!presenter->slots[i].buffer ||
            shell->connection->api.proxy_add_listener(
                presenter->slots[i].buffer,
                (void (**)(void))(void *)&g_vgfx_wayland_buffer_listener,
                &presenter->slots[i]) != 0) {
            vgfx_wayland_shm_close(presenter);
            vgfx_wayland_shm_error(error, error_size, "could not create presentation buffers");
            return 0;
        }
    }
    return 1;
}

static int vgfx_wayland_shm_drain(vgfx_wayland_shm_presenter_t *presenter) {
    if (!presenter || !presenter->shell || !presenter->queued || presenter->frame_callback)
        return 1;
    vgfx_wayland_shm_slot_t *slot = NULL;
    for (size_t i = 0; i < sizeof(presenter->slots) / sizeof(presenter->slots[0]); ++i) {
        if (!presenter->slots[i].busy) {
            slot = &presenter->slots[i];
            break;
        }
    }
    if (!slot)
        return 1;
    const uint8_t *rgba = presenter->queued_rgba;
    size_t pixel_count = presenter->buffer_size / 4;
    for (size_t i = 0; i < pixel_count; ++i) {
        slot->pixels[i * 4] = rgba[i * 4 + 2];
        slot->pixels[i * 4 + 1] = rgba[i * 4 + 1];
        slot->pixels[i * 4 + 2] = rgba[i * 4];
        slot->pixels[i * 4 + 3] = 0xFF;
    }
    slot->busy = 1;
    vgfx_wayland_connection_t *connection = presenter->shell->connection;
    uint32_t surface_version = connection->api.proxy_get_version(
        (struct wl_proxy *)presenter->surface);
    (void)connection->api.proxy_marshal_flags((struct wl_proxy *)presenter->surface,
                                              VGFX_WL_SURFACE_ATTACH,
                                              NULL,
                                              surface_version,
                                              0,
                                              slot->buffer,
                                              0,
                                              0);
    presenter->frame_callback = connection->api.proxy_marshal_flags(
        (struct wl_proxy *)presenter->surface,
        VGFX_WL_SURFACE_FRAME,
        connection->api.callback_interface,
        surface_version,
        0,
        NULL);
    if (!presenter->frame_callback ||
        connection->api.proxy_add_listener(presenter->frame_callback,
                                           (void (**)(void))(void *)&g_vgfx_wayland_frame_listener,
                                           presenter) != 0) {
        if (presenter->frame_callback)
            connection->api.proxy_destroy(presenter->frame_callback);
        presenter->frame_callback = NULL;
        slot->busy = 0;
        return 0;
    }
    uint32_t damage_opcode = surface_version >= 4 ? VGFX_WL_SURFACE_DAMAGE_BUFFER
                                                  : VGFX_WL_SURFACE_DAMAGE;
    (void)connection->api.proxy_marshal_flags((struct wl_proxy *)presenter->surface,
                                              damage_opcode,
                                              NULL,
                                              surface_version,
                                              0,
                                              0,
                                              0,
                                              presenter->width,
                                              presenter->height);
    (void)connection->api.proxy_marshal_flags((struct wl_proxy *)presenter->surface,
                                              VGFX_WL_SURFACE_COMMIT,
                                              NULL,
                                              surface_version,
                                              0);
    if (connection->api.display_flush(connection->display) < 0 && errno != EAGAIN)
        return 0;
    presenter->queued = 0;
    presenter->queued_rgba = NULL;
    presenter->queued_rgba_size = 0;
    presenter->generation++;
    return 1;
}

int vgfx_wayland_shm_present(vgfx_wayland_shm_presenter_t *presenter,
                             const uint8_t *rgba,
                             size_t rgba_size) {
    if (!presenter || !presenter->shell || !rgba || rgba_size < presenter->buffer_size)
        return 0;
    presenter->queued_rgba = rgba;
    presenter->queued_rgba_size = rgba_size;
    presenter->queued = 1;
    return vgfx_wayland_shm_drain(presenter);
}
