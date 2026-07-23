//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_egl_wayland.c
// Purpose: Dynamically create OpenGL EGL contexts on native Wayland surfaces.
// Key invariants: See vgfx3d_egl_wayland.h.
// Ownership/Lifetime: See vgfx3d_egl_wayland.h.
// Links: src/runtime/graphics/3d/backend/vgfx3d_egl_wayland.h
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_egl_wayland.h"

#include "rt_platform.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
typedef intptr_t EGLAttrib;

enum {
    EGL_FALSE_VALUE = 0,
    EGL_NONE_VALUE = 0x3038,
    EGL_RED_SIZE_VALUE = 0x3024,
    EGL_GREEN_SIZE_VALUE = 0x3023,
    EGL_BLUE_SIZE_VALUE = 0x3022,
    EGL_ALPHA_SIZE_VALUE = 0x3021,
    EGL_DEPTH_SIZE_VALUE = 0x3025,
    EGL_SURFACE_TYPE_VALUE = 0x3033,
    EGL_WINDOW_BIT_VALUE = 0x0004,
    EGL_RENDERABLE_TYPE_VALUE = 0x3040,
    EGL_OPENGL_BIT_VALUE = 0x0008,
    EGL_CONTEXT_MAJOR_VERSION_VALUE = 0x3098,
    EGL_CONTEXT_MINOR_VERSION_KHR_VALUE = 0x30FB,
    EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR_VALUE = 0x30FD,
    EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR_VALUE = 0x0001,
    EGL_OPENGL_API_VALUE = 0x30A2,
    EGL_PLATFORM_WAYLAND_KHR_VALUE = 0x31D8,
};

typedef EGLDisplay (*egl_get_display_fn)(void *native_display);
typedef EGLDisplay (*egl_get_platform_display_fn)(uint32_t, void *, const EGLAttrib *);
typedef EGLBoolean (*egl_initialize_fn)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*egl_terminate_fn)(EGLDisplay);
typedef EGLBoolean (*egl_bind_api_fn)(uint32_t);
typedef EGLBoolean (*egl_choose_config_fn)(EGLDisplay,
                                           const EGLint *,
                                           EGLConfig *,
                                           EGLint,
                                           EGLint *);
typedef EGLContext (*egl_create_context_fn)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*egl_destroy_context_fn)(EGLDisplay, EGLContext);
typedef EGLSurface (*egl_create_window_surface_fn)(EGLDisplay, EGLConfig, void *, const EGLint *);
typedef EGLBoolean (*egl_destroy_surface_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*egl_make_current_fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*egl_swap_buffers_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*egl_swap_interval_fn)(EGLDisplay, EGLint);
typedef void *(*egl_get_proc_address_fn)(const char *);
typedef void *(*wl_egl_window_create_fn)(void *, int, int);
typedef void (*wl_egl_window_destroy_fn)(void *);
typedef void (*wl_egl_window_resize_fn)(void *, int, int, int, int);

typedef struct {
    void *egl_library;
    void *wayland_egl_library;
    egl_get_display_fn get_display;
    egl_get_platform_display_fn get_platform_display;
    egl_initialize_fn initialize;
    egl_terminate_fn terminate;
    egl_bind_api_fn bind_api;
    egl_choose_config_fn choose_config;
    egl_create_context_fn create_context;
    egl_destroy_context_fn destroy_context;
    egl_create_window_surface_fn create_window_surface;
    egl_destroy_surface_fn destroy_surface;
    egl_make_current_fn make_current;
    egl_swap_buffers_fn swap_buffers;
    egl_swap_interval_fn swap_interval;
    egl_get_proc_address_fn get_proc_address;
    wl_egl_window_create_fn window_create;
    wl_egl_window_destroy_fn window_destroy;
    wl_egl_window_resize_fn window_resize;
} vgfx3d_egl_api_t;

struct vgfx3d_egl_wayland {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    void *window;
};

static vgfx3d_egl_api_t g_egl;
static int32_t g_egl_state;
static int32_t g_egl_lock;

static void vgfx3d_egl_lock(void) {
    while (rt_atomic_test_and_set(&g_egl_lock, __ATOMIC_ACQUIRE)) {}
}

