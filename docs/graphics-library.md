---
status: active
audience: public
last-updated: 2025-11-21
---

# ViperGFX Graphics Library

**Version:** 1.0.0
**Location:** `/src/lib/graphics`
**Status:** ✅ Integrated into Viper build system

---

## Overview

ViperGFX is a cross-platform software 2D graphics library integrated into the Viper project. It provides window management, pixel operations, drawing primitives, and input handling through a simple C API.

### Key Features

- **Pure software rendering** — No GPU dependencies
- **Cross-platform** — macOS (Cocoa), Linux (X11), Windows (Win32)
- **Simple API** — Immediate-mode drawing with direct framebuffer access
- **Event handling** — Keyboard, mouse, and window events
- **FPS limiting** — Built-in frame rate control
- **Test infrastructure** — Mock backend for deterministic unit testing

### Design Principles

- **Zero external dependencies** — No SDL, GLFW, or other libraries
- **Native platform APIs** — Direct Cocoa/X11/Win32 integration
- **Pure C implementation** — C99 standard with C++ compatibility
- **Deterministic rendering** — Integer-only math for predictable results

---

## Platform Support

| Platform | Backend | Status |
|----------|---------|--------|
| **macOS** | Cocoa/AppKit | ✅ **Fully Implemented** |
| **Linux** | X11 | ⏳ Stub (planned) |
| **Windows** | Win32 GDI | ⏳ Stub (planned) |
| **Testing** | Mock backend | ✅ Fully Implemented |

---

## Quick Start

### Building

ViperGFX builds as part of the main Viper build:

```bash
cmake -S . -B build
cmake --build build --target vipergfx
```

Or build standalone:

```bash
cd src/lib/graphics
cmake -S . -B build
cmake --build build
```

### Basic Example

```c
#include "vgfx.h"

int main(void) {
    // Create window
    vgfx_window_params_t params = vgfx_default_window_params();
    params.width = 800;
    params.height = 600;
    params.title = "My Window";
    params.fps = 60;

    vgfx_window_t win = vgfx_create_window(&params);
    if (!win) {
        return 1;
    }

    // Main loop
    while (!vgfx_should_close(win)) {
        // Clear screen
        vgfx_cls(win, VGFX_BLACK);

        // Draw primitives
        vgfx_pset(win, 100, 100, VGFX_WHITE);
        vgfx_line(win, 50, 50, 200, 150, VGFX_RED);
        vgfx_rect(win, 300, 300, 400, 400, VGFX_GREEN);
        vgfx_circle(win, 500, 300, 50, VGFX_BLUE);

        // Handle events
        vgfx_event_t event;
        while (vgfx_poll_event(win, &event)) {
            if (event.type == VGFX_EVENT_KEY_DOWN) {
                if (event.data.key.key == VGFX_KEY_ESCAPE) {
                    break;
                }
            }
        }

        // Update display and limit FPS
        vgfx_update(win);
    }

    vgfx_destroy_window(win);
    return 0;
}
```

---

## API Quick Reference

### Window Management

```c
// Create and destroy
vgfx_window_t vgfx_create_window(const vgfx_window_params_t* params);
void vgfx_destroy_window(vgfx_window_t window);

// Window state
int vgfx_should_close(vgfx_window_t window);
int vgfx_update(vgfx_window_t window);  // Present + process events + FPS limit

// Query window properties
int32_t vgfx_width(vgfx_window_t window);
int32_t vgfx_height(vgfx_window_t window);
uint8_t* vgfx_get_framebuffer(vgfx_window_t window);
```

### Drawing Primitives

```c
// Pixel operations
void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color);
vgfx_color_t vgfx_point(vgfx_window_t window, int32_t x, int32_t y);
void vgfx_cls(vgfx_window_t window, vgfx_color_t color);

// Lines and shapes
void vgfx_line(vgfx_window_t window, int32_t x0, int32_t y0,
               int32_t x1, int32_t y1, vgfx_color_t color);
void vgfx_rect(vgfx_window_t window, int32_t x0, int32_t y0,
               int32_t x1, int32_t y1, vgfx_color_t color);
void vgfx_rect_fill(vgfx_window_t window, int32_t x0, int32_t y0,
                    int32_t x1, int32_t y1, vgfx_color_t color);
void vgfx_circle(vgfx_window_t window, int32_t cx, int32_t cy,
                 int32_t radius, vgfx_color_t color);
void vgfx_circle_fill(vgfx_window_t window, int32_t cx, int32_t cy,
                      int32_t radius, vgfx_color_t color);
```

