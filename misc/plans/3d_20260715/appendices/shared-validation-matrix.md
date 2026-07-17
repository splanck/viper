# Appendix — Shared Validation Matrix

## Purpose

This matrix defines the minimum evidence required by change type. Individual
plans add focused tests; they do not remove these shared gates. Commands assume
the repository root and an existing build unless the row explicitly calls the
platform build script.

## Baseline capture

Before implementation, record:

```sh
git status --short
build/src/tools/zanna/zanna --dump-runtime-api > /tmp/zanna-runtime-api-before.json
ctest --test-dir build -L graphics3d --output-on-failure
```

Do not commit `/tmp` output. If the build is absent or stale, use the platform
script first. On macOS:

```sh
ZANNA_SKIP_CLEAN=1 ./scripts/build_zanna_mac.sh
```

Use the corresponding Linux or Windows script on those systems. Never use raw
CMake as a substitute for the full platform build gate.

## Change-type matrix

| Change type | Required focused evidence | Required shared evidence |
|---|---|---|
| Backend blend/texture fix | Minimal repro; unit test around command state; software plus affected GPU capture/samples | `test_rt_canvas3d`, `test_rt_canvas3d_gpu_paths`, `test_rt_canvas3d_production`, graphics3d label |
| World3D internals | New C unit tests; Zia fixture/probe; lifecycle/stale-handle test | `test_rt_game3d`, diagnostics, graphics3d label |
| Public runtime class/member | Surface dump diff; VM and native/link coverage; disabled-graphics symbols | completeness script, surface audit bundle, docs snippets |
| Fixed-step or event order | Deterministic trace repeated twice; dropped-step/clear-boundary tests | run-fixed/run-frames probes, native callback parity |
| Resource ownership | double-release, partial construction, world-destroy-first, scope-destroy-first, stale object tests | ASan/LSan lane where available; runtime robustness |
| Asset/save parser | missing/corrupt/oversized/truncated inputs; atomic failure test | fuzz or corpus smoke where applicable; platform path tests |
| Performance hot path | warm-up then allocation count and timing at representative load | existing perf label; no regression beyond plan budget |
| Docs/examples only | `zanna check` every changed Zia file; `./scripts/check_docs.sh` link/path check | docs snippet probes and example/package smoke |
| Demo migration | all existing demo probes before and after; deterministic state comparison | graphics3d label and platform build |

## Core focused commands

Use exact executable paths from the current build tree. Common commands:

```sh
ctest --test-dir build -R 'test_rt_canvas3d|test_rt_canvas3d_gpu_paths|test_rt_canvas3d_production' --output-on-failure
ctest --test-dir build -R 'test_rt_game3d|test_rt_game3d_diagnostics|test_rt_game3d_thirdperson|test_rt_game3d_combat' --output-on-failure
ctest --test-dir build -R 'g3d_test_game3d_(world|runframes|runfixed|assets|effects|sound|persistence|docs)' --output-on-failure
ctest --test-dir build -L graphics3d --output-on-failure
./scripts/check_runtime_completeness.sh
./scripts/audit_runtime_surface.sh
./scripts/lint_platform_policy.sh --changed-only
```

The regex examples are starting points. Confirm test names with
`ctest --test-dir build -N` and include every new test explicitly in handoff
evidence.

## Runtime surface validation

For any `.def`, public header, class ID, VM bridge, or runtime registry change:

1. Save the pre-change `--dump-runtime-api` JSON.
2. Build the registry/tools.
3. Save the post-change JSON.
4. Diff only the expected classes/members.
5. Run:

```sh
./scripts/check_runtime_completeness.sh
./scripts/audit_runtime_surface.sh
ctest --test-dir build -R 'test_graphics3d_abi_surface|test_graphics3d_runtime_manifest|test_runtime_class_qualified_surface|test_runtime_surface_audit' --output-on-failure
```

6. Check both interpreted Zia and native/AOT use for any callback or typed
   object signature.
7. Build the disabled-graphics/runtime variant used by current CI and verify
   every new symbol has a deliberate stub behavior.

Do not update a golden/surface inventory until the change is approved and the
diff has been manually reviewed.

## Backend matrix

| Platform | Backend | Role | Minimum requirement |
|---|---|---|---|
| Any | Software | Portable correctness/headless reference | Required for all render-semantic changes |
| macOS | Metal | Apple GPU implementation | Required for plans 01–02 and release wave |
| Windows | D3D11 | Windows GPU implementation | Required before public release of render/API changes |
| Linux/portable desktop | OpenGL | GL implementation | Required before public release of render/API changes |

