# OGL-12: Shadow Mapping

## Depends On

- OGL-10

## Correction To The Earlier Plan

Canvas3D shadow-pass scheduling already exists in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c). This plan does not need to invent a new shadow replay system. The OpenGL work is implementing the backend's `shadow_begin`, `shadow_draw`, and `shadow_end` hooks and integrating the resulting depth texture into the main shader.

## Backend State

Add to `gl_context_t`:

- shadow framebuffer
- shadow depth texture
- shadow shader program
- cached shadow light VP matrix
- shadow bias
- shadow-active flag

## Shadow Pass

### `shadow_begin()`

- ignore the CPU `depth_buf` argument, as Metal already does
- create or resize a depth-only framebuffer and texture
- bind the shadow FBO
- set the shadow viewport
- clear depth
- use the shadow shader program
- store the light VP matrix

### `shadow_draw()`

Render opaque geometry depth-only into the shadow texture.

Recommended implementation:

- separate shadow vertex shader
- no fragment shader, or the minimal fragment path required by the driver
- reuse the normal vertex format
- use the same model matrix upload convention as the main pass

### `shadow_end()`

- end the shadow pass
- store the bias
- mark shadowing active for later main-pass sampling
- restore state needed by the regular draw path:
  - main framebuffer or RTT framebuffer
  - main viewport
  - main GLSL program

That state restoration is required because the current Canvas3D end-of-frame flow runs the shadow pass before the opaque/transparent replay, not as a separate frame.

## Main Pass Shader Work

Add:

```glsl
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowVP;
uniform float uShadowBias;
uniform int uShadowEnabled;
```

For directional lights, sample the shadow map and attenuate direct light.

Use hardware depth comparison via:

- `GL_TEXTURE_COMPARE_MODE = GL_COMPARE_REF_TO_TEXTURE`
- `GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL`

Recommended shadow texture wrap/filter:

- `GL_LINEAR`
- `GL_CLAMP_TO_BORDER`
- white border color

## Loader And Constant Additions

Load:

- `TexParameterfv`

Add constants for:

- `GL_DEPTH_COMPONENT`
- `GL_TEXTURE_COMPARE_MODE`
- `GL_COMPARE_REF_TO_TEXTURE`
- `GL_TEXTURE_COMPARE_FUNC`
- `GL_CLAMP_TO_BORDER`
- `GL_TEXTURE_BORDER_COLOR`

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Directional lights cast shadows in the main pass
- State is correctly restored after the shadow prepass
- RTT and onscreen rendering both still work after shadowing is enabled
