# Capabilities And Stub Policy

Viper supports builds where some subsystems are absent or stubbed. That is
reasonable, but the public API must describe the behavior.

## Current Risks

Language-service stubs are part of the runtime core source list:

- `src/runtime/core/rt_zia_completion_stub.c`;
- `src/runtime/core/rt_zia_highlight_stub.c`;
- `src/runtime/core/rt_basic_completion_stub.c`.

Graphics-disabled builds compile many public stubs:

- `src/runtime/graphics/common/rt_canvas_stubs.c`;
- `src/runtime/graphics/common/rt_canvas3d_stubs.c`;
- `src/runtime/graphics/common/rt_3d_asset_stubs.c`;
- `src/runtime/graphics/common/rt_3d_scene_stubs.c`;
- `src/runtime/graphics/common/rt_3d_physics_stubs.c`;
- `src/runtime/graphics/common/rt_3d_world_stubs.c`;
- `src/runtime/graphics/common/rt_disabled_runtime_stubs.c`.

The behavior is mixed:

- some functions trap;
- some optionally trap if `VIPER_GRAPHICS_STUBS_STRICT` is set;
- some return `NULL`;
- some return empty strings or empty collections;
- some no-op;
- some return plausible scalar values such as `0`, `1`, or `1.0`.

## Capability Metadata

Every public row needs:

- capability name, such as `core`, `filesystem`, `graphics2d`, `graphics3d`,
  `gui`, `audio`, `network`, `crypto`, `tooling`;
- required build flag or link feature;
- availability in current binary;
- unavailable behavior;
- whether a probe API exists;
- whether fallback behavior is exact, degraded, or unavailable.

Example metadata:

```json
{
  "capabilities": ["graphics3d"],
  "availability": "build-dependent",
  "unavailable_behavior": "trap",
  "probe": "Viper.Graphics3D.Canvas3D.IsAvailable"
}
```

## Stub Behavior Decisions

### Stable Public APIs

Stable public APIs must not silently pretend work succeeded. If a capability is
missing, use one of:

- `Result` with an unavailable-capability error;
- explicit trap with capability diagnostic;
- documented probe returning `i1`.

### Constructors

Constructors for unavailable capabilities should not silently return `NULL`.

Decision:

- stable constructors either trap or return `Result<T>`.
- `NewOption` is acceptable only when absence is normal and not an error.
- no-op constructor stubs are allowed only for test-only preview APIs.

### Queries

Query APIs may return fallback values only if:

- the method name is a probe, such as `IsAvailable`;
- the docs define the fallback;
- metadata marks the result as a fallback;
- the value cannot be mistaken for a valid real measurement.

`contrast_ratio == 1.0` is not acceptable as a stable fallback because it looks
like a real contrast value.

### No-Op Setters

No-op setters are dangerous when the getter later returns a plausible value or
when user code expects state to be retained.

Decision: no-op setters in disabled builds must either:

- trap;
- return `Result<void>`;
- be limited to preview/test harness APIs with metadata.

## Work Items

1. Add capability metadata to runtime definitions.
2. Add a generated `viper runtime capabilities --json` or include current
   availability in `--dump-runtime-api`.
3. Normalize graphics-disabled constructors.
4. Normalize language-service unavailable payloads around `Result` or explicit
   `available: false` objects.
5. Replace fake scalar fallbacks with traps or unavailable results.
6. Add tests for strict and non-strict disabled builds.
7. Update docs to teach capability probes before capability-dependent calls.

