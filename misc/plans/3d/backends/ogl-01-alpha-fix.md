# OGL-01: Fix uAlpha Undeclared Uniform (CRITICAL)

## Context
The GLSL fragment shader uses `uAlpha` on lines 388 and 414 but never declares it as `uniform float uAlpha`. `glGetUniformLocation` returns -1. Alpha is undefined — surfaces are either fully opaque or invisible depending on driver behavior. This is the only critical bug across all backends.

## Current GLSL (broken)
```glsl
// Line 388 (unlit path):
if (uUnlit != 0) { FragColor = vec4(uDiffuseColor.rgb, uAlpha); return; }
// Line 414 (lit path):
FragColor = vec4(result, uAlpha);
```

But `uAlpha` is never declared. The uniform list has:
```glsl
uniform vec4 uDiffuseColor;  // alpha could be in .w
uniform vec4 uSpecularColor;
uniform vec3 uEmissiveColor;
uniform int uUnlit;
// NO: uniform float uAlpha;
```

## Fix
Add the missing declaration:
```glsl
uniform float uAlpha;
```

Place it after `uniform int uUnlit;` in the fragment shader string.

The C-side code already retrieves the location (line 562: `ctx->uAlpha = gl.GetUniformLocation(ctx->program, "uAlpha")`) and sets it (line 667: `gl.Uniform1f(ctx->uAlpha, cmd->alpha)`). The only missing piece is the GLSL declaration.

**This is a one-line fix.**

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — add `uniform float uAlpha;` to fragment shader source string

## Testing
- Transparent object (alpha=0.5) → semi-transparent (was either invisible or opaque)
- Opaque object (alpha=1.0) → solid (no change)
- Alpha=0 → invisible (was random)
