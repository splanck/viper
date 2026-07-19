//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_wayland_egl.cpp
// Purpose: Prove the OpenGL backend can create a native Wayland EGL context.
// Key invariants: A live Wayland window uses EGL rather than GLX or software fallback.
// Ownership/Lifetime: The test destroys the GPU context before its native window.
// Links: src/runtime/graphics/3d/backend/vgfx3d_egl_wayland.c
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "vgfx.h"
extern "C" {
#include "vgfx3d_backend.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    if (!std::getenv("WAYLAND_DISPLAY")) {
        std::puts("Wayland EGL test skipped: WAYLAND_DISPLAY is not set");
        return 0;
    }
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 128;
    params.height = 96;
    params.title = "Zanna Wayland EGL Test";
    params.fps = -1;
    vgfx_window_t window = vgfx_create_window(&params);
    if (!window) {
        std::fprintf(stderr, "window creation failed: %s\n", vgfx_get_last_error());
        return 1;
    }
    const vgfx3d_backend_t *backend = vgfx3d_select_backend();
    if (!backend || std::strcmp(backend->name, "opengl") != 0) {
        std::fprintf(stderr, "expected OpenGL backend, got %s\n", backend ? backend->name : "null");
        vgfx_destroy_window(window);
        return 1;
    }
    void *context = backend->create_ctx(window, params.width, params.height);
    if (!context) {
        std::fputs("native Wayland EGL OpenGL context creation failed\n", stderr);
        vgfx_destroy_window(window);
        return 1;
    }
    vgfx_set_gpu_present(window, 1);
    backend->set_vsync(context, 1);
    backend->clear(context, window, 0.1f, 0.2f, 0.4f);
    backend->present(context);
    backend->clear(context, window, 0.4f, 0.2f, 0.1f);
    backend->present(context);
    if (!vgfx_pump_events(window)) {
        std::fputs("Wayland dispatch failed after EGL presentation\n", stderr);
        backend->destroy_ctx(context);
        vgfx_destroy_window(window);
        return 1;
    }
    backend->destroy_ctx(context);
    vgfx_destroy_window(window);
    std::puts("Native Wayland EGL OpenGL context created successfully");
    return 0;
}