### Input Handling

```c
// Event polling
int vgfx_poll_event(vgfx_window_t window, vgfx_event_t* event);

// State queries
int vgfx_key_down(vgfx_window_t window, vgfx_key_t key);
void vgfx_get_mouse(vgfx_window_t window, int32_t* x, int32_t* y);
int vgfx_mouse_button_down(vgfx_window_t window, vgfx_mouse_button_t button);
```

### Colors

```c
// Predefined colors
#define VGFX_BLACK       0x000000
#define VGFX_WHITE       0xFFFFFF
#define VGFX_RED         0xFF0000
#define VGFX_GREEN       0x00FF00
#define VGFX_BLUE        0x0000FF
#define VGFX_YELLOW      0xFFFF00
#define VGFX_CYAN        0x00FFFF
#define VGFX_MAGENTA     0xFF00FF

// Color construction
vgfx_color_t vgfx_rgb(uint8_t r, uint8_t g, uint8_t b);
```

### Events

```c
typedef enum {
    VGFX_EVENT_NONE,
    VGFX_EVENT_CLOSE,
    VGFX_EVENT_KEY_DOWN,
    VGFX_EVENT_KEY_UP,
    VGFX_EVENT_MOUSE_DOWN,
    VGFX_EVENT_MOUSE_UP,
    VGFX_EVENT_MOUSE_MOVE,
    VGFX_EVENT_RESIZE,
    VGFX_EVENT_FOCUS_GAINED,
    VGFX_EVENT_FOCUS_LOST
} vgfx_event_type_t;
```

---

## Testing

### Unit Tests

ViperGFX includes comprehensive unit tests using the mock backend:

```bash
# Build tests
cmake --build build --target test_window test_pixels test_drawing test_input

# Run all graphics tests
ctest --test-dir build -R "test_window|test_pixels|test_drawing|test_input"
```

### Test Coverage

- **test_window** — Window lifecycle (T1-T3)
- **test_pixels** — Pixel operations and framebuffer (T4-T6, T14)
- **test_drawing** — Drawing primitives (T7-T13)
- **test_input** — Input and event queue (T16-T21)

All 20 tests pass (100% success rate).

---

## Examples

### Example Programs

Located in `/src/lib/graphics/examples/`:

- **basic_draw.c** — Simple drawing demo
- **quick_test.c** — Minimal functionality test

### Build Examples

```bash
cmake --build build --target basic_draw quick_test
./build/examples/basic_draw
```

---

## Integration with Viper

### Using from CMake

```cmake
# Link against ViperGFX
target_link_libraries(your_target PRIVATE vipergfx)

# Include headers
target_include_directories(your_target PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib/graphics/include
)
```

### Future: BASIC Integration

Graphics support will be added to Viper BASIC with statements like:

```basic
' Planned BASIC graphics support (not yet implemented)
SCREEN 13                      ' Initialize graphics mode
PSET (100, 100), 15            ' Set pixel at (100, 100) to color 15
LINE (0, 0)-(319, 199), 12     ' Draw line from (0,0) to (319,199)
CIRCLE (160, 100), 50, 14      ' Draw circle at center with radius 50
```

This will lower to calls to the ViperGFX runtime functions.

---

## Architecture

### Component Structure

```
src/lib/graphics/
├── include/
│   ├── vgfx.h           # Public API
│   └── vgfx_config.h    # Configuration macros
├── src/
│   ├── vgfx.c           # Core implementation
│   ├── vgfx_draw.c      # Drawing algorithms
│   ├── vgfx_internal.h  # Internal structures
│   ├── vgfx_platform_macos.m    # macOS backend
│   ├── vgfx_platform_linux.c    # Linux backend (stub)
│   ├── vgfx_platform_win32.c    # Windows backend (stub)
│   └── vgfx_platform_mock.c     # Mock backend for tests
├── tests/               # Unit tests
└── examples/            # Example programs
```

