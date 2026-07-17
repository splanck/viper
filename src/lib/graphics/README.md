# ZannaGFX - Cross-Platform Software 2D Graphics Library

**Version:** 1.0.0
**Status:** ✅ Phase 1 Complete (macOS)

ZannaGFX is a pure software-rendered, single-window, cross-platform 2D graphics library written in C11. It provides
window management, pixel operations, drawing primitives, and input handling with zero external dependencies.

## Features

- **Pure C11** - No external dependencies (no SDL, GLFW, etc.)
- **Software rendering** - 32-bit RGBA framebuffer with direct pixel access
- **Cross-platform** - Native backends for macOS (Cocoa), Linux (X11), Windows (Win32)
- **Drawing primitives** - Lines, rectangles, circles (outline & filled)
- **Overflow-safe clipping** - Extreme public coordinates and clip rectangles are canonicalized before rasterization
- **Input handling** - Keyboard, mouse, and synchronized event queue
- **FPS limiting** - Configurable frame rate control with queryable frame timing
- **Fully documented** - 164% comment coverage with Doxygen annotations

## Platform Support

| Platform | Status         | Backend                                    |
|----------|----------------|--------------------------------------------|
| macOS    | ✅ **Complete** | Cocoa/AppKit (`src/vgfx_platform_macos.m`) |
| Linux    | ✅ **Complete** | X11 (`src/vgfx_platform_linux.c`)          |
| Windows  | ✅ **Complete** | Win32 GDI (`src/vgfx_platform_win32.c`)    |
| Testing  | ✅ Complete     | Mock backend (`src/vgfx_platform_mock.c`)  |

## Building Standalone

From the **Zanna repository root**, configure and build as a standalone project:

```bash
# Configure from repository root
cmake -S src/lib/graphics -B build-gfx

# Build the library
cmake --build build-gfx

# Run tests (optional)
cd build-gfx
ctest --output-on-failure

# Run examples (macOS only)
./examples/basic_draw
./examples/quick_test
```

### Build Options

```bash
# Disable tests
cmake -S src/lib/graphics -B build-gfx -DVGFX_BUILD_TESTS=OFF

# Disable examples
cmake -S src/lib/graphics -B build-gfx -DVGFX_BUILD_EXAMPLES=OFF

# Build in release mode
cmake -S src/lib/graphics -B build-gfx -DCMAKE_BUILD_TYPE=Release
```

## Building as Part of Zanna

When included via `add_subdirectory()` from a parent CMake project (like Zanna), ZannaGFX automatically integrates:

```cmake
# In parent CMakeLists.txt
add_subdirectory(src/lib/graphics)

# Link against it
target_link_libraries(your_target PRIVATE zannagfx)
```

The library will use the parent project's test and example build settings.

## Library Output

- **Static library**: `libzannagfx.a` (Unix) or `zannagfx.lib` (Windows)
- **Location**: `build-gfx/lib/` (standalone) or `build/lib/` (integrated)
- **Size**: ~139 KB

## Quick Start

### Minimal Example

```c
#include "vgfx.h"

int main(void) {
    // Create window with default parameters
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 800;
    params.height = 600;
    params.title = "Hello ZannaGFX";
    params.fps = 60;
    params.resizable = 0;

    vgfx_window_t win = vgfx_create_window(&params);
    if (!win) {
        fprintf(stderr, "Failed to create window: %s\n", vgfx_get_last_error());
        return 1;
    }

    // Main loop
    int running = 1;
    while (running) {
        // Pump OS events before simulation/rendering when same-frame input matters
        if (!vgfx_pump_events(win)) {
            break;
        }

        // Handle events
        vgfx_event_t event;
        while (vgfx_poll_event(win, &event)) {
            switch (event.type) {
                case VGFX_EVENT_CLOSE:
                    running = 0;
                    break;
                case VGFX_EVENT_KEY_DOWN:
                    if (event.data.key.key == VGFX_KEY_ESCAPE) {
                        running = 0;
                    }
                    break;
                case VGFX_EVENT_RESIZE:
                    // event.data.resize.width/height are physical framebuffer pixels
                    // event.data.resize.logical_width/logical_height match vgfx_get_size()
                    break;
                default:
                    break;
            }
        }

        // Clear screen to black
        vgfx_cls(win, VGFX_BLACK);

        // Draw some primitives
        vgfx_fill_circle(win, 400, 300, 100, VGFX_RED);
        vgfx_circle(win, 400, 300, 105, VGFX_WHITE);
        vgfx_line(win, 0, 0, 800, 600, VGFX_YELLOW);

        // Present framebuffer and handle FPS limiting
        if (!vgfx_update(win)) {
            break;
        }
    }

    vgfx_destroy_window(win);
    return 0;
}
```

