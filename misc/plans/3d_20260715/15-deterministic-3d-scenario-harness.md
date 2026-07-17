# Plan 15 — Deterministic 3D Scenario Harness

## Outcome

Create a shared Zia scenario harness and test convention for synthetic input,
fixed stepping, state traces, final-frame capture, pixel assertions, allocation/
counter checks, and standardized pass/fail output. Use it to reduce the custom
probe-loop boilerplate across all three games without adding product runtime
API in the first version.

## Problem statement

Zanna already provides the essential primitives: synthetic Canvas3D input
(`PushSyntheticKey`, `PushSyntheticMouse`, `AdvanceSyntheticFrame`,
`ClearSyntheticInput`, `SetSyntheticDeltaTimeSec`), World3D
RunFrames/RunFramesOnly, manual phase methods, final-frame capture
(`World3D.CaptureFinalFrame`, `Canvas3D.ScreenshotFinal`), Pixels inspection,
and many C/Zia fixtures. Each game still builds its own probe loops,
termination, state assertions, image sampling, and output format. That makes
migrations harder to compare and test behavior inconsistent.

## Dependencies

- plan 03 FrameDriver3D for custom fixed/render scenarios;
- plan 10 stable entity/event traces;
- plan 12 framework integration is optional but should consume the harness;
- current fixtures, CTest conventions, and demo probe scripts.

No ADR is needed for example/test library code. Promotion into the product
runtime or a new public testing ABI requires a later ADR.

## Scope

In scope:

- `examples/games/lib/scenario3d.zia` and small support modules;
- deterministic seed/time/input schedule;
- fixed step and render/capture control;
- reusable scalar/vector/event/pixel assertions;
- trace recording/comparison and standard output/exit convention;
- CTest registration helpers/conventions where appropriate;
- adoption by representative probes in all three games.

Out of scope:

- a general unit-test framework replacement;
- image-diff machine learning or external libraries;
- hiding backend requirements;
- product save/replay format;
- nondeterministic real-time soak testing.

## Primary owners

- new modules under `examples/games/lib/`;
- `src/tests/fixtures/runtime/` shared examples if a copy is needed;
- `src/tests/CMakeLists.txt` helper patterns;
- demo run-probe scripts and selected probes;
- `docs/internals/testing.md` and Game3D testing section.

## Harness design

Prefer composition over inheritance. A `Scenario3D` object/config holds:

- World3D and optional FrameDriver3D;
- fixed dt, frame/step limits, seed;
- scheduled synthetic key/mouse/button/axis events by step/frame;
- named state samples supplied explicitly by the scenario;
- assertion/failure count and first diagnostic;
- capture requests and pixel sample expectations;
- backend/capability metadata for diagnostics.

The harness cannot accept arbitrary closures if Zia representation makes them
awkward. Provide explicit drive methods and a small scenario interface/base
class when needed:

```text
setup(scenario)
fixedStep(scenario, step, dt)
beforeRender(scenario, frame)
drawCustom(scenario, frame)
sampleState(scenario, step)
finish(scenario)
```

Keep test dispatch on the VM/main thread.

## Determinism rules

- fixed seed and fixed delta;
- input edges scheduled by integer step/frame, not wall time;
- asynchronous assets preloaded or excluded from measured interval;
- no dependency on monitor refresh;
- authoritative state sampled after the documented simulation boundary;
- float comparisons use explicit per-field tolerance;
- event traces use stable IDs/kinds, never pointer strings;
- image assertions use structural samples/regions unless a backend-specific
  golden is intentionally maintained;
- test prints exactly one final `RESULT: pass` or `RESULT: fail ...` line and
  returns through the existing runner convention.

## Implementation sequence

### Phase 0 — Probe inventory

Catalog every probe in the three games by:

- state-only versus render/capture;
- fixed/manual/real-time loop;
- synthetic input needs;
- backend/display requirement;
- repeated helper code;
- expected runtime and timeout.