### Platform Abstraction

The library uses a platform abstraction layer with function pointers:

```c
// Platform backend functions
int vgfx_platform_init_window(struct vgfx_window* win, const vgfx_window_params_t* params);
void vgfx_platform_destroy_window(struct vgfx_window* win);
int vgfx_platform_process_events(struct vgfx_window* win);
int vgfx_platform_present(struct vgfx_window* win);
void vgfx_platform_sleep_ms(int32_t ms);
int64_t vgfx_platform_now_ms(void);
```

Each platform implements these functions in its backend file.

### Mock Backend

The mock backend (`vgfx_platform_mock.c`) provides:

- **Deterministic time control** — `vgfx_mock_set_time_ms()`
- **Event injection** — `vgfx_mock_inject_key_event()`, etc.
- **No OS dependencies** — Pure in-memory simulation
- **Test automation** — Reproducible test scenarios

---

## Documentation

### Detailed Specification

For complete implementation details, see:
- **[gfxlib.md](../src/lib/graphics/gfxlib.md)** — Full specification
- **[INTEGRATION.md](../src/lib/graphics/INTEGRATION.md)** — Build integration details
- **[STATUS.md](../src/lib/graphics/STATUS.md)** — Implementation status
- **[MACOS_BACKEND.md](../src/lib/graphics/MACOS_BACKEND.md)** — macOS backend notes
- **[DRAWING_PRIMITIVES.md](../src/lib/graphics/DRAWING_PRIMITIVES.md)** — Algorithm details
- **[TEST_INFRASTRUCTURE.md](../src/lib/graphics/TEST_INFRASTRUCTURE.md)** — Testing approach

### API Reference

Full API documentation is available in the header comments:
- **[vgfx.h](../src/lib/graphics/include/vgfx.h)** — Public API with Doxygen comments

---

## Build Configuration

### CMake Options

```cmake
# Standalone build options
option(VGFX_BUILD_TESTS "Build ViperGFX tests" ON)
option(VGFX_BUILD_EXAMPLES "Build ViperGFX examples" ON)
```

When building as part of Viper:
- Tests are controlled by `VIPER_BUILD_TESTING`
- Examples are controlled by `BUILD_EXAMPLES`

### Platform Detection

The build system automatically selects the correct backend:
- **macOS**: Cocoa backend (`vgfx_platform_macos.m`)
- **Linux**: X11 backend (`vgfx_platform_linux.c`)
- **Windows**: Win32 backend (`vgfx_platform_win32.c`)

---

## Future Enhancements

### Planned Features (Out of Scope for v1.0)

- **Text rendering** — Bitmap fonts
- **Image loading** — BMP/PNG support
- **Sprites** — Sprite management and blitting
- **Palette modes** — 8-bit indexed color
- **Multiple windows** — Multi-window support
- **Audio** — Basic sound support

---

## Performance Notes

### Rendering Performance

ViperGFX uses pure software rendering with the following characteristics:

- **Framebuffer format**: 32-bit RGBA (4 bytes per pixel)
- **Drawing algorithms**: Integer-only Bresenham (lines) and midpoint (circles)
- **Memory access**: Direct memory writes (no GPU overhead)
- **FPS limiting**: Voluntary frame rate control via sleep

### Typical Performance

On modern hardware (2020+ Mac):
- **800×600 window**: 60 FPS sustained
- **Full-screen drawing**: ~1000 primitives per frame at 60 FPS
- **Event latency**: <1ms for keyboard/mouse

---

## License

ViperGFX is part of the Viper project and distributed under the GNU GPL v3.
See [LICENSE](../LICENSE) for details.

---

## Summary

ViperGFX provides a simple, deterministic, cross-platform 2D graphics solution for the Viper project:

✅ **Integrated** — Builds as part of Viper
✅ **Tested** — 20/20 tests passing (100%)
✅ **Documented** — Complete API reference and examples
✅ **macOS Ready** — Fully implemented Cocoa backend
⏳ **Portable** — Linux/Windows backends planned

For questions or contributions, see the [main Viper documentation](README.md).
