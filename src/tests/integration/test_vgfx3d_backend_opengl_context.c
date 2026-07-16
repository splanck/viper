//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/integration/test_vgfx3d_backend_opengl_context.c
// Purpose: Live GLX smoke coverage for the OpenGL 3D backend context, targets, and readback.
//
// Key invariants:
//   - A real GL 3.3 core context can compile/link every backend shader.
//   - A cleared frame can be read back without GL/resource errors.
//   - Hosts without an X display or suitable driver report CTest skip (77).
//
// Ownership/Lifetime:
//   - The test owns one ViperGFX window and one backend context and destroys both.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "vgfx.h"
#include "vgfx3d_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void identity(float m[16]) {
    memset(m, 0, 16u * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int main(void) {
    vgfx_window_params_t params = {64, 64, "Viper OpenGL context test", -1, 0, 0};
    vgfx3d_camera_params_t camera;
    uint8_t pixels[64 * 64 * 4];
    vgfx_window_t window;
    void *context;

    if (!getenv("DISPLAY")) {
        fprintf(stderr, "SKIP: DISPLAY is not available\n");
        return 77;
    }
    window = vgfx_create_window(&params);
    if (!window) {
        fprintf(stderr, "SKIP: ViperGFX window unavailable: %s\n", vgfx_get_last_error());
        return 77;
    }
    context = vgfx3d_opengl_backend.create_ctx(window, params.width, params.height);
    if (!context) {
        fprintf(stderr, "SKIP: GL 3.3 core context unavailable\n");
        vgfx_destroy_window(window);
        return 77;
    }

    memset(&camera, 0, sizeof(camera));
    identity(camera.view);
    identity(camera.projection);
    camera.forward[2] = -1.0f;
    camera.znear = 0.1f;
    camera.zfar = 100.0f;
    vgfx3d_opengl_backend.set_gpu_postfx_enabled(context, 1);
    vgfx3d_opengl_backend.clear(context, window, 0.75f, 0.25f, 0.5f);
    vgfx3d_opengl_backend.begin_frame(context, &camera);
    vgfx3d_opengl_backend.end_frame(context);
    vgfx3d_opengl_backend.present(context);
    memset(pixels, 0, sizeof(pixels));
    if (!vgfx3d_opengl_backend.readback_rgba(context, pixels, 64, 64, 64 * 4)) {
        fprintf(stderr, "FAIL: OpenGL frame readback failed\n");
        vgfx3d_opengl_backend.destroy_ctx(context);
        vgfx_destroy_window(window);
        return 1;
    }
    if (pixels[0] < 150 || pixels[1] < 30 || pixels[2] < 80) {
        fprintf(stderr,
                "FAIL: unexpected clear readback %u,%u,%u,%u\n",
                (unsigned)pixels[0],
                (unsigned)pixels[1],
                (unsigned)pixels[2],
                (unsigned)pixels[3]);
        vgfx3d_opengl_backend.destroy_ctx(context);
        vgfx_destroy_window(window);
        return 1;
    }

    vgfx3d_opengl_backend.destroy_ctx(context);
    vgfx_destroy_window(window);
    puts("OpenGL live-context smoke passed");
    return 0;
}
