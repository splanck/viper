# ViperGFX Implementation Status

**Date:** 2026-02-23
**Version:** 1.1.0

## Completed Components

### ✅ Core API (100%)

- **vgfx.h** (420 lines): Complete public API header
    - All types, enums, constants defined
    - 35 function declarations
    - Inline helper functions (`vgfx_rgb`)
    - Color constants (VGFX_BLACK, VGFX_WHITE, etc.)
    - C/C++ compatible

- **vgfx_config.h** (80 lines): Configuration macros
    - All 9 configuration parameters
    - Override-friendly with `#ifndef` guards
    - Well-documented defaults

### ✅ Core Implementation (100%)

- **vgfx.c** (596 lines): Platform-agnostic core
    - Thread-local error handling (C11/_Thread_local + fallbacks)
    - Aligned memory allocation (64-byte alignment)
    - Event queue (lock-free SPSC ring buffer)
    - Window lifecycle management
    - FPS limiting algorithm
    - Input state tracking
    - All 35 public API functions implemented

- **vgfx_internal.h** (108 lines): Internal structures
    - Complete `vgfx_window` structure
    - Platform backend function declarations
    - Event queue helpers

### ✅ Drawing Primitives (100%)

- **vgfx_draw.c** (230 lines): Software rendering
    - ✅ Bresenham line algorithm
    - ✅ Rectangle outline and filled
    - ✅ Midpoint circle algorithm (outline)
    - ✅ Filled circle with scanline optimization
    - ✅ Pixel plotting with bounds checking

### ✅ Platform Backends (macOS Complete!)

- **vgfx_platform_macos.m** (545 lines): macOS/Cocoa backend
    - ✅ Complete NSWindow + NSView implementation
    - ✅ Custom VGFXView subclass with CGImage rendering
    - ✅ Window delegate for resize/close/focus events
    - ✅ Full keyboard event translation (A-Z, 0-9, arrows, Enter, Escape, Space)
    - ✅ Full mouse event translation (move, buttons, drag)
    - ✅ Coordinate system conversion (Cocoa bottom-left → top-left)
    - ✅ Framebuffer reallocation on resize
    - ✅ Timer functions (mach_absolute_time)
    - ✅ TESTED AND WORKING - Graphics display correctly!

- **vgfx_platform_linux.c** (60 lines): Linux/X11 backend
    - Stub implementations with correct signatures
    - Timer functions fully implemented (clock_gettime)

- **vgfx_platform_win32.c** (56 lines): Windows/Win32 backend
    - Stub implementations with correct signatures
    - Timer functions fully implemented (QueryPerformanceCounter)

### ✅ Build System (100%)

- **CMakeLists.txt**: Complete build configuration
    - Platform detection (macOS/Linux/Windows)
    - Static library target
    - Examples and tests configured
    - Proper dependency management

### ✅ Examples & Tests (100%)

- **api_test.c**: Comprehensive API validation
    - 11 tests covering all core functionality
    - ✅ All tests passing
    - Works with stub platform backends

- **quick_test.c**: Automated visual test
    - Creates window, draws test pattern, auto-exits
    - ✅ TESTED ON macOS - Graphics display correctly!
    - Red square, green circle, blue rectangle, yellow lines, magenta circle

- **basic_draw.c**: Interactive example
    - Demonstrates window creation
    - Event handling
    - Drawing primitives
    - FPS limiting
    - ✅ TESTED ON macOS - Fully functional!

## Build Status

```
Library: libvipergfx.a (18 KB)
Exported Functions: 48
Total Lines of Code: 1,613

Compilation: ✅ No warnings
Tests: ✅ All 11 tests pass
C99 Compatible: ✅ Yes
C++ Compatible: ✅ Yes
```

## Remaining Work

### ✅ Platform Backend Implementation - macOS (100%)

#### macOS/Cocoa Backend

- [x] `vgfx_platform_init_window`: Create NSWindow + VGFXView ✅
- [x] `vgfx_platform_destroy_window`: Release Cocoa resources ✅
- [x] `vgfx_platform_process_events`: Process NSEvent queue ✅
- [x] `vgfx_platform_present`: Blit framebuffer to NSView ✅

### 🔲 Platform Backend Implementation - Linux/Windows (0%)

#### Linux/X11 Backend

- [ ] `vgfx_platform_init_window`: Create X11 Window + XImage
- [ ] `vgfx_platform_destroy_window`: Destroy X11 resources
- [ ] `vgfx_platform_process_events`: Process X11 event queue
- [ ] `vgfx_platform_present`: XPutImage to window

#### Windows/Win32 Backend

- [ ] `vgfx_platform_init_window`: CreateWindowEx + DIB section
- [ ] `vgfx_platform_destroy_window`: Destroy HWND and GDI resources
- [ ] `vgfx_platform_process_events`: Process message queue
- [ ] `vgfx_platform_present`: BitBlt/StretchDIBits to window

### 🔲 Additional Features (Optional)

- [ ] Unit tests for drawing algorithms
- [ ] Performance benchmarks
- [ ] Additional examples (bouncing ball, input demo, etc.)
- [ ] Documentation generation (Doxygen)

## Testing

### Current Test Coverage

- ✅ Window creation and destruction
- ✅ FPS configuration
- ✅ Framebuffer access
- ✅ Screen clearing
- ✅ Pixel read/write
- ✅ All drawing primitives
- ✅ Color utilities
- ✅ Input state polling
- ✅ Event queue operations
- ✅ Window updates

