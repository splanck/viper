# Phase 5: OpenGL 3.3 Core GPU Backend (Linux)

## Goal

GPU-accelerated 3D rendering on Linux using OpenGL 3.3 Core Profile via Mesa.

## Prerequisites

- Phase 2 complete (backend abstraction provides `vgfx3d_backend_t` vtable)

Implements `vgfx3d_opengl_backend` as a `vgfx3d_backend_t`, filling in all vtable function pointers.

## Custom GL Loader

Since GLAD/GLEW are external dependencies, implement a minimal custom loader (~500 LOC):

**`src/lib/graphics/src/vgfx3d_opengl_loader.c`** / `.h`
```c
// Load libGL.so via dlopen
// Resolve glXGetProcAddressARB
// Load all GL 3.3 Core functions (~150 function pointers):
//   glCreateShader, glShaderSource, glCompileShader,
//   glCreateProgram, glAttachShader, glLinkProgram,
//   glGenVertexArrays, glBindVertexArray,
//   glGenBuffers, glBindBuffer, glBufferData,
//   glGenTextures, glBindTexture, glTexImage2D,
//   glDrawElements, glDrawArrays,
//   glEnable, glDisable, glDepthFunc, glBlendFunc,
//   glViewport, glClear, glClearColor, glClearDepth,
//   ... etc
// Return 0 on success, -1 if libGL not found
```

## New Files

**`src/lib/graphics/src/vgfx3d_opengl.c`** (~700 LOC)
- GLX context creation on existing X11 window
- Choose visual with depth buffer + double buffering
- VAO/VBO/IBO management
- Shader compilation (GLSL from C string)
- Uniform buffer objects (UBOs) for camera, lights, material
- Texture upload (glTexImage2D from Pixels RGBA)
- Mipmap generation (glGenerateMipmap)
- MSAA via glRenderbufferStorageMultisample (FBO-based)
- Present via glXSwapBuffers

**`src/lib/graphics/src/vgfx3d_opengl_shaders.c`** (~200 LOC)
```c
// GLSL shaders as C string literals
static const char *vertex_shader_src =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 2) in vec2 aUV;\n"
    "layout(location = 3) in vec4 aColor;\n"
    "\n"
    "uniform mat4 uModelMatrix;\n"
    "uniform mat4 uViewProjection;\n"
    "uniform mat4 uNormalMatrix;\n"
    // ... same logic as Metal/D3D11 versions
    ;

static const char *fragment_shader_src =
    "#version 330 core\n"
    // ... Blinn-Phong shading, same logic
    ;
```

## Platform Integration

**`src/lib/graphics/src/vgfx_platform_linux.c`**:
- When Canvas3D created: create GLX context on existing X11 window
- Choose GLXFBConfig with depth buffer, double buffer, RGBA
- `glXMakeCurrent` before rendering, `glXSwapBuffers` to present
- Existing 2D XImage path continues for regular Canvas

**`src/lib/graphics/CMakeLists.txt`**:
- Add `vgfx3d_opengl.c`, `vgfx3d_opengl_loader.c`, `vgfx3d_opengl_shaders.c` (Linux only)
- Link `-ldl` (for dlopen/dlsym)
- Optional: `find_package(OpenGL)` for build-time check, graceful disable if not found

## Wayland Consideration

- Initial implementation: GLX (X11) only
- Future: EGL backend for Wayland compositors
- XWayland compatibility layer handles most cases today

## Fallback

If libGL.so not found (headless servers, minimal installs):
- `dlopen("libGL.so.1", RTLD_LAZY)` returns NULL → fall back to software

