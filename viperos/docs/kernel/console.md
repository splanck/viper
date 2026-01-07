# Console, Graphics Console, and Logging

ViperOS uses two output channels during bring-up:

- **Serial console**: always-on debug output over PL011 UART (reliable, minimal, safe during panics).
- **Graphics console**: a framebuffer-backed text renderer for “boot splash” style output and interactive UX.

This page explains how those pieces fit together, and why they’re wired the way they are.

## Serial console: the bedrock

The serial console is implemented in `kernel/console/serial.cpp` and targets QEMU’s `virt` machine PL011 UART at
`0x09000000`.

Design choices (bring-up oriented):

- **Polling I/O**: `serial::putc()` waits until TX FIFO has space; `serial::getc()` blocks until RX FIFO has data.
- **No allocations, no locks**: serial output is safe to call extremely early and in fatal paths.
- **CRLF normalization**: `serial::puts()` emits `\r\n` when it sees `\n`, so logs render cleanly in typical serial
  terminals.

In practice: if something goes wrong before the framebuffer is ready (or after it is corrupted), serial output is still
expected to work.

Key files:

- `kernel/console/serial.hpp`
- `kernel/console/serial.cpp`

## Framebuffer ownership: `ramfb`

The graphics stack is built around a single “framebuffer provider” module: `kernel/drivers/ramfb.*`.

It supports two modes:

- **External framebuffer**: initialized from UEFI GOP information (`ramfb::init_external(...)`).
- **QEMU RAM framebuffer**: initialized via QEMU plumbing (typically `fw_cfg`) in `ramfb::init(width, height)`.

Once initialized, `ramfb` exposes:

- basic framebuffer metadata (`ramfb::get_info()`)
- raw pixel buffer access (`ramfb::get_framebuffer()`)
- helpers like `ramfb::put_pixel()` and `ramfb::clear()`

The graphics console builds on top of this primitive “put pixels somewhere” contract.

Key files:

- `kernel/drivers/ramfb.hpp`
- `kernel/drivers/ramfb.cpp`
- `kernel/drivers/fwcfg.*` (used for QEMU configuration)

## Graphics console: `gcon`

The graphics console lives in `kernel/console/gcon.*`. It is intentionally "small terminal, not a window system":

- fixed-width font (8x16 base, scaled to 10x20)
- cursor in character-cell coordinates with blinking support
- control characters (`\n`, `\r`, `\t`, backspace)
- ANSI escape sequence support (cursor, colors, clearing)
- line wrapping and scrolling
- scrollback buffer (1000 lines)

### Rendering model

The console renders glyphs into the framebuffer:

- `font::get_glyph(c)` gives the bitmap for ASCII character `c`
- `gcon` expands that bitmap into pixels using the current foreground/background colors

Because this is direct rendering, there’s no retained scene graph: what you draw is what you get.

### Scrolling model

Scrolling is implemented by copying pixel rows upward by one text line and clearing the bottom line. This is simple and
correct, but it’s O(width*height) per scroll.

That trade-off is appropriate for boot logs and low-volume output; it’s not a final “high-throughput console”
implementation.

Key files:

- `kernel/console/gcon.hpp`
- `kernel/console/gcon.cpp`
- `kernel/console/font.*`

## “Logging” in practice: how messages reach both consoles

There is no single unified logging subsystem yet; instead, code often prints directly to serial and optionally mirrors
to the graphics console.

Two common patterns you’ll see:

- Subsystems print to serial only (safe and universal).
- Syscall/debug paths print to serial and also call `gcon::puts()` when available (see `kernel/syscall/dispatch.cpp`,
  `sys_debug_print()`).

This is deliberate during bring-up: it keeps the “print path” minimal and avoids pulling a logger into every subsystem.

## Input feedback loop: timer-driven polling

ViperOS uses polling for early input:

- The timer interrupt handler calls `input::poll()`.
- Input drivers (e.g. virtio input) update internal state.
- Higher-level code can read state/events from the input subsystem.

This is not “console I/O” in the Unix sense yet, but it’s the beginning of interactive UX: the graphics console can show
output while input is polled in the background.

Key files:

- `kernel/arch/aarch64/timer.cpp` (tick handler)
- `kernel/input/*`
- `kernel/drivers/virtio/input.*`

## ANSI escape sequence support

The graphics console now supports a subset of ANSI escape sequences:

| Sequence | Description |
|----------|-------------|
| `ESC[H` / `ESC[n;mH` | Cursor position |
| `ESC[nA/B/C/D` | Cursor movement |
| `ESC[J` / `ESC[K` | Erase display/line |
| `ESC[nm` | Set graphics rendition (colors) |
| `ESC[?25h/l` | Show/hide cursor |

Color codes 30-37, 40-47, 90-97, 100-107 are supported for foreground and background.

## Current limitations

- There's no structured log levels or log sinks; output is "print strings" during bring-up.
- No double-buffering (updates are direct to framebuffer)
- No Unicode support (ASCII 32-126 only)

