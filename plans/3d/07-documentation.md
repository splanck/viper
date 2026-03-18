# Phase 7: Documentation

## New Documents

**`docs/graphics3d-guide.md`** (~800 lines)
- Getting started: Canvas3D, Camera3D, your first rotating cube
- Meshes: procedural generation, OBJ loading, vertex manipulation
- Materials: colors, textures, shininess
- Lighting: directional, point, ambient, multiple lights
- Advanced: wireframe, fog, billboards, alpha blending
- Winding order: CCW is front-facing; backface culling discards CW triangles; all mesh generators emit CCW
- Performance tips: triangle budgets, texture sizes, Z-buffer precision
- Full API reference table
- BASIC + Zia code examples for every feature

**`docs/graphics3d-architecture.md`** (~500 lines)
- Software pipeline stages (with diagrams)
- GPU backend architecture (Metal, D3D11, OpenGL)
- Backend selection logic
- Shader architecture (MSL / HLSL / GLSL comparison)
- Resource management (buffer creation, texture upload, cleanup)
- Threading model (main thread only, consistent with ViperGFX)
- Integration with existing 2D system

**`docs/graphics3d-shaders.md`** (~300 lines)
- Vertex shader (all 3 languages side-by-side)
- Fragment shader (all 3 languages side-by-side)
- Uniform buffer layouts
- Texture sampling
- How to modify shaders for new effects

## Updated Documents

| Document | Change |
|----------|--------|
| `docs/README.md` | Add Graphics3D links in Runtime Library section |
| `docs/viperlib.md` | Add Viper.Graphics3D class index (Canvas3D, Mesh3D, Camera3D, Material3D, Light3D) |
| `docs/runtime-api-complete.md` | Add all Graphics3D RT_FUNC entries |
| `docs/graphics-library.md` | Add "3D Graphics" section with cross-reference |