### Platform-Specific Testing (Blocked)

- ⏸️ Actual window display (requires platform backends)
- ⏸️ Real event generation (requires platform backends)
- ⏸️ User input handling (requires platform backends)
- ⏸️ Window resize handling (requires platform backends)

## Architecture

```
┌─────────────────────────────────────────────┐
│         Application Code (BASIC/C)          │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│         Public API (vgfx.h)                 │
│  - Window management                        │
│  - Drawing primitives                       │
│  - Event handling                           │
│  - Input polling                            │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│      Core Implementation (vgfx.c)           │
│  - Platform-agnostic logic                  │
│  - Software rendering (vgfx_draw.c)         │
│  - Event queue management                   │
└─────────────────┬───────────────────────────┘
                  │
        ┌─────────┴─────────┐
        │                   │
┌───────▼────────┐  ┌──────▼──────┐  ┌────────▼─────────┐
│  macOS/Cocoa   │  │  Linux/X11  │  │  Windows/Win32   │
│    (Stubs)     │  │   (Stubs)   │  │     (Stubs)      │
└────────────────┘  └─────────────┘  └──────────────────┘
```

## Next Steps

1. **Platform Backend Implementation**: Choose one platform (recommend macOS first since that's the development
   platform) and implement the 4 required functions.

2. **Integration Testing**: Once a platform backend is complete, run the `basic_draw` example to verify window creation
   and display.

3. **Additional Platforms**: Implement remaining platform backends (Linux/X11, Windows/Win32).

4. **Performance Optimization**: Profile drawing operations and optimize hotspots if needed.

## Runtime Layer Improvements (2026-02-23)

The following improvements were made in the Viper runtime layer (`src/runtime/graphics/`):

### Bug Fixes

| ID | Component | Fix |
|----|-----------|-----|
| C-2 | Sprite | Flip + scale: scale now applied to flipped buffer, not original |
| C-3 | Sprite | `SetOrigin` pivot point now correctly applied during draw |
| C-4 | Tilemap | Tileset memory leak on `SetTileset` replacement fixed |
| C-5 | GUI | `g_input_capture_widget` cleared when owning widget is destroyed |
| H-1 | Canvas | `Poll()` now drains all queued events per call (was dropping all but first) |
| H-3 | Pixels | PNG IDAT accumulation: integer overflow guard added |
| H-4 | Pixels | BMP save: 32-bit file-size overflow guard added |
| H-5 | Pixels | PNG stride: overflow guard for wide images added |
| H-6 | Particle | rt_obj leak on calloc failure in emitter constructor fixed |
| H-7 | Tilemap | One-way platform: frame-rate-independent body-top check replaces `bvy*0.017` |
| H-2 | TTF | Composite glyph realloc chain: use temporaries; partial failure no longer leaks |
| M-1 | Sprite | `SetFrame` resets animation timer to prevent immediate frame advance |
| M-4 | SpriteSheet | `ensure_cap` uses malloc+memcpy so partial failure can be rolled back |
| M-5 | Glyph cache | `g_cache_tick` promoted to `uint64_t`; no longer wraps after 4B hits |
| M-7 | GUI | Nested modal dialog attempt is now rejected (first dialog stays active) |
| C-1 | Scene | `AddChild` cycle detection added; prevents infinite loop in world-transform update |

### API Additions

| Method | Description |
|--------|-------------|
| `Canvas.SavePng(path)` | Save canvas contents to a PNG file |
| `Canvas.Screenshot()` | (was missing RT_METHOD) capture canvas to Pixels |
| `Canvas.SetClipRect()`, `Canvas.ClearClipRect()` | Clip region control (now accessible from Zia/BASIC) |
| `Canvas.SetTitle()`, `Canvas.Fullscreen()`, `Canvas.Windowed()` | Window management (now accessible) |
| `Canvas.TextWidth()`, `Canvas.TextHeight()` | Text metrics (now accessible) |
| `Canvas.GetFps()`, `Canvas.GetScale()`, `Canvas.SetFps()` | FPS / scale (now accessible) |
| `Canvas.Focus()`, `Canvas.IsFocused()` | Window focus (now accessible) |
| `Color.GetH()`, `Color.GetS()`, `Color.GetL()` | HSL extraction (now accessible) |
| `Pixels.LoadBmp()`, `Pixels.LoadPng()` | Image loading from disk (now accessible) |

### Performance Improvements

| ID | Component | Change |
|----|-----------|--------|
| H-8 | Canvas.CopyRect | Direct framebuffer reads replace per-pixel `vgfx_point()` calls |
| L-3 | Canvas.GradientH/V | Framebuffer direct writes; `memcpy` per row for horizontal gradient |
| L-4 | Canvas.ThickLine | Parallelogram scanline fill + endcaps — O((len+r)·r) vs O(len·r²) |
| L-5 | Pixels.Blur | Separable horizontal+vertical passes — ~10× faster at r=10 |

## Notes

- All core functionality is complete and tested
- Drawing algorithms are correct (Bresenham, midpoint circle)
- Memory management is safe (aligned allocation, bounds checking)
- API is stable and matches the specification (gfxlib.md v1.0.2)
- Thread-local error handling works across C99/C11/C++
- Library is ready for platform backend development