Compile and link:

```bash
gcc main.c -o myapp -Iinclude -Llib -lzannagfx -framework Cocoa
```

## API Overview

### Window Management

```c
vgfx_window_t vgfx_create_window(const vgfx_window_params_t* params);
void vgfx_destroy_window(vgfx_window_t window);
int vgfx_update(vgfx_window_t window);  // Present + events + FPS limit
int vgfx_pump_events(vgfx_window_t window);  // Events only, no present
int vgfx_should_close(vgfx_window_t window);
void vgfx_set_default_fps(int32_t fps);
int32_t vgfx_get_default_fps(void);
int32_t vgfx_frame_time_ms(vgfx_window_t window);
```

### Drawing Primitives

```c
void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color);
void vgfx_cls(vgfx_window_t window, vgfx_color_t color);
void vgfx_line(vgfx_window_t window, int32_t x0, int32_t y0, int32_t x1, int32_t y1, vgfx_color_t color);
void vgfx_rect(vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color);
void vgfx_rect_fill(vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t color);
void vgfx_circle(vgfx_window_t window, int32_t cx, int32_t cy, int32_t r, vgfx_color_t color);
void vgfx_circle_fill(vgfx_window_t window, int32_t cx, int32_t cy, int32_t r, vgfx_color_t color);
```

All drawing primitives, including `vgfx_pset`, `vgfx_pset_alpha`, and `vgfx_cls`,
honor the active clip rectangle set by `vgfx_set_clip`. When close prevention is
enabled, the close event is still delivered; only the automatic close-request
flag is suppressed.

### Input Handling

```c
int vgfx_poll_event(vgfx_window_t window, vgfx_event_t* event);
int32_t vgfx_flush_events(vgfx_window_t window);
void vgfx_clear_events(vgfx_window_t window);  // Compatibility wrapper
int32_t vgfx_event_overflow_count(vgfx_window_t window);
int vgfx_key_down(vgfx_window_t window, vgfx_key_t key);
int vgfx_mouse_pos(vgfx_window_t window, int32_t* x, int32_t* y);
int vgfx_mouse_button(vgfx_window_t window, vgfx_mouse_button_t button);
```

The event queue is protected internally so platform callbacks and application
polling cannot corrupt queue state. If the queue fills, older non-close events
may be evicted; `VGFX_EVENT_CLOSE` is preserved and drops are observable through
`vgfx_event_overflow_count`. The overflow counter saturates instead of wrapping
negative after extreme sustained event loss.

Resize events report physical framebuffer dimensions and logical dimensions
after the active coordinate scale. Platform backends reject resize dimensions
that exceed framebuffer safety limits before allocating presentation buffers, so
native resize storms cannot leave the framebuffer and presentation surface with
different committed sizes.

Window creation dimensions are logical units. Backends create a native client
area large enough for the display scale and allocate the framebuffer in physical
pixels. Canvas enables coordinate scaling so drawing, size queries, and input
remain in logical pixels on Retina/HiDPI displays.

`vgfx_mouse_pos` always writes the last known window-relative coordinates, even
when the cursor is outside the client area. In that case it returns `0` and the
coordinates may be negative or greater than/equal to the current window size.
Clipboard text and file-drop paths are normalized as UTF-8 where the platform
backend exposes Unicode paths. File-drop paths must fit in the fixed event
payload; oversized paths are dropped and counted by `vgfx_event_overflow_count`
instead of being truncated into invalid or misleading paths. On X11, clipboard
operations use the focused window when available and fall back to another live
ZannaGFX window, so multi-window apps keep clipboard access after the
last-created window closes.

### Colors

```c
#define VGFX_BLACK    0x000000
#define VGFX_WHITE    0xFFFFFF
#define VGFX_RED      0xFF0000
#define VGFX_GREEN    0x00FF00
#define VGFX_BLUE     0x0000FF
#define VGFX_YELLOW   0xFFFF00
#define VGFX_CYAN     0x00FFFF
#define VGFX_MAGENTA  0xFF00FF

vgfx_color_t vgfx_rgb(uint8_t r, uint8_t g, uint8_t b);
```

## Threading & Limitations

### Threading Model

ZannaGFX is **single-threaded** and **not thread-safe**:

- All ZannaGFX functions must be called from the **main thread**
- The platform backend interacts with OS windowing systems that require main-thread execution (Cocoa, X11, Win32)
- Do **not** call ZannaGFX functions from worker threads or signal handlers
- The internal event queue is synchronized only for backend event production and application polling.

**Correct usage:**

```c
// GOOD: All calls from main thread
int main(void) {
    vgfx_window_t win = vgfx_create_window(&params);
    // ... draw and update from main thread ...
    vgfx_destroy_window(win);
}
```

