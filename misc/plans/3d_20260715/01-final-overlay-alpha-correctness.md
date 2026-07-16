# Plan 01 — Final-Overlay Alpha Correctness

## Outcome

Make `Canvas3D.DrawRect2DAlpha` implement straight-alpha source-over blending
in every overlay path and backend. The existing public signature remains
unchanged. Add automated tests that fail on the confirmed fully opaque result
and protect all overlay primitives from scalar/vertex alpha regressions.

## Why this is first

The defect is visible in 3dbowling and directly affects the current
`GameBase3D` fade transition. It reproduced on both software and Metal, which
places the likely fault in shared final-overlay command construction/replay or
in a state contract used by both implementations. Later framework work must
not build transition behavior on a known-broken primitive.

Confirmed repro:

```sh
build/src/tools/viper/viper run examples/games/3dbowling/known_viper_issues/overlay_alpha_repro.zia
```

Baseline input/background: `192,128,64`; overlay: black at `0.5`; observed
final sample: `0,0,0`; expected: approximately `96,64,32`.

## Scope

In scope:

- final-overlay screen-geometry command construction;
- vertex color alpha, command alpha, material diffuse alpha, and backend blend
  state interaction;
- temporary overlay frames and explicit `BeginOverlay`/`EndOverlay` frames;
- software, Metal, D3D11, and OpenGL source-over semantics;
- post-FX/no-post-FX and render-target/final-window paths;
- regression tests and correction of the known-issues README after proof.

Out of scope:

- changing the public color format or adopting premultiplied-alpha input;
- redesigning all transparent 3D materials;
- fixing AA-text identity (plan 02);
- declaring the historical material/overlay issue fixed without reproduction.

## Dependencies and ADR decision

Dependency: plan 00 only.

This should be an internal correctness fix with no new public API or C ABI, so
an ADR is normally not required. If investigation reveals that documented
alpha semantics must change, stop and create an ADR before implementation.

## Primary source owners

- `src/runtime/graphics/3d/render/rt_canvas3d_overlay.c`
- `src/runtime/graphics/3d/render/rt_canvas3d_draw.inc`
- `src/runtime/graphics/3d/render/rt_canvas3d_occlusion.inc`
- `src/runtime/graphics/3d/render/rt_canvas3d_internal.h`
- `src/runtime/graphics/3d/render/rt_canvas3d.c`
- `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c`
- Metal/D3D11/OpenGL draw/present/shader includes if the shared fix is
  insufficient
- `src/tests/unit/test_rt_canvas3d.cpp`
- `src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`
- `src/tests/unit/test_rt_canvas3d_production.cpp`
- `src/tests/fixtures/runtime/` and `src/tests/CMakeLists.txt`

## Contract to preserve

For straight-alpha input `(Cs, As)` over destination `Cd`:

```text
out.rgb = Cs * As + Cd * (1 - As)
out.a   = As + Cd.a * (1 - As)
```

Required edge cases:

- alpha `0` is a no-op and may skip queueing;
- alpha `1` replaces the destination in the covered area;
- non-finite alpha follows the current documented/default behavior;
- values outside `[0,1]` clamp;
- RGB tint is applied exactly once;
- opacity is applied exactly once—never once in vertex data and again in a
  scalar/material field;
- overlay ignores depth, is unlit, and is finalized after post-FX;
- draw ordering among overlay commands remains submission order.

## Implementation sequence

### Phase 1 — Freeze a minimal automated failure

1. Run the existing repro on software and Metal. Record final sample values,
   viewport, quality, post-FX, and backend.
2. Add a small fixture under `src/tests/fixtures/runtime/` that:
   draws a known opaque background, draws four adjacent black rectangles at
   alpha `0`, `0.25`, `0.5`, and `1`, captures the final frame, samples the
   rectangle centers, prints deterministic values, and exits.
3. Add a second case using a colored source such as red over blue. Black over a
   background cannot detect accidental RGB premultiplication.
4. Run both cases in an explicit overlay pass and through the helper's
   temporary-frame path.
5. Add a unit-level assertion for the queued final-overlay command: vertex
   alpha, diffuse alpha, scalar alpha, unlit, and depth-disable values. Use
   existing test access patterns; do not expose internals publicly.
6. Confirm the test fails for the observed reason before changing runtime code.

### Phase 2 — Trace the alpha pipeline

Follow one rectangle from API entry to final pixel:

1. In `rt_canvas3d_draw_rect2d_alpha`, confirm clamping and the alpha passed to
   `canvas3d_queue_screen_rect`.
