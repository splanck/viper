# Phase 2: Backend Abstraction

## Goal

Extract the software renderer's internals behind a `vgfx3d_backend_t` vtable dispatch interface. The software renderer becomes the first backend implementation. GPU backends (Phases 3-5) implement the same vtable.

## Prerequisites

- Phase 1 complete (software renderer provides the reference implementation to extract)

## Backend Interface

**`src/lib/graphics/src/vgfx3d_backend.h`**:
```c
typedef struct vgfx3d_backend {
    const char *name;   // "software", "metal", "d3d11", "opengl"

    int  (*init)(vgfx3d_context_t *ctx, struct vgfx_window *win);
    void (*destroy)(vgfx3d_context_t *ctx);

    void (*begin_frame)(vgfx3d_context_t *ctx, const vgfx3d_camera_t *cam);
    void (*submit_draw)(vgfx3d_context_t *ctx, const vgfx3d_draw_cmd_t *cmd);
    void (*end_frame)(vgfx3d_context_t *ctx);

    void (*clear)(vgfx3d_context_t *ctx, float r, float g, float b);
    void (*resize)(vgfx3d_context_t *ctx, int32_t w, int32_t h);

    // Resource management
    void *(*create_mesh_buffer)(vgfx3d_context_t *ctx, const vgfx3d_vertex_t *verts, uint32_t vc,
                                const uint32_t *indices, uint32_t ic);
    void  (*destroy_mesh_buffer)(vgfx3d_context_t *ctx, void *buf);
    void *(*create_texture)(vgfx3d_context_t *ctx, const uint8_t *rgba, int32_t w, int32_t h);
    void  (*destroy_texture)(vgfx3d_context_t *ctx, void *tex);
} vgfx3d_backend_t;
```

## Backend Selection

```c
// Compile-time: which backends are available
#if defined(__APPLE__)
  extern const vgfx3d_backend_t vgfx3d_metal_backend;
#endif
#if defined(_WIN32)
  extern const vgfx3d_backend_t vgfx3d_d3d11_backend;
#endif
#if defined(__linux__)
  extern const vgfx3d_backend_t vgfx3d_opengl_backend;
#endif
extern const vgfx3d_backend_t vgfx3d_software_backend;  // always available

// Runtime selection in vgfx3d_create():
const vgfx3d_backend_t *vgfx3d_select_backend(void) {
    // Try GPU first, fall back to software
    #if defined(__APPLE__)
    if (vgfx3d_metal_backend.init(...) == 0) return &vgfx3d_metal_backend;
    #elif defined(_WIN32)
    if (vgfx3d_d3d11_backend.init(...) == 0) return &vgfx3d_d3d11_backend;
    #elif defined(__linux__)
    if (vgfx3d_opengl_backend.init(...) == 0) return &vgfx3d_opengl_backend;
    #endif
    return &vgfx3d_software_backend;
}
```

## Scope

This phase is strictly the dispatch interface. It does NOT implement GPU backends or advanced rendering features.

Advanced rendering features are covered in their dedicated phases: normal mapping (Phase 9), skybox/cube maps (Phase 11), skeletal animation (Phase 14), particles with billboards (Phase 17), post-processing (Phase 18), etc.

