# Feature 4: User-Facing Shader API (3D)

## Current Reality

The 3D runtime exposes materials and lights but not user-authored shaders. The engine
has four backends:
- software
- Metal
- OpenGL
- D3D11

This plan now assumes a hard requirement: the feature must land with equivalent public
behavior across all four backends. No software-first rollout, and no public API that is
"supported later" on native backends.

## Problem

Without a shader API, developers cannot build:
- toon / cel shading
- hologram / dissolve materials
- stylized unlit materials
- custom vertex deformation and material effects

## Corrected Scope

### New Class

`Viper.Graphics3D.Shader3D`

```text
Shader3D.New(vertexSource, fragmentSource) -> Shader3D
Shader3D.Compile() -> Boolean
Shader3D.LastError -> String

shader.SetFloat(name, value)
shader.SetVec3(name, x, y, z)
shader.SetVec4(name, x, y, z, w)
shader.SetTexture(name, pixels)
shader.SetInt(name, value)

material.SetShader(shader)
material.ClearShader()
```

v1 scope:
- constrained custom vertex and fragment stages
- shared language semantics across all four backends
- fixed engine-owned vertex inputs and material bindings

Not v1:
- arbitrary backend-specific extensions
- compute shaders
- post-processing graph editor

## Implementation

### Phase 1: shared frontend and canonical shader IR (8-10 days)

- Add `src/runtime/graphics/rt_shader3d.c` + `rt_shader3d.h`
- Parse a constrained shader language into a typed canonical IR
- Define one backend-independent contract for:
  - vertex inputs
  - varyings
  - uniforms
  - textures
  - output color
- Lock the v1 feature set to what all four backends can implement identically

This IR is the source of truth. Backends do not invent their own semantics.

### Phase 2: implement all four backends together (14-20 days)

In the same feature branch:
- software backend interprets or JITs the canonical IR
- Metal backend lowers from IR to MSL
- OpenGL backend lowers from IR to GLSL
- D3D11 backend lowers from IR to HLSL

The key rule is parity:
- same uniform packing
- same varying semantics
- same coordinate conventions
- same texture sampling rules
- same error handling for unsupported constructs

The feature is not public until all four backends compile and run the same baseline shaders.

### Phase 3: shared conformance suite and enablement gate (6-10 days)

- Add backend-agnostic shader conformance tests
- Render the same scenes on all four backends
- Compare outputs within documented tolerances
- Include:
  - flat color
  - textured unlit
  - toon shading
  - simple vertex displacement

If any backend fails parity, the feature remains disabled behind an internal or experimental gate.

## Viper-Specific Notes

- `Material3D` already exists and is the correct attachment point for custom shaders
- The current vertex format in `rt_canvas3d_internal.h` is the compatibility contract for v1
- The plan should not depend on currently reserved-but-not-fully-used fields like `env_map` or `reflectivity`
- Backend parity requires a shared test harness, not just four codepaths

## Runtime Registration

Add:
- `Viper.Graphics3D.Shader3D`
- `Material3D.SetShader`
- `Material3D.ClearShader`

Use the current `runtime.def` style.

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_shader3d.c` | New |
| `src/runtime/graphics/rt_shader3d.h` | New |
| `src/runtime/graphics/vgfx3d_backend_sw.c` | Modify |
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Modify |
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | Modify |
| `src/runtime/graphics/vgfx3d_backend_d3d11.c` | Modify |
| `src/il/runtime/runtime.def` | Add `Shader3D` and material hooks |
| `src/tests/runtime/RTShader3DTests.cpp` | New |

## Documentation Updates

- Update `docs/graphics3d-guide.md`
- Update `docs/graphics3d-architecture.md`
- Add a dedicated shader page once the public language is frozen
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- The public feature is all-or-nothing across software, Metal, OpenGL, and D3D11
- No backend-specific public surface in v1
- Build integration belongs in `src/runtime/CMakeLists.txt`