Select one state, one input, one render, and one stress probe across the games
as initial consumers. Do not mechanically rewrite all probes.

### Phase 1 — Minimal assertions/output

Implement helpers for:

- `expectTrue`, integer/string equality;
- float/vector near with caller tolerance;
- range and monotonic assertions;
- event kind/entity sequence;
- Pixels RGBA near, luminance/color dominance, non-empty bounds;
- counter before/after equality;
- fail-fast option versus collect failures;
- standardized result line.

Assertion messages include scenario, step/frame, expected, and actual.

### Phase 2 — Input schedule

Wrap existing synthetic input operations; do not add a second input state.
Support key/button press/hold/release, mouse delta, resize/close if current test
hooks allow it, and clear/reset. Verify edge semantics match Input3D and Action
in the same polled frame.

### Phase 3 — Fixed/run modes

Provide:

- simulation-only N fixed steps;
- N rendered frames via FrameDriver with zero/multiple steps naturally;
- custom render hooks;
- capture without present where approved;
- controlled long-frame injection for spiral guard (through
  `SetSyntheticDeltaTimeSec`, never wall-clock sleeps);
- timeout/maximum steps that fails rather than hangs.

Never call both `World3D.Update` and FrameDriver BeginFrame in one scenario.

### Phase 4 — Trace and replay comparison

Create a bounded in-memory trace of integers and quantized/tolerance-compared
floats. Run the same scenario twice and compare. Avoid writing a new persistent
format in v1. Provide a concise first-difference diagnostic.

### Phase 5 — Capture utilities

Centralize final-frame capture and sample coordinates relative to viewport.
Support:

- exact/near RGBA;
- region color dominance;
- alpha blend expected value;
- non-background occupancy;
- optional PNG save only when existing Pixels API supports it and test runner
  requests artifacts.

Keep software as correctness baseline and declare backend-specific GPU tests.

### Phase 6 — CTest integration

Add a small CMake helper only if it reduces repeated registration while
retaining labels, timeouts, working directory, backend environment, and
display requirements. Changing broad test infrastructure may require separate
review; keep initial changes localized.

### Phase 7 — Representative migrations

- Bowling: migrate one trajectory/state probe and one overlay sample probe.
- Ridgebound: migrate state/quality or traversal plus one render smoke section.
- Ashfall: migrate movement/combat state scenario and one render/capture probe.

Run original and harness versions side by side until traces/samples match. Only
then delete duplicate probe code. Keep game-specific domain assertions in the
game modules.

### Phase 8 — Harness self-tests/docs

Add self-tests for failure messages, tolerance boundaries, input edges,
timeouts, repeated traces, capture coordinates, and cleanup after failure.
Document adding a state-only test, render test, GPU-specific test, and stress
test.

## Performance and reliability

- bounded trace/sample storage with explicit overflow failure;
- no filesystem writes unless artifacts requested;
- deterministic maximum frames/steps and CTest timeout;
- cleanup world/driver/scopes even after assertion failure;
- report backend/capability to make CI failures actionable;
- harness overhead measured and excluded from game performance thresholds where
  necessary.

## Validation

Run harness self-tests, original/migrated representative probes side by side,
software and available GPU capture tests, full graphics3d label, demo probe
scripts, package/working-directory tests, and full platform builds.

## Acceptance criteria

- Representative probes in all three games use the harness and match originals.
- Input/fixed/event traces repeat identically.
- Pixel assertions catch plans 01–02 regressions with actionable diagnostics.
- Scenarios cannot hang and clean up after failure.
- No product runtime API or external dependency is added.
- Test labels/backend requirements remain explicit.

## Stop conditions

Stop if the harness requires production-only hooks that weaken runtime safety,
relies on wall-clock input, hides display/backend requirements, or becomes a
replacement for all testing infrastructure. Keep it a thin composition layer.

## Handoff evidence

Provide probe inventory, selected before/after code size, trace/sample parity,
self-test output, runtime overhead, labels/timeouts, and recommendation on
whether to promote any helper later.