static void vgfx3d_egl_unlock(void) { rt_atomic_clear(&g_egl_lock, __ATOMIC_RELEASE); }

static void *vgfx3d_egl_open(const char *primary, const char *fallback) {
    void *library = dlopen(primary, RTLD_LOCAL | RTLD_NOW);
    return library ? library : dlopen(fallback, RTLD_LOCAL | RTLD_NOW);
}

static int vgfx3d_egl_load(void) {
    int32_t state = __atomic_load_n(&g_egl_state, __ATOMIC_ACQUIRE);
    if (state != 0)
        return state > 0;
    vgfx3d_egl_lock();
    state = __atomic_load_n(&g_egl_state, __ATOMIC_RELAXED);
    if (state != 0) {
        int result = state > 0;
        vgfx3d_egl_unlock();
        return result;
    }
    g_egl.egl_library = vgfx3d_egl_open("libEGL.so.1", "libEGL.so");
    g_egl.wayland_egl_library =
        vgfx3d_egl_open("libwayland-egl.so.1", "libwayland-egl.so");
    if (!g_egl.egl_library || !g_egl.wayland_egl_library)
        goto fail;
#define LOAD_EGL(field, symbol)                                                                    \
    g_egl.field = (__typeof__(g_egl.field))dlsym(g_egl.egl_library, symbol);                        \
    if (!g_egl.field)                                                                               \
        goto fail
#define LOAD_WL(field, symbol)                                                                     \
    g_egl.field = (__typeof__(g_egl.field))dlsym(g_egl.wayland_egl_library, symbol);                \
    if (!g_egl.field)                                                                               \
        goto fail
    LOAD_EGL(get_display, "eglGetDisplay");
    LOAD_EGL(initialize, "eglInitialize");
    LOAD_EGL(terminate, "eglTerminate");
    LOAD_EGL(bind_api, "eglBindAPI");
    LOAD_EGL(choose_config, "eglChooseConfig");
    LOAD_EGL(create_context, "eglCreateContext");
    LOAD_EGL(destroy_context, "eglDestroyContext");
    LOAD_EGL(create_window_surface, "eglCreateWindowSurface");
    LOAD_EGL(destroy_surface, "eglDestroySurface");
    LOAD_EGL(make_current, "eglMakeCurrent");
    LOAD_EGL(swap_buffers, "eglSwapBuffers");
    LOAD_EGL(swap_interval, "eglSwapInterval");
    LOAD_EGL(get_proc_address, "eglGetProcAddress");
    LOAD_WL(window_create, "wl_egl_window_create");
    LOAD_WL(window_destroy, "wl_egl_window_destroy");
    LOAD_WL(window_resize, "wl_egl_window_resize");
#undef LOAD_EGL
#undef LOAD_WL
    g_egl.get_platform_display = (egl_get_platform_display_fn)dlsym(
        g_egl.egl_library, "eglGetPlatformDisplay");
    if (!g_egl.get_platform_display)
        g_egl.get_platform_display = (egl_get_platform_display_fn)g_egl.get_proc_address(
            "eglGetPlatformDisplayEXT");
    __atomic_store_n(&g_egl_state, 1, __ATOMIC_RELEASE);
    vgfx3d_egl_unlock();
    return 1;
fail:
    if (g_egl.wayland_egl_library)
        dlclose(g_egl.wayland_egl_library);
    if (g_egl.egl_library)
        dlclose(g_egl.egl_library);
    memset(&g_egl, 0, sizeof(g_egl));
    __atomic_store_n(&g_egl_state, -1, __ATOMIC_RELEASE);
    vgfx3d_egl_unlock();
    return 0;
}

int vgfx3d_egl_wayland_available(void) { return vgfx3d_egl_load(); }

void *vgfx3d_egl_wayland_get_proc(const char *name) {
    if (!name || !vgfx3d_egl_load())
        return NULL;
    void *result = g_egl.get_proc_address(name);
    return result ? result : dlsym(g_egl.egl_library, name);
}

