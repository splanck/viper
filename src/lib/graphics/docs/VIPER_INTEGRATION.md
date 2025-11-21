# ViperGFX Runtime Integration Guide

**Version:** 1.0.0
**Date:** 2025-11-21
**Status:** Planning Document

---

## Table of Contents

1. [Overview](#overview)
2. [Integration Architecture](#integration-architecture)
3. [BASIC Language Mapping](#basic-language-mapping)
4. [Runtime Integration Points](#runtime-integration-points)
5. [Main Loop Architecture](#main-loop-architecture)
6. [Memory Management](#memory-management)
7. [Error Handling](#error-handling)
8. [Performance Considerations](#performance-considerations)
9. [Future Enhancements](#future-enhancements)

---

## Overview

This document describes how ViperGFX integrates into the Viper runtime to provide graphics capabilities for BASIC programs. The integration enables classic BASIC graphics statements like `SCREEN`, `PSET`, `LINE`, `CIRCLE`, and input handling through familiar constructs like `INKEY$` and mouse functions.

### Design Principles

1. **Single Window Model** - One graphics window per BASIC program session
2. **Synchronous Rendering** - Graphics operations complete immediately (software rendering)
3. **Event-Driven Input** - Keyboard and mouse state updated during main loop iteration
4. **Deterministic Behavior** - Software rendering ensures reproducible output
5. **Resource Lifetime** - Window lifecycle tied to BASIC runtime session

---

## Integration Architecture

### Component Layers

```
┌─────────────────────────────────────────────────────┐
│         BASIC Source Code                           │
│  SCREEN 1                                           │
│  PSET (100, 100), 15                                │
│  LINE (0, 0)-(320, 200), 4                          │
│  CIRCLE (160, 100), 50, 2                           │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│      Frontend (src/frontends/basic/)                │
│  - Parse graphics statements                        │
│  - Lower to IL with runtime calls                   │
│  - Type checking and validation                     │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│      IL (Intermediate Language)                     │
│  call @Viper.Graphics.Pset(i32, i32, i32)           │
│  call @Viper.Graphics.Line(i32, i32, i32, i32, i32) │
│  call @Viper.Graphics.Circle(i32, i32, i32, i32)    │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│      Runtime (src/runtime/)                         │
│  - Graphics subsystem (viper_gfx_*)                 │
│  - Window lifecycle management                      │
│  - Coordinate system translation                    │
│  - Color palette mapping                            │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│      ViperGFX (src/lib/graphics/)                   │
│  - vgfx_create_window                               │
│  - vgfx_pset, vgfx_line, vgfx_circle                │
│  - vgfx_update (present + events)                   │
│  - vgfx_key_down, vgfx_mouse_pos                    │
└─────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────┐
│      Platform Backend                               │
│  - macOS: Cocoa/AppKit                              │
│  - Linux: X11 (planned)                             │
│  - Windows: Win32 (planned)                         │
└─────────────────────────────────────────────────────┘
```

### Static Library Dependency

```cmake
# In src/runtime/CMakeLists.txt
target_link_libraries(viper_runtime PRIVATE vipergfx)
target_include_directories(viper_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib/graphics/include
)
```

The runtime links against `libvipergfx.a` as a private dependency, making graphics functionality available to runtime functions without exposing ViperGFX headers to IL or frontend layers.

---

## BASIC Language Mapping

### Graphics Initialization

#### `SCREEN mode [, colorMode] [, activePage] [, visualPage]`

Opens the graphics window and initializes the graphics subsystem.

**BASIC:**
```basic
SCREEN 1    ' 320x200, 4 colors
SCREEN 2    ' 640x200, monochrome
SCREEN 7    ' 320x200, 16 colors
SCREEN 9    ' 640x350, 16 colors
SCREEN 12   ' 640x480, 16 colors
SCREEN 13   ' 320x200, 256 colors
```

**Runtime Function:**
```c
// src/runtime/viper_gfx_screen.c
void Viper_Graphics_Screen(int32_t mode) {
    // Map BASIC screen mode to window dimensions
    int32_t width, height;
    switch (mode) {
        case 1:  width = 320; height = 200; break;
        case 2:  width = 640; height = 200; break;
        case 7:  width = 320; height = 200; break;
        case 9:  width = 640; height = 350; break;
        case 12: width = 640; height = 480; break;
        case 13: width = 320; height = 200; break;
        default:
            Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
            return;
    }

    // Create window if not already open
    if (!g_viper_graphics_window) {
        vgfx_window_params_t params = vgfx_window_params_default();
        params.width = width;
        params.height = height;
        params.title = "Viper BASIC Graphics";
        params.fps = 60;
        params.resizable = 0;

        g_viper_graphics_window = vgfx_create_window(&params);
        if (!g_viper_graphics_window) {
            Viper_Error_Raise(VIPER_ERR_OUT_OF_MEMORY);
            return;
        }

        // Clear to black
        vgfx_cls(g_viper_graphics_window, VGFX_BLACK);
    }

    g_viper_screen_mode = mode;
}
```

### Drawing Primitives

#### `PSET (x, y) [, color]`

Sets a pixel at the specified coordinates.

**BASIC:**
```basic
PSET (100, 100), 15    ' White pixel at (100, 100)
```

**IL:**
```
call @Viper.Graphics.Pset(i32 100, i32 100, i32 15)
```

**Runtime Function:**
```c
void Viper_Graphics_Pset(int32_t x, int32_t y, int32_t color) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    // Translate BASIC color index to RGB
    vgfx_color_t rgb = Viper_Graphics_MapColor(color);
    vgfx_pset(g_viper_graphics_window, x, y, rgb);
}
```

#### `LINE [(x1, y1)]-(x2, y2) [, color] [, BF]`

Draws a line or box between two points.

**BASIC:**
```basic
LINE (0, 0)-(100, 100), 15           ' White diagonal line
LINE (10, 10)-(50, 50), 4, B         ' Red box outline
LINE (10, 10)-(50, 50), 4, BF        ' Red filled box
```

**Runtime Functions:**
```c
void Viper_Graphics_Line(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                         int32_t color) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    vgfx_color_t rgb = Viper_Graphics_MapColor(color);
    vgfx_line(g_viper_graphics_window, x1, y1, x2, y2, rgb);
}

void Viper_Graphics_Box(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                        int32_t color, int filled) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    vgfx_color_t rgb = Viper_Graphics_MapColor(color);
    int32_t w = x2 - x1;
    int32_t h = y2 - y1;

    if (filled) {
        vgfx_fill_rect(g_viper_graphics_window, x1, y1, w, h, rgb);
    } else {
        vgfx_rect(g_viper_graphics_window, x1, y1, w, h, rgb);
    }
}
```

#### `CIRCLE (x, y), radius [, color] [, start, end, aspect]`

Draws a circle or ellipse.

**BASIC:**
```basic
CIRCLE (160, 100), 50, 2              ' Red circle
CIRCLE (160, 100), 50, 2, , , 0.5     ' Red ellipse (aspect ratio 0.5)
```

**Runtime Function:**
```c
void Viper_Graphics_Circle(int32_t cx, int32_t cy, int32_t radius,
                           int32_t color) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    vgfx_color_t rgb = Viper_Graphics_MapColor(color);
    vgfx_circle(g_viper_graphics_window, cx, cy, radius, rgb);
}

// Note: Filled circles use PAINT or CIRCLE with fill parameter
void Viper_Graphics_CircleFilled(int32_t cx, int32_t cy, int32_t radius,
                                 int32_t color) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    vgfx_color_t rgb = Viper_Graphics_MapColor(color);
    vgfx_fill_circle(g_viper_graphics_window, cx, cy, radius, rgb);
}
```

#### `CLS [mode]`

Clears the screen to the background color.

**BASIC:**
```basic
CLS        ' Clear to black
CLS 1      ' Clear graphics viewport only
CLS 2      ' Clear text viewport only
```

**Runtime Function:**
```c
void Viper_Graphics_Cls(int32_t mode) {
    if (!g_viper_graphics_window) {
        // CLS without graphics window just clears text output
        return;
    }

    if (mode == 0 || mode == 1) {
        vgfx_cls(g_viper_graphics_window, g_viper_background_color);
    }
}
```

### Input Handling

#### `INKEY$`

Returns a string representing the last key pressed.

**BASIC:**
```basic
k$ = INKEY$
IF k$ = CHR$(27) THEN END    ' ESC to exit
```

**Runtime Function:**
```c
ViperString* Viper_Input_Inkey(void) {
    if (!g_viper_graphics_window) {
        return Viper_String_Empty();
    }

    // Poll events and update keyboard state
    vgfx_event_t event;
    while (vgfx_poll_event(g_viper_graphics_window, &event)) {
        if (event.type == VGFX_EVENT_KEY_DOWN && !event.data.key.is_repeat) {
            // Convert vgfx_key_t to ASCII/scan code string
            return Viper_Input_KeyToString(event.data.key.key);
        }
    }

    return Viper_String_Empty();
}
```

#### Mouse Functions

**BASIC:**
```basic
x = MOUSEX      ' Get mouse X coordinate
y = MOUSEY      ' Get mouse Y coordinate
b = MOUSEBUTTON ' Get button state (1=left, 2=right, 4=middle)
```

**Runtime Functions:**
```c
int32_t Viper_Input_MouseX(void) {
    if (!g_viper_graphics_window) return 0;

    int32_t x, y;
    vgfx_mouse_pos(g_viper_graphics_window, &x, &y);
    return x;
}

int32_t Viper_Input_MouseY(void) {
    if (!g_viper_graphics_window) return 0;

    int32_t x, y;
    vgfx_mouse_pos(g_viper_graphics_window, &x, &y);
    return y;
}

int32_t Viper_Input_MouseButton(void) {
    if (!g_viper_graphics_window) return 0;

    int32_t buttons = 0;
    if (vgfx_mouse_button(g_viper_graphics_window, VGFX_MOUSE_LEFT))
        buttons |= 1;
    if (vgfx_mouse_button(g_viper_graphics_window, VGFX_MOUSE_RIGHT))
        buttons |= 2;
    if (vgfx_mouse_button(g_viper_graphics_window, VGFX_MOUSE_MIDDLE))
        buttons |= 4;

    return buttons;
}
```

---

## Runtime Integration Points

### Global Window State

The runtime maintains a single global window handle for the graphics subsystem:

```c
// src/runtime/viper_gfx_internal.h

// Global graphics window (NULL if not initialized)
static vgfx_window_t g_viper_graphics_window = NULL;

// Current screen mode (0 = text mode, 1-13 = graphics modes)
static int32_t g_viper_screen_mode = 0;

// Background color for CLS
static vgfx_color_t g_viper_background_color = VGFX_BLACK;

// EGA/VGA color palette (16 colors)
static vgfx_color_t g_viper_palette[16] = {
    0x000000,  // 0: Black
    0x0000AA,  // 1: Blue
    0x00AA00,  // 2: Green
    0x00AAAA,  // 3: Cyan
    0xAA0000,  // 4: Red
    0xAA00AA,  // 5: Magenta
    0xAA5500,  // 6: Brown
    0xAAAAAA,  // 7: Light Gray
    0x555555,  // 8: Dark Gray
    0x5555FF,  // 9: Light Blue
    0x55FF55,  // 10: Light Green
    0x55FFFF,  // 11: Light Cyan
    0xFF5555,  // 12: Light Red
    0xFF55FF,  // 13: Light Magenta
    0xFFFF55,  // 14: Yellow
    0xFFFFFF   // 15: White
};
```

### Initialization

Graphics initialization happens on first `SCREEN` statement:

```c
void Viper_Runtime_Init(void) {
    // ... other runtime init ...
    g_viper_graphics_window = NULL;
    g_viper_screen_mode = 0;
}
```

### Cleanup

Graphics cleanup on program termination:

```c
void Viper_Runtime_Shutdown(void) {
    // Clean up graphics window
    if (g_viper_graphics_window) {
        vgfx_destroy_window(g_viper_graphics_window);
        g_viper_graphics_window = NULL;
    }

    // ... other runtime cleanup ...
}
```

### Color Mapping

Convert BASIC color indices (0-15 for EGA/VGA modes) to 24-bit RGB:

```c
vgfx_color_t Viper_Graphics_MapColor(int32_t basic_color) {
    if (basic_color < 0 || basic_color > 15) {
        return VGFX_BLACK;  // Default to black for invalid colors
    }
    return g_viper_palette[basic_color];
}
```

---

## Main Loop Architecture

### Execution Model

Viper BASIC programs run in an **event-driven main loop** managed by the runtime:

```c
// Simplified main loop in VM or compiled code
void Viper_Execute_Program(ViperProgram* program) {
    Viper_Runtime_Init();

    while (program->running) {
        // Execute one iteration of BASIC code
        Viper_Execute_Statement(program);

        // If graphics window is open, update it
        if (g_viper_graphics_window) {
            // Present framebuffer and process events
            if (!vgfx_update(g_viper_graphics_window)) {
                // Update failed (rare, platform error)
                Viper_Error_Raise(VIPER_ERR_DEVICE_IO);
                break;
            }

            // Check for close event
            vgfx_event_t event;
            while (vgfx_poll_event(g_viper_graphics_window, &event)) {
                if (event.type == VGFX_EVENT_CLOSE) {
                    program->running = 0;
                    break;
                }
                // Handle other events (resize, focus, etc.)
            }
        }
    }

    Viper_Runtime_Shutdown();
}
```

### Frame Rate Control

`vgfx_update()` handles FPS limiting automatically based on the window's configured frame rate:

```c
// Set target FPS (typically 60 for smooth animation)
vgfx_set_fps(g_viper_graphics_window, 60);

// Update will sleep to maintain target FPS
vgfx_update(g_viper_graphics_window);
```

For BASIC programs that want to control timing manually:

```basic
SCREEN 12
vgfx_set_fps(0)    ' Unlimited FPS

DO
    ' Draw frame
    CLS
    CIRCLE (320, 240), 100, 15

    ' Manual delay
    SLEEP 16   ' ~60 FPS (16.67ms)
LOOP
```

### Event Processing

The runtime processes ViperGFX events and translates them to BASIC-compatible input state:

```c
void Viper_Graphics_ProcessEvents(void) {
    if (!g_viper_graphics_window) return;

    vgfx_event_t event;
    while (vgfx_poll_event(g_viper_graphics_window, &event)) {
        switch (event.type) {
            case VGFX_EVENT_KEY_DOWN:
                // Update INKEY$ buffer
                Viper_Input_EnqueueKey(event.data.key.key);
                break;

            case VGFX_EVENT_CLOSE:
                // Set runtime flag to terminate
                Viper_Runtime_RequestShutdown();
                break;

            case VGFX_EVENT_RESIZE:
                // Handle window resize (if resizable)
                Viper_Graphics_OnResize(event.data.resize.width,
                                        event.data.resize.height);
                break;

            default:
                // Ignore other events for now
                break;
        }
    }
}
```

---

## Memory Management

### Window Lifecycle

The graphics window is tied to the runtime session:

```
Program Start → Viper_Runtime_Init()
                g_viper_graphics_window = NULL

SCREEN 1      → Viper_Graphics_Screen(1)
                g_viper_graphics_window = vgfx_create_window(...)

Program Body  → Graphics operations via g_viper_graphics_window

Program End   → Viper_Runtime_Shutdown()
                vgfx_destroy_window(g_viper_graphics_window)
                g_viper_graphics_window = NULL
```

### Framebuffer Ownership

ViperGFX **owns the framebuffer** - the runtime never directly allocates or frees framebuffer memory. All pixel operations go through ViperGFX APIs.

**Direct framebuffer access** (for advanced use cases):

```c
void Viper_Graphics_BulkDraw(uint8_t* pixels, size_t size) {
    if (!g_viper_graphics_window) return;

    vgfx_framebuffer_t fb;
    if (vgfx_get_framebuffer(g_viper_graphics_window, &fb)) {
        // Copy pixels directly to framebuffer
        memcpy(fb.pixels, pixels, size);
    }
}
```

### Error Handling

Graphics errors are mapped to BASIC runtime errors:

```c
vgfx_window_t win = vgfx_create_window(&params);
if (!win) {
    const char* err = vgfx_get_last_error();
    if (strstr(err, "allocation")) {
        Viper_Error_Raise(VIPER_ERR_OUT_OF_MEMORY);
    } else {
        Viper_Error_Raise(VIPER_ERR_DEVICE_IO);
    }
    return;
}
```

---

## Error Handling

### ViperGFX Error States

ViperGFX uses thread-local error storage:

```c
// Check for errors after window creation
vgfx_window_t win = vgfx_create_window(&params);
if (!win) {
    const char* msg = vgfx_get_last_error();
    fprintf(stderr, "Graphics error: %s\n", msg);
    vgfx_clear_error();
}
```

### Mapping to BASIC Errors

| ViperGFX Error | BASIC Error | Description |
|----------------|-------------|-------------|
| `VGFX_ERR_ALLOC` | Out of Memory (7) | Failed to allocate framebuffer |
| `VGFX_ERR_PLATFORM` | Device I/O Error (57) | Platform-specific failure (window creation, etc.) |
| `VGFX_ERR_INVALID_PARAM` | Illegal Function Call (5) | Invalid parameters to graphics function |

### Runtime Error Handling

```c
void Viper_Graphics_SafeCall(void (*fn)(void)) {
    if (!g_viper_graphics_window) {
        Viper_Error_Raise(VIPER_ERR_ILLEGAL_FUNCTION_CALL);
        return;
    }

    fn();

    // Check for ViperGFX errors
    const char* err = vgfx_get_last_error();
    if (err) {
        // Map to appropriate BASIC error
        Viper_Error_Raise(VIPER_ERR_DEVICE_IO);
        vgfx_clear_error();
    }
}
```

---

## Performance Considerations

### Software Rendering Overhead

ViperGFX is **pure software rendering** - all drawing happens on the CPU:

- **800×600 window**: ~7.3 MB framebuffer
- **Sustained 60 FPS**: Achievable for typical BASIC graphics workloads
- **Throughput**: ~1000 primitives per frame at 60 FPS

### Optimization Strategies

1. **Batch Updates**: Draw all primitives, then call `vgfx_update()` once per frame
   ```basic
   ' GOOD: Batch all drawing
   DO
       CLS
       FOR i = 1 TO 100
           CIRCLE (RND * 640, RND * 480), 10, i MOD 16
       NEXT
       ' Single update at end of frame
   LOOP
   ```

2. **Minimize Updates**: Don't call `vgfx_update()` inside tight loops
   ```basic
   ' BAD: Update in loop
   FOR i = 1 TO 1000
       PSET (i, i), 15
       ' vgfx_update() called implicitly - SLOW!
   NEXT

   ' GOOD: Update after loop
   FOR i = 1 TO 1000
       PSET (i, i), 15
   NEXT
   ' Single update after drawing
   ```

3. **Use Filled Primitives**: `vgfx_fill_rect()` and `vgfx_fill_circle()` are optimized
   ```c
   // Filled rectangle uses optimized scanline filling
   vgfx_fill_rect(win, x, y, w, h, color);  // Fast

   // vs. drawing individual lines (slower)
   for (int i = 0; i < h; i++) {
       vgfx_line(win, x, y+i, x+w, y+i, color);
   }
   ```

### FPS Limiting

Configure appropriate FPS based on program needs:

```c
// Animation-heavy programs
vgfx_set_fps(g_viper_graphics_window, 60);

// Slower-paced programs
vgfx_set_fps(g_viper_graphics_window, 30);

// Unlimited FPS (for benchmarking)
vgfx_set_fps(g_viper_graphics_window, 0);
```

---

## Future Enhancements

### Planned Features (Post-v1.0)

1. **PAINT Statement**
   - Flood fill algorithm
   - Pattern fills
   - Boundary detection

2. **Sprite Support**
   - `GET` and `PUT` statements
   - Sprite arrays
   - Transparency/masking

3. **Text Rendering**
   - Bitmap font support
   - `LOCATE` and `PRINT` in graphics mode
   - Multiple fonts

4. **Palette Customization**
   - `PALETTE` statement
   - Custom color definitions
   - Palette animation

5. **Advanced Shapes**
   - Ellipses
   - Arcs
   - Bezier curves
   - Polygons

6. **Screen Paging**
   - Multiple pages (active vs. visual)
   - `PCOPY` for page-to-page copying
   - Double buffering

7. **View Ports**
   - `VIEW` statement
   - Clipping regions
   - Coordinate transformation

### Linux and Windows Support

Complete X11 and Win32 backend implementations:

```
Phase 2: Linux Support
  - X11 event handling
  - X11 window creation
  - XImage rendering

Phase 3: Windows Support
  - Win32 event loop
  - GDI rendering
  - Windows message handling
```

---

## Summary

### Integration Checklist

When integrating ViperGFX into the Viper runtime:

- [ ] Link runtime against `libvipergfx.a`
- [ ] Implement `Viper_Graphics_*` runtime functions
- [ ] Create global window state in runtime
- [ ] Add cleanup in `Viper_Runtime_Shutdown()`
- [ ] Map BASIC colors to RGB palette
- [ ] Integrate `vgfx_update()` into main loop
- [ ] Handle ViperGFX events (close, resize, etc.)
- [ ] Map ViperGFX errors to BASIC error codes
- [ ] Add frontend lowering for graphics statements
- [ ] Test with example BASIC programs

### Key Design Points

✅ **Single Window** - One window per BASIC session
✅ **Synchronous** - All drawing operations complete immediately
✅ **Global State** - Runtime owns window handle
✅ **Error Mapping** - ViperGFX errors → BASIC errors
✅ **Main Loop** - `vgfx_update()` called once per iteration
✅ **Software Only** - No GPU dependencies
✅ **Platform Abstraction** - ViperGFX handles OS details

### Contact

For questions about ViperGFX runtime integration, see:
- `/src/lib/graphics/gfxlib.md` - Complete API specification
- `/src/lib/graphics/README.md` - Library documentation
- `/src/lib/graphics/examples/` - Example programs