A local owner may leave D3D11/OpenGL cells pending only at an intermediate
change if plan 20 assigns named follow-up owners and the change is not declared
released. Backend-specific semantics may not be waived because software passes.

For image checks, record:

- backend and adapter;
- viewport and render scale;
- quality tier and post-FX state;
- exact sampled coordinates/expected tolerance;
- final-frame versus scene-only capture;
- hash only when deterministic across the target backend; otherwise use
  structural sample/tolerance checks.

## Fixed-step determinism protocol

For any simulation-affecting change:

1. Use a fixed seed and fixed delta.
2. Avoid wall-clock conditions and asynchronous asset completion inside the
   measured interval.
3. Run the scenario twice in one process and once in a fresh process.
4. Capture a state trace at defined frames: entity positions/velocities,
   controller state, event kinds/IDs, and relevant counters.
5. Require byte-identical integer/event traces and tolerance-bounded floats.
6. Separately test a long-frame case that triggers the spiral guard and records
   dropped steps.
7. Verify render interpolation changes captures but not authoritative state.

## Lifecycle protocol

Every owning class/scope/pool must test these sequences:

1. normal create/use/release;
2. release twice;
3. partially constructed object followed by release;
4. child released before owner;
5. owner/world destroyed before wrapper finalizer;
6. tracked entity manually despawned before scope release;
7. pool/resource clear while handles are live;
8. out-of-memory or capacity-growth failure where injection support exists;
9. stale-handle public calls produce established diagnostics rather than UAF;
10. a loop of at least 100 create/release cycles has stable live counts.

## Performance budgets

Plans with hot paths must state a plan-specific budget. Program defaults:

- FrameDriver3D and event polling: zero heap allocations per steady-state frame.
- SceneScope3D: allocation allowed on registration/growth; zero per-frame work.
- Environment stack: zero allocations per steady-state update/draw after
  registration.
- Query result/list: reuse bounded world scratch or documented caller-owned
  object; no per-hit object churn in the normal path.
- Effect/audio pools: zero allocations for an emit/play hit after warm-up.
- Quality resolve/apply: allocation permitted on explicit quality change, not
  every frame.
- Save and asset resolution: allocations permitted off the hot path, all input
  lengths/counts bounded.

If an existing allocator/GC design makes a default impossible, measure the
current baseline, document the delta, and obtain explicit review rather than
quietly weakening the goal.

## Demo gates

### 3dbowling

Use `examples/games/3dbowling/run_probes.sh` or the Windows companion and retain
the existing release-gate selection documented by the game. Also run the three
known-issue repros explicitly while plans 01–02 are active. Record when a visual
probe requires Metal due to software runtime cost.

### Ridgebound

Use `examples/games/ridgebound/run_probes.sh` or the Windows companion, plus the
documented smoke, state, topology, and traversal probes. Validate at least
Performance and Cinematic quality and both software/available GPU rendering.

### Ashfall

Run every file under `examples/games/ashfall/probes` through the project's
established runner/command. The reviewed baseline was 14/14. Migration must
retain smoke, core, movement, combat, level, enemy, campaign, menu, assets,
manifest, render, performance, meta, and stress-combat coverage.

## Documentation and packaging

For changed public APIs:

- update modular `.def` documentation comments;
- update `docs/zannalib/graphics/game3d.md` and the low-level page when relevant;
- update architecture/frame-order docs for phase/lifetime contracts;
- add or update docs snippet fixtures;
- update starter/example code and check it;
- run package dry-run tests for changed starters/assets;
- run a markdown link/path scan over this package and changed docs;
- never hand-edit generated docs when a generator owns them.

## Full exit gate

Before a plan is marked complete:

```sh
./scripts/build_zanna_mac.sh
```

or the target platform equivalent must pass without skip flags. Every plan in
this program adds CMake source/test entries, and `ZANNA_SKIP_CLEAN=1` can mask
configure-stage errors after CMakeLists changes, so the completion gate is a
clean configure+build+test run; skip flags are for intermediate iteration
only. Before the program release, clean full scripts must pass on macOS,
Windows, and Linux. The release owner also runs platform policy lint, runtime
API audits, smoke/install/package stages, and any sanitizer/fuzz lanes
required by the changed parsers/lifetimes.

