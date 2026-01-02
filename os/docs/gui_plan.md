# ViperOS GUI Desktop Implementation Plan

## Executive Summary

ViperOS has solid foundations for a GUI desktop: dual framebuffer support (ramfb + VirtIO-GPU), VirtIO-input devices, a mature microkernel IPC system, and an established userspace server pattern. This plan outlines the prerequisites, architecture, and implementation phases for building a windowed desktop environment.

---

## Current State Assessment

### What's Already Working

| Component | Status | Location |
|-----------|--------|----------|
| Framebuffer (ramfb) | Stable | `kernel/drivers/ramfb.cpp` |
| VirtIO-GPU 2D | Stable | `kernel/drivers/virtio/gpu.cpp` |
| Graphics console | Stable | `kernel/console/gcon.cpp` |
| Font rendering | Stable | `kernel/console/font.cpp` |
| Keyboard input | Stable | `kernel/drivers/virtio/input.cpp` |
| inputd server | Stable | `user/servers/inputd/` |
| IPC channels | Stable | `kernel/ipc/channel.cpp` |
| Shared memory | Stable | kernel cap system |
| Userspace servers | Stable | blkd, netd, fsd, consoled, inputd |

### What's Missing/Incomplete

| Component | Status | Notes |
|-----------|--------|-------|
| Mouse input | Stubbed | Events polled but ignored in `input.cpp:229` |
| Cursor rendering | Missing | No hardware or software cursor |
| Window surfaces | Missing | CAP_KIND_SURFACE reserved but unused |
| Display compositor | Missing | No window compositing |
| GUI client library | Missing | No libgui for applications |

---

## Architecture Decision: Userspace vs Kernel Service

### Recommendation: **100% Userspace Services**

| Factor | Userspace | Kernel |
|--------|-----------|--------|
| Fault isolation | Crash doesn't take down system | Crash = system crash |
| Development | Easier debugging, faster iteration | Requires kernel rebuild |
| Security | Runs with minimal privileges | Full kernel access |
| Performance | IPC overhead | Direct memory access |
| Modularity | Can restart/upgrade independently | Tightly coupled |
| ViperOS pattern | Matches blkd/netd/fsd pattern | Goes against microkernel design |

**Rationale:**
1. ViperOS is explicitly a microkernel - GUI belongs in userspace
2. Shared memory handles eliminate most IPC overhead for pixel data
3. Existing servers (blkd, netd, fsd) prove the pattern works
4. Fault isolation is critical for desktop stability

### Performance Mitigation

1. **Zero-copy pixel buffers**: Compositor and clients share memory directly
2. **Damage tracking**: Only composite changed regions, not full screen
3. **Batch updates**: Coalesce multiple draw operations into single composite

---

## Proposed GUI Architecture

```
+-------------------------------------------------------------+
|                    Client Applications                       |
|                 (hello.prg, edit.prg, etc.)                 |
+---------------------------+---------------------------------+
                            | libgui API
                            v
+-------------------------------------------------------------+
|                      libgui.a                                |
|  - Window creation    - Event handling                       |
|  - Pixel buffer ops   - Drawing primitives                   |
+---------------------------+---------------------------------+
                            | IPC (channels + shared memory)
                            v
+-------------------------------------------------------------+
|               Display Server (displayd.sys)                  |
|  - Framebuffer access - Surface allocation                   |
|  - Compositing        - Cursor rendering                     |
|  - Window management  - Input routing                        |
+---------------------------+---------------------------------+
                            | VirtIO-GPU / ramfb
                            v
+-------------------------------------------------------------+
|                    Hardware / QEMU                           |
+-------------------------------------------------------------+
```

### Component Responsibilities

#### displayd.sys (Display Server + Window Manager)
- **Owns the framebuffer** via CAP_DEVICE_ACCESS
- **Allocates surfaces** as shared memory regions
- **Composites** all visible surfaces to framebuffer
- **Renders cursor** on top of everything
- **Manages windows** (create, destroy, move, resize, z-order)
- **Routes input** from inputd to focused window
- Registers as `DISPLAY:` assign

#### libgui.a (Client Library)
- **Simple API** for creating windows
- **Event loop** helper for processing input
- **Basic drawing primitives** (fill_rect, draw_line, blit, text)

---

## Phase 0: Prerequisites

### 0.1 Mouse Input Support

**Problem**: Mouse events are detected but ignored in `kernel/drivers/virtio/input.cpp`

**Solution**: Track mouse state and generate events

**File: `kernel/input/input.hpp`** - Add:
```cpp
struct MouseState {
    i32 x;              // Absolute position (clamped to screen)
    i32 y;
    i32 dx;             // Delta since last poll
    i32 dy;
    u8 buttons;         // Bitmask: BIT0=left, BIT1=right, BIT2=middle
};

// New API
MouseState get_mouse_state();
void set_mouse_bounds(u32 width, u32 height);  // Set screen limits
```

### 0.2 Framebuffer Access from Userspace

**Problem**: displayd needs to write pixels to framebuffer, but framebuffer is kernel memory.

**Solution**: Add syscall to map framebuffer into userspace

**New syscall: `SYS_MAP_FRAMEBUFFER` (0x1A0)**
```cpp
// In kernel/syscall/table.cpp
static SyscallResult sys_map_framebuffer(u64, u64, u64, u64, u64, u64) {
    // Verify caller has CAP_DEVICE_ACCESS
    // Map framebuffer physical memory into caller's address space
    // Return: {virt_addr, width, height, stride, format}
}
```

