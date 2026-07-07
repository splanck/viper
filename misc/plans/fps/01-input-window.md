# 01 — Engine: Raw Mouse, Gamepad→Input3D, Fullscreen Creation, DataDir

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track E · **2-session chunk**
> (session A: E1 raw mouse across 4 platform backends; session B: E2/E3/E4 + docs/tests).
> Eliminates constraints: no OS raw-relative mouse (#17), gamepad outside Input3D (#18),
> no fullscreen-at-creation and no per-user data directory (#19).

## 0. TL;DR

Give Viper first-class FPS input: **raw relative mouse mode** (unaccelerated, sub-pixel,
never edge-bound) on Win32/macOS/X11 with the mock backend deterministic; **gamepad merged
into `Input3D`** move/look axes; **fullscreen at window creation** (no windowed flash); and
**`Path.DataDir(app)`** for saves/settings. All additions follow runtime-surface discipline
(runtime.def pairs, completeness check, surface audits, viperlib docs, ADR for the new ABI
entry points).

## 1. Current state (verified anchors)

- Warp-to-center capture **already works**: `rt_canvas3d.c:1816-1824` computes captured-mode
  deltas as offset-from-window-center each poll and calls `rt_mouse_force_delta(dx,dy)`;
  `rt_canvas3d.c:1888-1893` warps the cursor back to center via `vgfx_warp_cursor`. Mouse-move
  events are ignored while captured (`:1842`). This stays as the universal fallback.
- `rt_mouse_capture()` sets a flag + hides the cursor (`rt_input.c:1811-1817`); deltas outside
  capture are absolute-position diffs (`rt_input.c:1396-1402`); `rt_mouse_force_delta` exists
  for synthetic/captured paths (`rt_input.c:1441-1445`).
- `vgfx_warp_cursor` implemented on all three platforms: Win32 `SetCursorPos`
  (`vgfx_platform_win32.c` ~1803), macOS `CGDisplayMoveCursorToPoint` +
  `CGAssociateMouseAndMouseCursorPosition` (`vgfx_platform_macos.m` ~1897), X11 `XWarpPointer`
  (`vgfx_platform_linux.c` ~2648); declared `vgfx.h:818`, dispatched `vgfx.c:1889`.
- Gamepad: `Viper.Input.Pad` is real and cross-platform (`rt_input_pad.c`, IOKit HID / evdev /
  XInput, ~1,661 lines) but `Input3D.MoveAxis()`/`LookAxis()` (`rt_game3d_input.c:167-240`)
  read keyboard+mouse only.
- Fullscreen: `Canvas3D.SetFullscreen/get_IsFullscreen/ToggleFullscreen` exist (added in the
  Ridgebound pass, RUNTIME_API_BUGS.md N4) but only post-creation.
- Paths: `Viper.IO.Path.ExeDir` exists; no per-user data dir helper.

## 2. New API surface (exact, to be registered in runtime.def)

```text
Viper.Input.Mouse.SetRelativeMode(i1)          void(i1)     — enable raw relative deltas
Viper.Input.Mouse.get_RelativeMode             i1()
Viper.Game3D.Input3D.SetRelativeLook(i1)       void(obj,i1) — Input3D convenience (calls the above with CaptureMouse)
Viper.Game3D.Input3D.BindPad(i64)              void(obj,i64) — merge pad N into Move/LookAxis (-1 unbinds)
Viper.Game3D.Input3D.get_PadBound              i64(obj)
Viper.Game3D.Input3D.SetPadLookSensitivity(f64) void(obj,f64)
Viper.Game3D.World3D.NewFullscreen(str)        obj(str)      — fullscreen-at-creation world
Viper.Graphics3D.Canvas3D.NewFullscreen(str)   obj(str)      — canvas variant (desktop resolution)
Viper.IO.Path.DataDir(str)                     str(str)      — per-user writable dir, created on demand
```
Semantics:
- Relative mode ON: OS accumulates raw deltas; `Mouse.DeltaX/Y` and `Input3D.MouseDelta/LookAxis`
  return the accumulated (sub-pixel-scaled ×1000 → i64 for Mouse, f64 for Input3D) motion since
  last poll; absolute `Mouse.X/Y` freezes at capture point; cursor hidden. OFF restores absolute.
  If the platform cannot provide raw motion, the existing warp-to-center path silently serves
  the deltas (mode still reports enabled; `Mouse.get_RelativeModeNative` i1 exposes truth for
  diagnostics).
- Pad merge: left stick → `MoveAxis` (deadzone-shaped, magnitude-preserving), right stick →
  `LookAxis` (per-axis response curve x^1.8, sensitivity scalar), triggers readable via existing
  `Pad` API. Keyboard/mouse and pad **sum** then clamp — no device switching logic in the engine
  (the game decides prompts via `get_PadBound` + last-device heuristic in Zia).
- `NewFullscreen`: creates the window directly in borderless-fullscreen at desktop resolution
  (per-platform: Metal layer full-size / `WS_POPUP` monitor-size / X11 `_NET_WM_STATE_FULLSCREEN`
  before map). No 1-frame windowed flash: the window is never shown in windowed state.
- `DataDir(app)`: `%APPDATA%\<app>` / `~/Library/Application Support/<app>` /
  `$XDG_DATA_HOME/<app>` (fallback `~/.local/share/<app>`); creates the directory; returns
  normalized path; traps only on empty app name (DomainError, message
  `"Path.DataDir: app name must be non-empty"`).

## 3. Implementation — E1 raw relative mouse (session A)

Platform work behind one new vgfx entry point pair (ADR: new C ABI surface):
```c
// vgfx.h (new)
bool vgfx_set_relative_mouse(vgfx_window_t window, bool enabled); // returns native support
void vgfx_get_relative_deltas(vgfx_window_t window, double *dx, double *dy); // since last call
```
- **Win32** (`vgfx_platform_win32.c`): `RegisterRawInputDevices` (usage 0x02 generic mouse,
  `RIDEV_INPUTSINK` off — window-focused), accumulate `WM_INPUT` `RAWINPUT.data.mouse.lLastX/Y`
  when `MOUSE_MOVE_RELATIVE`; unregister on disable. Alt-Tab/focus-loss: auto-suspend
  (release cursor clip), resume on focus.
- **macOS** (`vgfx_platform_macos.m`): `CGAssociateMouseAndMouseCursorPosition(false)` +
  accumulate `NSEvent` `deltaX/deltaY` from `mouseMoved`/`mouseDragged` (these report raw HID
  deltas while dissociated); re-associate on disable/focus-loss. Keep cursor parked center.
- **Linux/X11** (`vgfx_platform_linux.c`): XInput2 `XISelectEvents` with `XI_RawMotion` on the
  root window; accumulate `XIRawEvent.raw_values` (fallback to `valuators` when raw absent);
  `XIQueryVersion` gate — if XI2.0 unavailable, return `false` (warp-to-center fallback used).
- **Mock** (`vgfx_platform_mock.c`): `vgfx_mock_push_relative_delta(dx,dy)` test hook;
  deterministic accumulation — this is what headless probes drive.
- **Runtime plumb**: `rt_input.c` gains `rt_mouse_set_relative_mode`; `rt_canvas3d.c` poll
  (the `captured` block at `:1816`) becomes: if relative-native → read
  `vgfx_get_relative_deltas` → `rt_mouse_force_delta`; else existing center-offset math.
  Sub-pixel: deltas kept as doubles in vgfx; `rt_input.c` stores f64 accumulators; existing
  i64 `Mouse.DeltaX/Y` round-to-nearest; new f64 precision flows through `Input3D.MouseDelta`.

## 4. Implementation — E2 / E3 / E4 (session B)

- **E2** `rt_game3d_input.c`: store `bound_pad` (default -1); `MoveAxis`/`LookAxis` add shaped
  pad contribution via existing `rt_pad_*` getters (`rt_input_pad.c`); radial deadzone helper
  (default 0.18, remap to 0–1). No per-frame allocation.
- **E3** vgfx: `vgfx_create_window_ex` gains a `fullscreen` flag (or new
  `vgfx_create_window_fullscreen`); wire per-platform paths listed in §2; `rt_canvas3d_new_full`
  + Game3D `World3D.NewFullscreen` constructor reusing existing world init
  (`rt_game3d_world_api.inc` world ctor path). `get_ActiveOutputWidth/Height` already report
  output size.
- **E4** `src/runtime/io/` (with `rt_platform.h` adapter for the three OS dirs — no raw
  `_WIN32` in runtime code): `rt_path_data_dir(app)`; recursive create; Windows uses
  `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` equivalent via existing platform adapter
  conventions.

## 5. Files

| File | Change |
|---|---|
| `src/lib/graphics/include/vgfx.h` | relative-mouse + fullscreen-create entry points |
| `src/lib/graphics/src/vgfx.c` | dispatch |
| `src/lib/graphics/src/vgfx_platform_win32.c` | WM_INPUT raw path; WS_POPUP fullscreen create |
| `src/lib/graphics/src/vgfx_platform_macos.m` | CGAssociate dissociation deltas; fullscreen create |
| `src/lib/graphics/src/vgfx_platform_linux.c` | XI2 RawMotion; _NET_WM fullscreen create |
| `src/lib/graphics/src/vgfx_platform_mock.c` | deterministic relative-delta hook |
| `src/runtime/graphics/input/rt_input.c` | relative mode state, f64 delta accumulators |
| `src/runtime/graphics/3d/render/rt_canvas3d.c` | captured-poll uses native deltas when available |
| `src/runtime/graphics/3d/rt_game3d_input.c` (or its .inc) | SetRelativeLook, BindPad merge |
| `src/runtime/io/rt_path*.c` + `src/runtime/rt_platform.h` | DataDir adapter |
| `src/il/runtime/runtime.def` | all §2 entries (RT_FUNC + RT_METHOD/RT_PROP pairs) |
| `docs/viperlib/input.md`, `docs/viperlib/graphics/rendering3d.md`, `docs/viperlib/io/*` | docs |
| `src/lib/graphics/tests/test_input.c`, `src/tests/unit/test_rt_canvas3d.cpp`, new `src/tests/unit/test_rt_path_datadir.cpp` | tests |

## 6. Tests (fail-before/pass-after)

1. vgfx mock: enable relative mode, push deltas (+3.5,−2.25) ×3, poll → accumulated (10.5,−6.75),
   second poll → (0,0). Disable → absolute positions resume.
2. `rt_input` unit: relative mode freezes `Mouse.X/Y`, `DeltaX/Y` round correctly, capture+release
   restores cursor visibility state.
3. Canvas3D captured-poll test (mock window): native-relative path bypasses warp; fallback path
   still warps (assert `vgfx_warp_cursor` called via mock counter).
4. Input3D pad merge: synthetic pad axes (0.5,0) + key W → MoveAxis clamps to unit length;
   deadzone zeroes 0.1 input; unbind restores keyboard-only.
5. `Path.DataDir("viper_test_app")` returns existing dir; per-OS suffix asserted via
   platform capability; empty name traps with exact message.
6. Fullscreen-create: mock backend asserts creation flag; on-hardware manual check
   (Metal + software) — no windowed flash, `get_IsFullscreen` true first frame.
7. Zia runtime probe `tests/zia_runtime/` addition: relative-look smoke driving synthetic
   deltas through `Input3D.LookAxis` under `RunFrames`.

## 7. Verification gate

`VIPER_SKIP_CLEAN=1 VIPER_TEST_LABEL=graphics ./scripts/build_viper_unix.sh` green →
`ctest --test-dir build -R 'input|canvas3d|path' --output-on-failure` → runtime completeness +
surface audit scripts → full no-skip build; manual on-hardware look-feel check (macOS): raw
mode vs warp fallback both smooth at 240 Hz+ polling, no edge saturation, Alt-Tab safe.
ADR filed for the new vgfx ABI entries. `docs/viperlib` updated. Windows/Linux code paths
compile-checked via the platform-policy lint (`./scripts/lint_platform_policy.sh`) and are
first exercised live in the P4 Windows lane (28-phasing §5).
