# Plan 05 — Device-Loss / Resize Fuzz Scaffolding

The two backends with no compile coverage on the macOS dev machine (D3D11,
OpenGL) are also the two with real device-loss and context-loss semantics.
This plan is small and lives with whichever work first lands on a
Windows/Linux box.

## Scope

1. **Resize fuzz probe** (`src/tests/graphics_conformance/resize_fuzz.zia`):
   deterministic pseudo-random walk of `Canvas3D.Resize` calls (including
   1×1, odd sizes, and repeated same-size) interleaved with rendered frames
   of the conformance scene; asserts no trap, `FrameFinalized` stays true,
   and a final screenshot at a known size matches the SW golden within
   conformance tolerance. Register per-backend in the by-hand script (SW leg
   can join ctest immediately — it validates target/temp-resource rebuild
   logic that is backend-independent).
2. **D3D11 device-removed handling**: audit every `Present`/`CreateBuffer`
   call site in `vgfx3d_backend_d3d11*.inc` for
   `DXGI_ERROR_DEVICE_REMOVED/RESET`; route to one recovery path that
   recreates the device, re-uploads cached geometry (geometry revision cache
   already knows what lives on GPU), and marks all backend caches invalid.
   Fuzz hook: `VIPER_D3D11_SIMULATE_DEVICE_LOSS=<frame>` env triggers
   `ID3D11Device4::RegisterDeviceRemovedEvent`-style simulated loss in debug
   builds (real trigger: `GetImmediateContext`→TDR via
   `D3D11_CREATE_DEVICE_DEBUG` + `ReportLiveDeviceObjects` audit run).
3. **GL context loss**: `GL_KHR_robustness` `glGetGraphicsResetStatus` poll
   in the frame loop when the extension is present; same recovery route
   (recreate context, invalidate caches). WGL/GLX/EGL specifics stay inside
   the platform adapter layer per the platform policy.
4. **Shared recovery contract**: backend vtable gains
   `int (*recover)(void *ctx)`; `rt_canvas3d.c` calls it when a frame
   submission reports loss, then replays the current frame once. Two
   consecutive failures → the documented recoverable render error, never a
   crash.

## Acceptance

- Resize fuzz green on SW (ctest) and on Metal/D3D11/GL by hand.
- Simulated device-loss run renders identically after recovery (conformance
  diff pre/post loss ≤ exact-mode tolerance on the same backend).
- No leaked GPU objects across recovery (D3D11 debug layer clean;
  `Diagnostics3D` texture/buffer counters return to pre-loss values).