---

## Phase 1: Mouse & Cursor

### Step 1.1: Kernel Mouse Processing
1. Add `MouseState` struct to `kernel/input/input.hpp`
2. Add global `g_mouse` state and screen bounds
3. Modify `EV_REL` handler in `input.cpp` to update mouse state
4. Add `EV_KEY` handling for mouse buttons (BTN_LEFT=0x110, BTN_RIGHT=0x111)
5. Add `get_mouse_state()` and `set_mouse_bounds()` functions
6. Add syscall `SYS_GET_MOUSE_STATE` returning MouseState

### Step 1.2: inputd Mouse Support
1. Add `INP_GET_MOUSE` request type to `input_protocol.hpp`
2. Implement handler in inputd to query kernel mouse state
3. Add mouse event notification path

### Step 1.3: Cursor Rendering (in displayd)
1. Define 16x16 cursor sprite bitmap (arrow pointer)
2. Track cursor position in displayd
3. Before compositing: save pixels under cursor location
4. After compositing: draw cursor sprite
5. On mouse move: restore saved pixels, update position, redraw cursor

---

## Phase 2: Display Server

### Step 2.1: Server Skeleton
Create `user/servers/displayd/main.cpp` following blkd pattern

### Step 2.2: Framebuffer Initialization
Map framebuffer into displayd's address space

### Step 2.3: Surface Allocation via Shared Memory
```cpp
struct Surface {
    u32 id;
    u32 width, height;
    u32 stride;
    u32 shm_handle;     // Shared memory handle
    u32* pixels;        // Mapped pointer in displayd
    i32 x, y;           // Position on screen
    u32 z_order;        // Stacking order
    bool visible;
};
```

### Step 2.4: Basic Compositing
Blit visible surfaces back-to-front, then draw cursor

### Step 2.5: Display Protocol
IPC protocol for client-server communication

---

## Phase 3: Window Management

### Step 3.1: Window State Tracking
```cpp
struct Window {
    u32 id;
    Surface* surface;
    char title[64];
    i32 x, y;
    u32 width, height;
    bool focused;
    u32 z_order;
    i32 event_channel;
};
```

### Step 3.2: Window Decorations (Minimal Style)
- 24px title bar
- 2px border
- Close button

### Step 3.3: Input Routing
Route mouse/keyboard events to appropriate windows

### Step 3.4: Focus Management
Track focused window, send focus events

---

## Phase 4: Client Library (libgui)

### Public API
```c
gui_window_t* gui_create_window(const char* title, uint32_t width, uint32_t height);
void gui_destroy_window(gui_window_t* win);
uint32_t* gui_get_pixels(gui_window_t* win);
void gui_present(gui_window_t* win);
int gui_poll_event(gui_window_t* win, gui_event_t* event);
int gui_wait_event(gui_window_t* win, gui_event_t* event);
void gui_fill_rect(gui_window_t* win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gui_draw_text(gui_window_t* win, uint32_t x, uint32_t y, const char* text, uint32_t color);
```

---

## Phase 5: Desktop Polish

### 5.1 Window Move/Resize via Mouse
Drag title bar to move, drag edges to resize

### 5.2 Alt+Tab Window Switching
Cycle focus through windows

---

## Key Files Summary

| Phase | New Files | Modified Files |
|-------|-----------|----------------|
| 0 | - | `kernel/drivers/virtio/input.cpp`, `kernel/input/input.hpp` |
| 0 | - | `kernel/syscall/table.cpp` (add SYS_MAP_FRAMEBUFFER) |
| 1 | - | `user/servers/inputd/main.cpp`, `input_protocol.hpp` |
| 2 | `user/servers/displayd/` | `user/CMakeLists.txt`, `user/vinit/vinit.cpp` |
| 3 | - | `user/servers/displayd/` |
| 4 | `user/libgui/` | `user/CMakeLists.txt` |
| 5 | `user/programs/hello_gui.c` | - |

---

## Testing Strategy

| Phase | Test | Description |
|-------|------|-------------|
| 0 | `mousetest.prg` | Print mouse coords to console |
| 1 | Visual | Cursor visible and moves with mouse |
| 2 | `displayd_smoke` | Surface creation, basic blit |
| 3 | Manual | Create window, move/resize, close |
| 4 | `hello_gui.prg` | Full client using libgui |
| 5 | Manual | Alt+Tab, title bar buttons |

---

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Resolution | Dynamic | Query VirtIO-GPU `get_display_info()` at runtime |
| Window style | Minimal | Thin border + small title bar, modern aesthetic |
| Widget toolkit | Raw pixels only | Drawing primitives, defer widgets to later |
| Desktop metaphor | Simple launcher | vinit shell launches GUI apps |

---

## Why Userspace?

The GUI should be **100% userspace** because:

1. **Microkernel philosophy**: ViperOS is explicitly a microkernel - all services belong in userspace
2. **Fault isolation**: A crashed display server doesn't crash the kernel
3. **Proven pattern**: blkd, netd, fsd already demonstrate the server model works
4. **Zero-copy performance**: Shared memory handles eliminate IPC overhead for pixels
5. **Easier development**: Can restart displayd without rebooting
6. **Security**: Display server runs with minimal privileges (only CAP_DEVICE_ACCESS for framebuffer)