**Incorrect usage:**

```c
// BAD: Calling from worker thread
void* worker_thread(void* arg) {
    vgfx_window_t win = (vgfx_window_t)arg;
    vgfx_pset(win, 100, 100, VGFX_RED);  // ❌ UNDEFINED BEHAVIOR
}
```

### Current Limitations

1. **Single Window** - Only one window per application
2. **HiDPI Support Is Low-Level** - Backing scale and logical coordinate scaling
   are supported, but applications must opt into coordinate scaling and handle
   any higher-level UI/layout policy themselves
3. **Software Rendering Only** - No GPU acceleration
4. **No Text Rendering** - No built-in font/text support (primitives only)
5. **No Image Loading** - No PNG/JPEG support (raw pixels only)
6. **Platform Support**:
    - ✅ macOS: Full support (Cocoa backend)
    - ✅ Linux: Full support (X11 backend)
    - ✅ Windows: Full support (Win32 GDI backend)

### Performance Characteristics

- **Resolution**: 800×600 achieves 60 FPS sustained
- **Throughput**: ~1000 primitives per frame at 60 FPS
- **Memory**: ~7.3 MB per 800×600 window (framebuffer + overhead)
- **Latency**: <1ms keyboard/mouse input latency

## Testing

ZannaGFX includes comprehensive unit tests using a mock backend for deterministic testing without requiring a display.

### Running Tests

```bash
# From build directory
cd build-gfx
ctest --output-on-failure

# Run specific test
./tests/test_input

# Run with verbose output
ctest -V
```

### Test Suites

| Test Suite       | Tests      | Description                                                                      |
|------------------|------------|----------------------------------------------------------------------------------|
| **test_window**  | T1-T3      | Window lifecycle: create, destroy, size queries                                  |
| **test_pixels**  | T4-T6, T14 | Pixel operations: pset, point, cls, framebuffer access                           |
| **test_drawing** | T7-T13     | Drawing primitives: lines, rectangles, circles (outline & filled)                |
| **test_input**   | T16-T25    | Input handling: keyboard, mouse, queueing, overflow, resize, text, scroll, focus |

**Total: 25 tests, 100% pass rate**

### Mock Backend

Tests use `vgfx_platform_mock.c` which provides:

- **Deterministic time**: Control time progression via `vgfx_mock_set_time_ms()`
- **Event injection**: Synthetic events via `vgfx_mock_inject_key_event()`, etc.
- **No display**: Pure in-memory framebuffer (no window creation)
- **Headless testing**: Run in CI/CD without X11/Cocoa/Win32

Example:

```c
vgfx_window_t win = vgfx_create_window(&params);

// Inject synthetic key press
vgfx_mock_inject_key_event(win, VGFX_KEY_A, 1);
vgfx_update(win);

// Verify key state
assert(vgfx_key_down(win, VGFX_KEY_A) == 1);
```

## Examples

The library includes three example programs demonstrating different use cases.

### Running Examples

```bash
# From build directory
cd build-gfx

# Interactive demo (macOS only)
./examples/basic_draw

# Quick automated test
./examples/quick_test

# API validation test
./examples/api_test
```

### Example Descriptions

#### `basic_draw.c` - Interactive Demo (macOS only)

Full-featured interactive example demonstrating:

- Window creation with resizing enabled
- All drawing primitives (rectangles, circles, lines)
- Event handling (keyboard, mouse, close)
- Real-time input polling
- 60 FPS with smooth animation

**Controls:**

- Close window or press ESC to exit
- Watch real-time rendering

**Output:**

```
ZannaGFX v1.0.0 - Basic Drawing Example
Window created: 640x480
[Window displays with colored shapes]
Close event received
Window destroyed
```

#### `quick_test.c` - Automated Visual Test

Automated test that creates a window, draws a test pattern, and exits after 30 frames. Used for verifying the macOS
backend works correctly.

**Features:**

- Non-interactive (auto-exits)
- Draws test pattern (rectangles, circles, lines)
- Verifies 30 frames at 60 FPS
- Suitable for automated testing

**Output:**

```
ZannaGFX macOS Backend Test
============================

1. Creating window...
   ✓ Window created

2. Drawing test pattern...
   ✓ Test pattern drawn

3. Running 30 frames...
   ✓ 30 frames completed in 0.50s (60 FPS)

SUCCESS: All checks passed
```

#### `api_test.c` - API Validation

Comprehensive API validation that works with **all backends** (including mock):

- Window creation and parameter validation
- Framebuffer access and pixel operations
- Drawing primitive functionality
- Error handling verification

**Features:**

