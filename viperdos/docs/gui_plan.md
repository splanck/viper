# ViperDOS GUI Desktop Implementation Plan

## Executive Summary

ViperDOS has solid foundations for a GUI desktop: dual framebuffer support (ramfb + VirtIO-GPU), VirtIO-input devices, a
mature IPC system, and an established display server pattern. This plan outlines the prerequisites, architecture, and
implementation phases for building a windowed desktop environment.

> **Implementation Status (v0.3.1):** Core GUI infrastructure is now implemented. displayd server provides window
> compositing, libgui provides client API, and hello_gui demonstrates the system. Mouse event delivery to windows is the
> main remaining work.

---

## Current State Assessment

### What's Already Working

| Component           | Status | Location                          |
|---------------------|--------|-----------------------------------|
| Framebuffer (ramfb) | Stable | `kernel/drivers/ramfb.cpp`        |
| VirtIO-GPU 2D       | Stable | `kernel/drivers/virtio/gpu.cpp`   |
| Graphics console    | Stable | `kernel/console/gcon.cpp`         |
| Font rendering      | Stable | `kernel/console/font.cpp`         |
| Keyboard input      | Stable | `kernel/drivers/virtio/input.cpp` |
| IPC channels        | Stable | `kernel/ipc/channel.cpp`          |
| Shared memory       | Stable | kernel cap system                 |
| Display servers     | Stable | consoled, displayd                |

### What's Implemented (v0.3.1)

| Component          | Status          | Notes                                      |
|--------------------|-----------------|--------------------------------------------|
| Mouse input        | **Implemented** | Events detected via kernel VirtIO-input    |
| Cursor rendering   | **Implemented** | Software cursor in displayd (16x16 arrow)  |
| Window surfaces    | **Implemented** | Shared memory pixel buffers                |
| Display compositor | **Implemented** | displayd server with window compositing    |
| GUI client library | **Implemented** | libgui provides create/destroy/present API |

### What's Still Missing

| Component               | Status      | Notes                                               |
|-------------------------|-------------|-----------------------------------------------------|
| Mouse events to windows | In Progress | Events detected but not delivered to focused window |
| Window move/resize      | Missing     | Title bar drag, edge resize not implemented         |
| Alt+Tab switching       | Missing     | No keyboard-based window switching                  |

---

## Architecture Decision: Userspace vs Kernel Service

### Recommendation: **100% Userspace Services**

| Factor           | Userspace                          | Kernel                     |
|------------------|------------------------------------|----------------------------|
| Fault isolation  | Crash doesn't take down system     | Crash = system crash       |
| Development      | Easier debugging, faster iteration | Requires kernel rebuild    |
| Security         | Runs with minimal privileges       | Full kernel access         |
| Performance      | IPC overhead                       | Direct memory access       |
| Modularity       | Can restart/upgrade independently  | Tightly coupled            |
| ViperDOS pattern | Matches display server pattern     | Goes against server design |

**Rationale:**

1. GUI belongs in userspace for fault isolation
2. Shared memory handles eliminate most IPC overhead for pixel data
3. Display servers (consoled, displayd) prove the pattern works
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
- **Routes input** from kernel to focused window
- Registers as `DISPLAY:` assign

#### libgui.a (Client Library)

- **Simple API** for creating windows
- **Event loop** helper for processing input
- **Basic drawing primitives** (fill_rect, draw_line, blit, text)

---

## Phase 0: Prerequisites - COMPLETED

### 0.1 Mouse Input Support - DONE

Mouse input is now working via kernel VirtIO-input driver.

### 0.2 Framebuffer Access from Userspace - DONE

displayd uses `SYS_MAP_DEVICE` to map the framebuffer into its address space.

---

## Phase 1: Mouse & Cursor - COMPLETED

### Step 1.1-1.3: All Done

- Mouse state is tracked via kernel input driver
- 16x16 cursor sprite is rendered by displayd
- Cursor position is tracked and updated on mouse movement
- Background save/restore is implemented for cursor rendering

---

## Phase 2: Display Server - COMPLETED

### Step 2.1-2.5: All Done

- `user/servers/displayd/` implements the display server
- Framebuffer is mapped via `SYS_MAP_DEVICE`
- Surfaces use shared memory for zero-copy pixel buffers
- Back-to-front compositing with cursor on top
- Display protocol defined in `display_protocol.hpp`

---

## Phase 3: Window Management - PARTIAL

### Step 3.1-3.2: Done

- Window state tracking implemented in displayd
- Window decorations with title bar, border, close button

### Step 3.3-3.4: In Progress

- Focus tracking is implemented
- **TODO:** Mouse click events not yet delivered to windows
- **TODO:** Keyboard events not yet routed to focused window

---

## Phase 4: Client Library (libgui) - COMPLETED

### Public API - Implemented in `user/libgui/`

```c
gui_window_t* gui_create_window(const char* title, uint32_t width, uint32_t height);
void gui_destroy_window(gui_window_t* win);
uint32_t* gui_get_pixels(gui_window_t* win);
void gui_present(gui_window_t* win);
int gui_poll_event(gui_window_t* win, gui_event_t* event);
void gui_fill_rect(gui_window_t* win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
```

Demo application: `user/hello_gui/` demonstrates window creation and pixel drawing.

---

## Phase 5: Desktop Polish - TODO

### 5.1 Window Move/Resize via Mouse

Drag title bar to move, drag edges to resize - **Not yet implemented**

### 5.2 Alt+Tab Window Switching

Cycle focus through windows - **Not yet implemented**

---

## Implemented Files

| Component          | Location                            | Status   |
|--------------------|-------------------------------------|----------|
| Display Server     | `user/servers/displayd/`            | Complete |
| GUI Client Library | `user/libgui/`                      | Complete |
| GUI Demo           | `user/hello_gui/`                   | Complete |
| Display Protocol   | `user/include/display_protocol.hpp` | Complete |

---

## Testing

| Test                  | Description                         | Status  |
|-----------------------|-------------------------------------|---------|
| Visual cursor         | Cursor visible and moves with mouse | Working |
| `hello_gui.prg`       | Full client using libgui            | Working |
| Window create/destroy | Create and close windows            | Working |
| Window move/resize    | Drag title bar and edges            | TODO    |
| Alt+Tab               | Keyboard window switching           | TODO    |

---

## Design Decisions

| Decision         | Choice          | Rationale                                        |
|------------------|-----------------|--------------------------------------------------|
| Resolution       | Dynamic         | Query VirtIO-GPU `get_display_info()` at runtime |
| Window style     | Minimal         | Thin border + small title bar, modern aesthetic  |
| Widget toolkit   | Raw pixels only | Drawing primitives, defer widgets to later       |
| Desktop metaphor | Simple launcher | vinit shell launches GUI apps                    |

---

## Why Userspace Display Servers?

The GUI should be **userspace** because:

1. **Fault isolation**: A crashed display server doesn't crash the kernel
2. **Proven pattern**: consoled and displayd demonstrate the server model works
3. **Zero-copy performance**: Shared memory handles eliminate IPC overhead for pixels
4. **Easier development**: Can restart displayd without rebooting
5. **Security**: Display server runs with minimal privileges (only CAP_DEVICE_ACCESS for framebuffer)
