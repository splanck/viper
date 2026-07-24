---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0168: Add Windowless Canvas3D Rendering

## Status

Accepted (2026-07-23)

## Context

Authoring tools, thumbnails, tests, server-side previews, and offline image
generation need the production 3D renderer without opening another platform
window. `RenderTarget3D` already owns an offscreen color/depth attachment, but
every `Canvas3D` constructor creates a window before a render target can be
bound. Zanna Studio therefore cannot embed a truthful shaded scene preview in
its existing GUI window and currently substitutes a node-marker wireframe.

Creating an invisible native window would still consume window-system and GPU
state, can flash or steal focus, fails on machines without a display server,
and has platform-specific lifecycle behavior. Reimplementing mesh and PBR
rasterization in Studio would duplicate runtime semantics and immediately
diverge from games.

Adding public runtime C ABI entry points requires this decision under ADR 0006.

## Decision

`Zanna.Graphics3D.Canvas3D` gains one additive static constructor and one
read-only property:

```text
NewOffscreen(target: Zanna.Graphics3D.RenderTarget3D) -> Canvas3D
IsOffscreen: Boolean
```

Their C ABI entry points are:

```c
void *rt_canvas3d_new_offscreen(void *target);
int8_t rt_canvas3d_get_is_offscreen(void *canvas);
```

`NewOffscreen` requires a live `RenderTarget3D`, retains it as the active
output, and creates the portable software backend without creating a platform
window or registering keyboard, mouse, gamepad, resize, or presentation state.
The caller keeps its target handle so it can use `AsPixels`/`CopyTo`; normal
`Canvas3D.Screenshot` APIs read the same active target.

The windowless canvas supports the regular render-target pipeline:

- `Clear`, `Begin`/`DrawMesh`/`End`, `SceneGraph.Draw`, screen-space overlay
  passes, post-processing, and screenshot readback operate on the bound target;
- authored hierarchy, visibility, transforms, meshes, materials, maps, and
  scene lights therefore use the same runtime traversal and software shading
  as a windowed canvas;
- `Width`, `Height`, `ActiveOutputWidth`, and `ActiveOutputHeight` report the
  bound target dimensions, while `WindowWidth` and `WindowHeight` report zero;
- `Backend` reports `software`, `IsOffscreen` reports true, `Flip` is inert,
  and live `Poll` returns zero because no window or input source exists; and
- `SetRenderTarget` may transactionally replace the active target.

`Resize` is rejected for an offscreen canvas because dimensions belong to its
explicit render target. Callers resize by allocating a replacement target and
calling `SetRenderTarget`. `ResetRenderTarget` (including
`SetRenderTarget(null)`) is likewise rejected and leaves the current target
bound because a windowless canvas has no fallback output.

Null, stale, and wrong-class target handles trap at construction with an
actionable error. Allocation or backend initialization failure returns no
partially usable canvas. Graphics-disabled builds expose matching symbols:
construction traps through the existing unavailable-graphics policy and
`IsOffscreen` returns false.

Zanna Studio uses one offscreen canvas, target, and orthographic camera for its
3D scene viewport. Its camera orbit and half-height exactly match the existing
editor projection so retained hierarchy markers and transform gizmos remain
aligned. Studio keeps an explicit wireframe mode and falls back to it if
offscreen allocation or readback fails.

## Consequences

- Studio and other tools can show production meshes and PBR materials inside an
  existing UI instead of maintaining a second renderer.
- Headless rendering is deterministic across macOS, Windows, and Linux because
  the constructor deliberately selects the from-scratch software backend.
- Offscreen callers pay software-rasterization cost and should render only when
  content or camera state changes, reuse targets, and bound preview resolution.
- The explicit target makes memory ownership, resizing, and allocation failure
  visible to callers rather than hiding a second framebuffer inside Canvas3D.
- Existing windowed callers remain source- and binary-compatible because the
  constructor, property, C symbols, and relaxed render-target guards are
  additive.
- Native rendering tests, graphics-disabled linkage, the reviewed Graphics3D
  ABI manifest, generated runtime documentation, authored graphics guidance,
  and Studio probes must cover the contract.

## Alternatives Considered

- **Create a hidden native window.** Rejected because it still depends on a
  display server and introduces focus, lifecycle, and backend differences.
- **Add a Studio-only rasterizer.** Rejected because mesh, material, texture,
  lighting, alpha, hierarchy, and visibility semantics would diverge from the
  runtime used by games.
- **Return an internally hidden render target.** Rejected because callers could
  not resize or perform allocation-free readback without more accessor surface,
  and the extra ownership would be surprising.
- **Let `New` accept an empty title as an offscreen signal.** Rejected because
  it overloads presentation text with lifecycle semantics and silently changes
  existing programs.
- **Select the platform GPU backend without a window.** Rejected for this
  contract because current native backends are drawable/window oriented and
  would make headless availability platform dependent.
