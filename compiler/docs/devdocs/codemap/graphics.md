# CODEMAP: Graphics Library

Cross-platform software 2D graphics library (`src/lib/graphics/`) and Viper runtime graphics classes (`src/runtime/rt_*.c`).

## Runtime Classes (`src/runtime/`)

The Viper.Graphics.* namespace is implemented by these C runtime files:

| File             | Purpose                                                                   |
|------------------|---------------------------------------------------------------------------|
| `rt_graphics.h`  | Canvas and Color class declarations                                       |
| `rt_graphics.c`  | Canvas drawing, Color utilities (RGB, HSL, lerp, brighten/darken)         |
| `rt_pixels.h`    | Pixels class declaration                                                  |
| `rt_pixels.c`    | Software image buffer, image processing (invert, grayscale, blur, resize) |
| `rt_sprite.h`    | Sprite class declaration                                                  |
| `rt_sprite.c`    | Animated sprite with animation, scaling, collision detection              |
| `rt_tilemap.h`   | Tilemap class declaration                                                 |
| `rt_tilemap.c`   | Tile-based map rendering with tileset support                             |
| `rt_camera.h`    | Camera class declaration                                                  |
| `rt_camera.c`    | 2D viewport camera with zoom, bounds, coordinate transforms               |

## Low-Level C Library

The underlying ViperGFX library provides platform-specific window management and drawing.

## Public API (`include/`)

| File            | Purpose                                             |
|-----------------|-----------------------------------------------------|
| `vgfx.h`        | Complete public API: window, drawing, input, events |
| `vgfx_config.h` | Configuration macros and defaults                   |

## Core Implementation (`src/`)

| File              | Purpose                                                             |
|-------------------|---------------------------------------------------------------------|
| `vgfx.c`          | Platform-agnostic core: window lifecycle, event queue, FPS limiting |
| `vgfx_draw.c`     | Drawing primitives: line, rect, circle (Bresenham, midpoint)        |
| `vgfx_internal.h` | Internal structures and platform backend declarations               |

## Platform Backends (`src/`)

| File                    | Purpose                                |
|-------------------------|----------------------------------------|
| `vgfx_platform_macos.m` | macOS Cocoa backend (fully functional) |
| `vgfx_platform_linux.c` | Linux X11 backend (stub)               |
| `vgfx_platform_win32.c` | Windows Win32 backend (stub)           |
| `vgfx_platform_mock.c`  | Mock backend for deterministic testing |

## Tests (`tests/`)

| File             | Purpose                     |
|------------------|-----------------------------|
| `test_harness.h` | Test harness macros         |
| `vgfx_mock.h`    | Mock backend test helpers   |
| `test_window.c`  | Window lifecycle tests      |
| `test_pixels.c`  | Pixel operation tests       |
| `test_drawing.c` | Drawing primitive tests     |
| `test_input.c`   | Input and event queue tests |

## Examples (`examples/`)

| File           | Purpose                                 |
|----------------|-----------------------------------------|
| `basic_draw.c` | Interactive demo with drawing and input |
| `quick_test.c` | Automated visual test (30 frames)       |
| `api_test.c`   | API validation (all backends)           |

## Documentation

| File                        | Purpose                            |
|-----------------------------|------------------------------------|
| `README.md`                 | Main documentation and quick start |
| `gfxlib.md`                 | Complete specification             |
| `STATUS.md`                 | Implementation status tracker      |
| `INTEGRATION.md`            | CMake integration guide            |
| `MACOS_BACKEND.md`          | macOS backend implementation notes |
| `DRAWING_PRIMITIVES.md`     | Drawing algorithm details          |
| `TEST_INFRASTRUCTURE.md`    | Testing approach                   |
| `docs/VIPER_INTEGRATION.md` | BASIC runtime integration guide    |
