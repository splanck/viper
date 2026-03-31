# Fix 109: OpenGL Backend — Zero Error Checking

## Severity: P1 — High

## Problem

The OpenGL backend (`vgfx3d_backend_opengl.c`) has NO `glGetError()` calls. Every GL
operation is fire-and-forget. Silent failures produce black screens, texture corruption,
or wrong rendering with no diagnostic.

## Prerequisites

**The OpenGL function loader must be extended.** Viper loads GL functions via `dlsym` into
a custom struct (lines 29-253). `glGetError` is NOT currently in the function pointer table.

### Step 1: Add `glGetError` to the GL function table

In the GL function pointer struct (around line 180), add:

```c
typedef GLenum (*PFNGLGETERRORPROC)(void);
```

Add to the `gl_funcs` struct:
```c
PFNGLGETERRORPROC GetError;
```

### Step 2: Load via dlsym

In the GL initialization function where other functions are loaded:
```c
gl.GetError = (PFNGLGETERRORPROC)dlsym(lib, "glGetError");
```

### Step 3: Add GL_CHECK macro

```c
#ifndef NDEBUG
#define GL_CHECK(call) do { \
    call; \
    GLenum _err = gl.GetError ? gl.GetError() : 0; \
    if (_err) fprintf(stderr, "GL error 0x%04X at %s:%d\n", _err, __FILE__, __LINE__); \
} while(0)
#else
#define GL_CHECK(call) (call)
#endif
```

### Step 4: Apply at critical sites (~15 locations)

1. After `glCompileShader` — check `GL_COMPILE_STATUS`
2. After `glLinkProgram` — check `GL_LINK_STATUS`
3. After `glTexImage2D` — verify texture creation
4. After `glBufferData` — verify buffer allocation
5. After `glCheckFramebufferStatus` — verify FBO completeness
6. After first draw call — catch any pipeline setup errors

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | Add function pointer, loader, macro, apply (~35 LOC) |

## Documentation Update

None needed — this is internal error checking, not a user-facing API.

## Test

- All existing tests pass (GL_CHECK is no-op in release builds)
- In debug build: deliberately corrupt a shader string, verify error appears on stderr
- Run on Linux with Mesa — verify no spurious errors in normal operation