vgfx3d_egl_wayland_t *vgfx3d_egl_wayland_create(void *native_display,
                                                void *native_surface,
                                                int32_t width,
                                                int32_t height) {
    if (!native_display || !native_surface || width <= 0 || height <= 0 || !vgfx3d_egl_load())
        return NULL;
    vgfx3d_egl_wayland_t *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return NULL;
    binding->display = g_egl.get_platform_display
                           ? g_egl.get_platform_display(
                                 EGL_PLATFORM_WAYLAND_KHR_VALUE, native_display, NULL)
                           : g_egl.get_display(native_display);
    EGLint major = 0;
    EGLint minor = 0;
    if (!binding->display || !g_egl.initialize(binding->display, &major, &minor) ||
        !g_egl.bind_api(EGL_OPENGL_API_VALUE))
        goto fail;
    const EGLint config_attributes[] = {EGL_SURFACE_TYPE_VALUE,
                                        EGL_WINDOW_BIT_VALUE,
                                        EGL_RENDERABLE_TYPE_VALUE,
                                        EGL_OPENGL_BIT_VALUE,
                                        EGL_RED_SIZE_VALUE,
                                        8,
                                        EGL_GREEN_SIZE_VALUE,
                                        8,
                                        EGL_BLUE_SIZE_VALUE,
                                        8,
                                        EGL_ALPHA_SIZE_VALUE,
                                        8,
                                        EGL_DEPTH_SIZE_VALUE,
                                        24,
                                        EGL_NONE_VALUE};
    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!g_egl.choose_config(binding->display, config_attributes, &config, 1, &config_count) ||
        config_count < 1 || !config)
        goto fail;
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_VALUE,
                                         3,
                                         EGL_CONTEXT_MINOR_VERSION_KHR_VALUE,
                                         3,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR_VALUE,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR_VALUE,
                                         EGL_NONE_VALUE};
    binding->context =
        g_egl.create_context(binding->display, config, NULL, context_attributes);
    if (!binding->context) {
        const EGLint fallback_attributes[] = {EGL_NONE_VALUE};
        binding->context =
            g_egl.create_context(binding->display, config, NULL, fallback_attributes);
    }
    binding->window = g_egl.window_create(native_surface, width, height);
    if (!binding->context || !binding->window)
        goto fail;
    binding->surface =
        g_egl.create_window_surface(binding->display, config, binding->window, NULL);
    if (!binding->surface || !vgfx3d_egl_wayland_make_current(binding))
        goto fail;
    /* Zanna owns dispatch on this wl_display. A blocking EGL swap interval may dispatch the
     * display internally while Zanna's queue is prepared for reading. Wayland compositors still
     * schedule scanout atomically with interval zero; explicit tearing requires another protocol. */
    (void)g_egl.swap_interval(binding->display, 0);
    return binding;
fail:
    vgfx3d_egl_wayland_destroy(binding);
    return NULL;
}

int vgfx3d_egl_wayland_make_current(vgfx3d_egl_wayland_t *binding) {
    return binding && binding->display && binding->surface && binding->context &&
           g_egl.make_current(
               binding->display, binding->surface, binding->surface, binding->context) !=
               EGL_FALSE_VALUE;
}

int vgfx3d_egl_wayland_swap(vgfx3d_egl_wayland_t *binding) {
    return binding && g_egl.swap_buffers(binding->display, binding->surface) != EGL_FALSE_VALUE;
}

int vgfx3d_egl_wayland_set_swap_interval(vgfx3d_egl_wayland_t *binding, int32_t interval) {
    (void)interval;
    return binding && g_egl.swap_interval(binding->display, 0) != EGL_FALSE_VALUE;
}

void vgfx3d_egl_wayland_resize(vgfx3d_egl_wayland_t *binding,
                               int32_t width,
                               int32_t height) {
    if (binding && binding->window && width > 0 && height > 0)
        g_egl.window_resize(binding->window, width, height, 0, 0);
}

void vgfx3d_egl_wayland_destroy(vgfx3d_egl_wayland_t *binding) {
    if (!binding)
        return;
    if (binding->display)
        (void)g_egl.make_current(binding->display, NULL, NULL, NULL);
    if (binding->surface && binding->display)
        (void)g_egl.destroy_surface(binding->display, binding->surface);
    if (binding->context && binding->display)
        (void)g_egl.destroy_context(binding->display, binding->context);
    if (binding->window)
        g_egl.window_destroy(binding->window);
    if (binding->display)
        (void)g_egl.terminate(binding->display);
    free(binding);
}
