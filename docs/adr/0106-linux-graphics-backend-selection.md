# ADR 0106: Linux Graphics Backend Selection

Date: 2026-07-16
Status: Accepted

## Context

Linux graphics currently means an X11 build whenever X11 development files are present. Runtime
window creation then assumes `XOpenDisplay(NULL)` succeeds. This excludes display-less services and
Wayland-only sessions without XWayland even though Zanna already has a dependency-free in-memory
graphics backend used by tests. It also conflates graphics capability with one window-system
dependency.

Changing backend selection affects the top-level build configuration, ZannaGFX, ZannaGUI, runtime
graphics capability, packaging dependencies, tests, and user documentation. The repository requires
an ADR for that cross-layer dependency change.

## Decision

Zanna separates graphics capability mode from graphics backend selection:

- `ZANNA_GRAPHICS_MODE=AUTO|REQUIRE|OFF` continues to decide whether graphics is optional, required,
  or disabled.
- `ZANNA_GRAPHICS_BACKEND=AUTO|NATIVE|X11|HEADLESS` selects the implementation.
- `AUTO` selects the native platform backend at configure time. On Linux that is currently X11.
- `NATIVE` requires the platform's canonical windowed backend and fails when unavailable.
- `X11` explicitly requires X11 and is valid only on Linux/Unix hosts.
- `HEADLESS` builds the existing dependency-free in-memory framebuffer backend as the product
  backend. It keeps graphics APIs available but creates no OS windows and consumes no display input.

Linux Wayland sessions use XWayland through the X11 backend when `DISPLAY` is available. Native
Wayland will be added as another implementation behind this selection boundary only when it can be
implemented from repository code using dynamically loaded stable system interfaces, without adding
product dependencies. `AUTO` must not silently choose headless at runtime because that would turn a
misconfigured desktop launch into an apparently successful invisible application. Operators and CI
select `HEADLESS` explicitly.

The mock implementation is renamed in user-facing build output to the headless backend, but its
deterministic event-injection entry points remain test-only conventions. Product callers receive the
normal public ZannaGFX surface.

## Validation Requirements

- Configure and build `zannagfx`, ZannaGUI, and runtime graphics with `HEADLESS` and no X11 lookup.
- Run the ZannaGFX framebuffer, drawing, input, and window tests against the headless implementation.
- Verify `AUTO`, `NATIVE`, and `X11` retain the Linux X11 backend when X11 is available.
- Verify `REQUIRE` plus an unavailable requested backend fails with an actionable diagnostic.
- Verify `OFF` continues to disable graphics regardless of backend selection.
- Keep Linux X11 runtime tests under X11/XWayland and run headless tests without `DISPLAY`.

## Consequences

- Linux servers and CI can build a functional software framebuffer without X11 headers or a display.
- Build metadata can distinguish capability from window-system choice.
- Native Wayland has an explicit integration seam instead of requiring conditionals throughout the
  graphics and runtime layers.
- Headless applications receive a process-local text clipboard and synthetic monitor/input state;
  they must not expect desktop clipboard, native monitor, or physical input integration.

## Alternatives Considered

- **Fall back to headless when `XOpenDisplay` fails.** Rejected because desktop applications would
  continue invisibly and hide deployment errors.
- **Disable graphics when X11 is absent.** Rejected because software rendering and deterministic
  headless operation remain useful capabilities.
- **Add a Wayland client library dependency now.** Rejected by the zero-dependency rule and because
  it would not provide a tested fallback contract.
- **Treat XWayland as native Wayland support.** Rejected as inaccurate; it remains the supported
  compatibility path until a native adapter is implemented.