- Platform-agnostic (works with mock backend)
- Tests all public API functions
- Validates return values and error states
- Useful for porting to new platforms

**Output:**

```
=== ZannaGFX API Test ===
Version: 1.0.0

Test 1: Creating window...
PASS: Window created

Test 2: Getting window size...
PASS: Size = 320x240

Test 3: FPS settings...
PASS: FPS = 30

[... more tests ...]

All tests passed!
```

## Directory Structure

```
src/lib/graphics/
├── include/
│   ├── vgfx.h              # Public API (489 lines, fully documented)
│   └── vgfx_config.h       # Configuration macros
├── src/
│   ├── vgfx.c              # Core implementation (888 lines)
│   ├── vgfx_draw.c         # Drawing algorithms (605 lines)
│   ├── vgfx_internal.h     # Internal structures (312 lines)
│   ├── vgfx_platform_macos.m    # macOS backend (760 lines)
│   ├── vgfx_platform_linux.c    # Linux backend (1157 lines)
│   ├── vgfx_platform_win32.c    # Windows backend (1226 lines)
│   └── vgfx_platform_mock.c     # Mock backend for tests (430 lines)
├── tests/
│   ├── test_window.c       # Window tests
│   ├── test_pixels.c       # Pixel tests
│   ├── test_drawing.c      # Drawing tests
│   └── test_input.c        # Input tests
├── examples/
│   ├── api_test.c          # API validation
│   ├── quick_test.c        # Visual test
│   └── basic_draw.c        # Interactive demo
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── gfxlib.md               # Detailed specification
└── *.md                    # Additional documentation
```

## Documentation

### Specification & Implementation

- **[gfxlib.md](gfxlib.md)** - Complete specification and implementation details (164 pages)
- **[STATUS.md](STATUS.md)** - Current implementation status and progress tracker
- **[include/vgfx.h](include/vgfx.h)** - Full API documentation with Doxygen comments (489 lines)

### Integration Guides

- **[INTEGRATION.md](INTEGRATION.md)** - CMake build system integration into Zanna
- **[docs/ZANNA_INTEGRATION.md](docs/ZANNA_INTEGRATION.md)** - Runtime integration guide for BASIC language support
    - BASIC statement mapping (SCREEN, PSET, LINE, CIRCLE, etc.)
    - Runtime architecture and main loop integration
    - Memory management and error handling
    - Performance optimization strategies

### Implementation Details

- **[MACOS_BACKEND.md](MACOS_BACKEND.md)** - macOS Cocoa backend implementation notes
- **[DRAWING_PRIMITIVES.md](DRAWING_PRIMITIVES.md)** - Drawing algorithm details (Bresenham, midpoint circle)
- **[TEST_INFRASTRUCTURE.md](TEST_INFRASTRUCTURE.md)** - Testing approach and mock backend

## Requirements

### Build Requirements

- **C11-compliant compiler** (GCC, Clang, or MSVC)
- **CMake 3.10+**
- **Platform SDK**:
    - macOS: Xcode Command Line Tools (Cocoa framework)
    - Linux: X11 development libraries (planned)
    - Windows: Windows SDK (planned)

### Runtime Requirements

- **macOS 10.10+** (for Cocoa backend)
- No runtime dependencies (statically linked)

## Performance

Software-rendered with the following characteristics:

- **800×600 window**: Sustained 60 FPS
- **Drawing throughput**: ~1000 primitives per frame at 60 FPS
- **Memory**: ~7.3 MB per 800×600 window (framebuffer + overhead)
- **Event latency**: <1ms for keyboard/mouse

## License

ZannaGFX is part of the Zanna project and distributed under the GNU GPL v3.
See [LICENSE](../../../LICENSE) for details.

## Contributing

ZannaGFX is part of the larger Zanna project. For questions or contributions, see the
main [Zanna documentation](../../../docs/README.md).

### Platform Backend Implementation

To implement a new platform backend:

1. Create `src/vgfx_platform_<platform>.c`
2. Implement the platform API functions (see `src/vgfx_internal.h`)
3. Update `CMakeLists.txt` platform detection
4. Test with the mock backend test suite

Required functions:

- `vgfx_platform_init_window()`
- `vgfx_platform_destroy_window()`
- `vgfx_platform_process_events()`
- `vgfx_platform_present()`
- `vgfx_platform_sleep_ms()`
- `vgfx_platform_now_ms()`

Frame limiting depends on `vgfx_platform_sleep_ms()` having millisecond-class
precision; the Windows backend uses waitable timers rather than plain
`Sleep()` to avoid coarse scheduler-tick frame pacing.

See `MACOS_BACKEND.md` for implementation guidance.

## Support

For issues or questions, see the main Zanna project documentation or file an issue in the Zanna repository.