2. In screen geometry construction, confirm each vertex receives the expected
   color/alpha and determine whether `cmd.alpha` is intended to multiply it.
3. In `canvas3d_make_final_overlay_screen_cmd`, verify why diffuse and scalar
   alpha are neutralized. The current comment says vertex alpha is the single
   source; prove that every replay shader actually consumes vertex alpha.
4. In final-overlay enqueue/replay, verify no material fill or replay copy
   restores opaque alpha, changes blend mode, or drops vertex color.
5. In software rasterization, inspect the fragment color and blend equation.
6. In Metal, D3D11, and OpenGL, inspect vertex layout, shader varyings, pipeline
   blend factors, and color write mask for the final-overlay replay pipeline.
7. Use temporary debug logging only behind an existing diagnostic mechanism or
   remove it before commit. Never leave unconditional per-draw logging.

Write a short root-cause note in the change description identifying the first
stage where `0.5` becomes or behaves as `1.0`.

### Phase 3 — Apply the narrow shared fix

Choose the fix based on evidence:

- If final replay drops vertex alpha, carry it through the shared vertex/shader
  contract.
- If software or a GPU pipeline uses opaque blend state, correct that pipeline
  to source-alpha/one-minus-source-alpha.
- If alpha is multiplied twice, establish one source of opacity and neutralize
  the other consistently.
- If final-overlay replay incorrectly reuses the general opaque mesh pipeline,
  introduce the smallest internal overlay pipeline distinction rather than
  weakening all mesh draws.

Do not solve this by pre-blending colors on the CPU. That would be wrong over
unknown destinations and would break overlapping translucent commands.

Keep `DrawRect2D` opaque. Confirm `DrawLine2DAlpha`, AA-text/image alpha, and
widget overlays still share correct blend semantics after the fix.

### Phase 4 — Expand regression coverage

Add tests for:

- alphas `0`, `0.25`, `0.5`, `0.75`, `1`;
- colored source over colored destination;
- two overlapping 50% rectangles in submission order;
- explicit overlay and auto temporary overlay;
- post-FX enabled and disabled;
- final-window capture and RenderTarget3D path if final overlays are supported
  there;
- resize between frames;
- repeated frames to catch stale blend/pipeline state;
- an opaque 3D draw after/before overlay to prove state restoration;
- the historical `material_overlay_repro.zia` as a non-regression sentinel.

Use per-channel tolerances that account for 8-bit rounding, normally ±1 or ±2.
Do not use a broad visual hash when exact center samples are sufficient.

### Phase 5 — Cross-backend closure

1. Run software and Metal locally on macOS.
2. Run D3D11 on Windows and OpenGL on its supported CI/desktop lane.
3. If a backend requires a local fix, keep the semantic equation identical and
   add a backend-focused unit/helper test.
4. Verify pipeline/blend state is restored after final-overlay replay.
5. Record untested backend cells as pending in plan 20; do not call the plan
   released until all cells pass.

### Phase 6 — Documentation cleanup

- Update the known-issues README to say the repro is now a regression fixture,
  including the fixing test/commit reference.
- Remove any warning in GameBase3D docs that suggests avoiding alpha fades.
- Do not delete the compact repro; it is useful for manual backend diagnosis.

## Validation commands

At minimum:

```sh
ctest --test-dir build -R 'test_rt_canvas3d|test_rt_canvas3d_gpu_paths|test_rt_canvas3d_production' --output-on-failure
ctest --test-dir build -R 'g3d_.*overlay' --output-on-failure
ctest --test-dir build -L graphics3d --output-on-failure
./scripts/lint_platform_policy.sh --changed-only
```

Run the full platform build script before completion.

## Acceptance criteria

- The original repro produces the expected half blend on software and Metal.
- Automated tests cover exact alpha edge cases and colored/overlapping sources.
- D3D11 and OpenGL pass the same semantic checks before release.
- No public API, class ID, or runtime registry change is introduced.
- Opaque overlays, text/images, 3D transparency, post-FX, resize, and state
  restoration remain green.
- The root cause is documented with evidence rather than inferred.

## Stop conditions

Stop and escalate if the only apparent fix requires changing all material alpha
semantics, changing the public color representation, or introducing a
backend-specific visible result. Those outcomes indicate a larger render
contract decision that needs an ADR.

## Handoff evidence

Provide before/after pixel samples, root-cause stage, exact tests added, backend
matrix, changed shader/blend state if any, and confirmation that the historical
material repro remains non-reproducing.
