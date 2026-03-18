# Viper.Graphics3D — Project Overview

## Project: Viper.Graphics3D

A multi-phase effort to add 3D graphics capabilities to the Viper runtime, starting with a software rasterizer and progressing to GPU-accelerated rendering via Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 Core (Linux).

**Namespace:** `Viper.Graphics3D` (distinct from existing `Viper.Graphics` 2D namespace)

**Runtime types:** Canvas3D, Mesh3D, Camera3D, Material3D, Light3D

**Input handling:** Canvas3D is rendering-only. All input via existing `Viper.Input.Keyboard` / `Viper.Input.Mouse` / `Viper.Input.Pad` modules.

**Phases:**

| # | Scope | Duration | Dependencies |
|---|-------|----------|--------------|
| 1 | Software 3D renderer | 6 weeks | None (uses existing framebuffer + math) |
| 2 | Metal GPU backend (macOS) | 6 weeks | Phase 1 complete |
| 3 | Direct3D 11 GPU backend (Windows) | 6 weeks | Phase 1 complete |
| 4 | OpenGL 3.3 GPU backend (Linux) | 6 weeks | Phase 1 complete |
| 5 | Backend abstraction + advanced features | 4 weeks | Phases 2-4 complete |
| 6 | Comprehensive testing | 3 weeks | All phases |
| 7 | Documentation | 2 weeks | All phases |

Phases 2-4 can run in parallel on different platforms.

**Existing infrastructure leveraged:**
- Vec3, Mat4, Mat3, Quat (complete 3D math in `src/runtime/graphics/`)
- ViperGFX framebuffer + platform backends (`src/lib/graphics/`)
- Pixels image buffer (`src/runtime/graphics/rt_pixels.c`)
- Runtime class registration pipeline (`runtime.def` → rtgen → frontends)

**Key design decisions:**
- Software renderer writes into existing `uint8_t *pixels` RGBA framebuffer
- GPU backends create native surfaces (CAMetalLayer, DXGI swap chain, GLX context) alongside existing 2D presentation
- 2D and 3D can coexist in same window (3D scene + 2D HUD overlay)
- All math stays double precision at API boundary; rasterizer/GPU uses float internally
- Backend selected at runtime: `auto` tries GPU first, falls back to software

**Critical technical constraints verified from codebase:**
- Mat4 is **row-major**, right-multiply with column vectors (`M(mat, r, c) = mat->m[r*4+c]`)
- Mat4 perspective uses **OpenGL NDC convention (Z: [-1,1])** — Z-buffer range is [-1,1], not [0,1]
- Vec3 is **right-handed** (+X right, +Y up, +Z toward viewer)
- Screen space is **Y-down** (top-left origin) — rasterizer must flip Y in NDC→screen transform
- Framebuffer pixel format is **RGBA** (`uint8_t[4]` per pixel: R, G, B, A in byte order)
- Framebuffer stride is always `width * 4` (tightly packed)
- Canvas color `0x00RRGGBB` → internal RGBA via `(color << 8) | 0xFF`
- `VIPER_ENABLE_GRAPHICS` is both a CMake option and a `#ifdef` guard in .c files; stubs compiled when OFF
- No GPU APIs exist anywhere in the codebase currently — this is greenfield
- OBJ loading must be implemented from scratch (existing loaders: BMP, PNG only)

