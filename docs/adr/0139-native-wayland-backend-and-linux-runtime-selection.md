---
status: active
audience: contributors
last-verified: 2026-07-18
---

# ADR 0139: Native Wayland Backend and Linux Runtime Selection

Date: 2026-07-18
Status: Accepted

## Context

Zanna's windowed Linux graphics implementation is X11. It works on many Wayland desktops through
XWayland, but it cannot create a visible window in a Wayland-only session. The Linux graphics
backend also leaks X11 types and behavior into runtime input, accessibility preferences, OpenGL,
packaging, and the public native-handle documentation.

ADR 0112 separated graphics capability from backend selection and reserved a native-Wayland seam.
This ADR defines the complete contract for filling that seam without adding a bundled product
dependency. It also records the unavoidable differences between X11 and the security-oriented
Wayland window-management model.

## Decision

### Backend selection

`ZANNA_GRAPHICS_BACKEND` accepts `AUTO`, `NATIVE`, `WAYLAND`, `X11`, and `HEADLESS`.

- `WAYLAND` builds native Wayland support and requires it at runtime. Failure to load the client
  interface or connect to the compositor reports `Wayland backend unavailable: <reason>`.
- `X11` builds and requires the X11 backend.
- `HEADLESS` builds the dependency-free in-memory backend and never connects to a compositor.
- `NATIVE` means Cocoa on macOS, Win32 on Windows, and Wayland on Linux.
- `AUTO` builds every available windowed adapter for the host. On Linux it tries Wayland when
  `WAYLAND_DISPLAY` names a usable compositor, then X11 when `DISPLAY` names a usable server. It
  never silently selects headless.

An explicit backend never falls back to another backend. `AUTO` may fall back from an unavailable
Wayland connection to X11, but a successfully connected backend does not change for the lifetime
of a window. All windows in a process use the same selected Linux display backend.

Normal Linux packages include both adapters. Build-time absence of X11 development files may omit
the X11 adapter in `AUTO`, while Wayland client entry points are resolved from stable system shared
libraries at runtime. Zanna checks in the protocol descriptions or generated bindings it consumes;
the product build does not download packages or generate sources with a host tool.

### Native handles and capabilities

The public graphics API exposes a typed native-handle descriptor containing a backend discriminator
and backend-specific display and surface values. The legacy native-view and native-display queries
remain compatibility wrappers. Consumers must check the discriminator instead of assuming that a
Linux display is X11.

Backend capabilities are queryable. Operations forbidden or unavailable under standard Wayland
have these defined results:

- Global window position is unavailable; position queries return `(0, 0)` and report no position
  capability, and position setters do nothing.
- Unsolicited focus and foreground requests do nothing. User-authorized activation uses
  `xdg_activation_v1` when advertised.
- Cursor warping does nothing. Relative mouse uses relative-pointer and pointer-constraints when
  advertised and otherwise reports unsupported.
- Optional protocols improve behavior but their absence never causes a core window to fail after
  the required globals have been found.

The required native-Wayland surface is `wl_compositor`, `wl_shm`, `wl_seat`, and stable
`xdg_wm_base`. Required-global failures use
`Wayland backend unavailable: compositor is missing <interface>`.

### Rendering and input

The baseline presenter uses release-tracked `wl_shm` buffers and compositor frame callbacks. Buffer
size and stride arithmetic is checked before allocation. OpenGL is binding-neutral: GLX remains the
X11 adapter and EGL supplies the Wayland adapter, with the CPU presenter as the safe fallback.

Keyboard layout and state are decoded through a dynamically loaded system xkbcommon interface.
Pointer, keyboard, touch, clipboard, drag-and-drop, cursor, text input, relative pointer, pointer
constraints, output scaling, fractional scaling, viewport, activation, and decoration protocols are
implemented when their defining globals are advertised. Protocol versions are capped at the newest
version tested by Zanna.

Clipboard and drag-and-drop transfers use nonblocking, bounded file-descriptor I/O. Text accepts
UTF-8 MIME variants and file drops accept `text/uri-list`. Missing text-input support retains basic
keyboard text but reports no native composition capability.

### Desktop integration

Linux preference and accessibility code may not depend on an X11 handle. XSettings remains an X11
adapter; environment and portal-backed preferences are display-neutral. AT-SPI projection is a
Linux desktop capability independent of whether the active compositor protocol is Wayland or X11.

### Feature and configuration policy

No separate feature toggle is added. `ZANNA_GRAPHICS_MODE` remains the capability gate and
`ZANNA_GRAPHICS_BACKEND` is the explicit configuration. Runtime diagnostics include the attempted
backend and preserve the underlying loader, connection, registry, or protocol failure reason.

## Validation Requirements

- Build and run backend-neutral graphics tests with `HEADLESS`.
- Build and run X11 tests under Xvfb and under XWayland.
- Build and run native Wayland tests with `DISPLAY` unset under a headless compositor.
- Verify `AUTO` prefers a usable Wayland compositor and falls back to a usable X11 server.
- Verify `WAYLAND`, `X11`, and `HEADLESS` never cross-fallback.
- Exercise compositor disconnect, initial configure, resize storms, buffer release, output changes,
  integer and fractional scaling, clipboard pipes, URI-list drops, keyboard repeat, composition,
  relative pointer, constraints, cursors, decorations, software presentation, and EGL presentation.
- Run the backend matrix on Weston and manually validate Mutter, KWin, and a wlroots compositor.
- Verify packages contain no unconditional X11 dependency when built Wayland-only and declare every
  required dynamically loaded system interface according to package policy.
- Run the complete platform-policy lint, Linux build, tests, smoke tests, and install audit.

## Consequences

- Wayland-only Linux desktops can run Zanna applications without XWayland.
- X11 remains supported explicitly and as the `AUTO` compatibility fallback.
- Some public operations have capability-based semantics instead of pretending Wayland permits
  global positioning, focus stealing, or cursor warping.
- The Linux binary and test matrix become larger because two window-system adapters are maintained.
- Dynamic protocol and loader code requires strict version, lifetime, bounds, and disconnect tests.

## Alternatives Considered

- **Use XWayland permanently.** Rejected because it excludes Wayland-only sessions and prevents
  native input, scaling, activation, and compositor integration.
- **Link libwayland, xkbcommon, EGL, or libdecor unconditionally.** Rejected by the zero-dependency
  product rule and because it would make optional desktop capabilities hard launch requirements.
- **Implement the Wayland wire protocol and XKB grammar from scratch.** Rejected because stable
  system client interfaces provide the transport and keyboard semantics while dynamic loading keeps
  them optional; duplicating both would add substantial security and compatibility risk.
- **Choose one Linux backend at configure time in all builds.** Rejected for normal packages because
  users commonly move the same installation between native Wayland, XWayland, X11, SSH forwarding,
  and display-less contexts.
- **Silently use headless when desktop connection fails.** Rejected by ADR 0112 because an invisible
  application is not a successful desktop fallback.
