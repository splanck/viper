# Appendix — Dependency and Risk Register

## Dependency graph

```text
01 overlay alpha ----+
02 Metal text -------+--> 03 frame driver --> 04 scopes --> 05 environment --> 09 quality
                                  |              |            |                 |
                                  |              +----------> 11 feedback <-----+
                                  |              +----------> 12 application
                                  +--> 06 motor <------ 10 queries/events
                                  +--> 07 camera <----- 10 queries/events

08 asset policy ------------------> 09 quality, 14 save, 17–19 migrations
10 queries/events ----------------> 06, 07, 11, 13 ballistics, 15 tests
11 feedback ----------------------> 13 ballistics
03/04/10 -------------------------> 15 scenario harness
03–15 ----------------------------> 16 docs/starters
foundations ----------------------> 17 bowling, 18 ridgebound, 19 ashfall
all ------------------------------> 20 release
```

Plans 01, 02, 08, and the design portion of 10 can start independently. Plans
03–07 and 10–13 share World3D structures or definition files and need landing
coordination.

## File-overlap map

| Shared owner/file area | Plans likely to touch it | Coordination rule |
|---|---|---|
| `rt_canvas3d*`, backend texture/blend files | 01, 02, 05 | Land 01–02 first; plan 05 should not alter overlay internals |
| `rt_game3d_internal.h` / `rt_game3d.h` | 03–07, 10–14 | One active structural owner or a pre-agreed field/ID allocation map |
| `rt_game3d.c` and included world-sim/render files | 03–05, 10, 12 | Land in dependency order; keep feature implementation in focused translation units/includes |
| `game3d/*.def` | 03–11, 13–14 | Update API register first; append class IDs once; rerun surface dump after every landing |
| `src/runtime/CMakeLists.txt` | all new C classes | Rebase immediately before landing; update real and disabled source lists |
| `Game3DRuntime.cpp` | any callback proposal | Avoid callbacks; if unavoidable, one bridge owner and explicit signature tests |
| `docs/zannalib/graphics/game3d.md` | most plans | Each plan owns a named section; plan 16 performs final navigation/editing pass |
| `examples/games/lib` | 12, 15, 16 | Keep application and test helpers in separate modules with explicit imports |
| demo `game.zia`/loop files | 17–19 | One migration plan per game; no cross-game mechanical rewrite |

## Risk scale

- **Critical**: data corruption, UAF, ABI break, cross-backend semantic split,
  or nondeterministic simulation.
- **High**: common demo regression, frame-order incompatibility, unbounded hot
  path, or public API that cannot be represented by all frontends.
- **Medium**: discoverability, migration complexity, fallback inconsistency, or
  backend-specific performance regression.
- **Low**: localized docs/naming/polish issue with an easy compatibility path.

## Program risk register

| ID | Risk | Severity | Trigger/evidence | Mitigation and owner |
|---:|---|---|---|---|
| R1 | Overlay fixes diverge across backends | Critical | Software pass but Metal/D3D11/GL mismatch | Plans 01–02 define semantic tests at command and final-capture layers; release matrix in plan 20 |
| R2 | New frame driver becomes another inflexible Run loop | High | Ashfall/Ridgebound still need to bypass it | Poll-based reserve/commit steps and explicit render phases; Ashfall spike before API freeze |
| R3 | Driver and World3D both poll input or advance time | Critical | duplicate edges, doubled elapsed, inconsistent dt | One owner per frame; phase state machine and unit tests in plan 03 |
| R4 | Scene scope double-removes shared resources | Critical | shared body/node/effect tracked in two scopes | one-scope ownership, idempotent duplicate registration, child transfer rules, stale checks in plan 04 |
| R5 | Environment stack changes transparency/depth order | High | water/vegetation visual regression | per-type render phase contract and software/GPU captures in plan 05 |
| R6 | Motor duplicates or fights CharacterController3D | High | two writes per fixed step, grounded instability | motor wraps one controller, consumes intent, and never owns camera/input in plan 06 |
| R7 | Camera modifiers mutate authoritative state | High | replay/determinism changes with shake enabled | render-only camera state, explicit seed, state trace tests in plan 07 |
| R8 | Asset catalog becomes a second resolver | High | different source/package order by loader | delegate to `IO.Assets`/`Assets.Resolver`; path-only catalog contract in plan 08 |
| R9 | Quality profiles overwrite authored post-FX | High | Ridgebound/Ashfall visual identity lost | immutable policy record + explicit apply ownership; authored override tests in plan 09 |
| R10 | Unified events allocate per event or retain stale entities | Critical | GC spikes/UAF after despawn | world-owned bounded storage, immutable frame views, stable IDs, clear-boundary tests in plan 10 |
| R11 | Query body→entity lookup misses raw bodies | Medium | raw physics hit has null Entity | valid raw-body result plus optional entity; scopes/registration can associate raw bodies |
| R12 | Pool reuse exposes old state | High | particles/sounds resume with stale transform/timer | complete reset contract per type and reuse tests in plan 11 |
| R13 | App framework duplicates UI widgets | Medium | new Game3D Button/Menu types | reuse `Zanna.Game.UI`; framework only owns orchestration in plan 12 |
| R14 | Ranged combat hard-codes FPS weapon policy | High | bowling/non-combat users inherit unnecessary systems | stateless ballistics + DamageSpec; weapon cadence/ammo stay game-owned in plan 13 |
| R15 | Save composition corrupts otherwise valid world saves | Critical | partial metadata/world write | staged temp files, manifest commit last, rollback/no-state-change tests in plan 14 |
| R16 | Scenario harness is product-only or display-dependent | Medium | tests cannot run software/headless lane | use current software/synthetic input; keep harness in examples until proven in plan 15 |
| R17 | Demo migration changes game feel | High | state/trajectory/probe deltas | strangler slices, before/after traces, tolerance budgets in plans 17–19 |
| R18 | Public surface grows faster than docs/audits | High | runtime dump drift, unqualified `obj` APIs | API register, per-plan surface diff, plan 16 navigation, plan 20 audit |
| R19 | Unrelated uncommitted changes are overwritten or historical deletions resurrected | Critical | unrelated worktree edits clobbered; plan directories removed by the 2026-07 docs reorganization restored | inspect `git status --short` before every patch; never restore deleted historical plan directories as a side effect |
| R20 | Source file size/health baselines regress | Medium | monolithic additions to `rt_game3d.c` | focused files, source headers, health audit update only with justification |

## Plan-specific go/no-go decisions

### Plan 03 API freeze

Do not freeze FrameDriver3D until small spikes reproduce all three loop shapes:

- bowling: multiple gameplay fixed steps before one render;
- Ridgebound: terrain before/alongside scene, water after opaque scene, overlay;
- Ashfall: fixed player/AI simulation, custom viewmodel/world passes, overlay,
  frame capture without present.

If any requires calling World3D internals or polling twice, revise the driver.

### Plan 04 ownership freeze

Do not add a generic `Track(obj)` API unless runtime type validation can select
the exact public teardown operation safely. Typed methods are verbose but safer
than guessing object layout from `obj`.

### Plan 05 phase freeze

Use actual render requirements, not a generic enum assumption. Terrain, opaque
vegetation, transparent water, sky, and post-scene effects may need distinct
locations. If the existing `RenderPass` constants cannot express this without
breaking compatibility, extend through ADR rather than overloading meanings.

### Plan 10 event freeze

The first event stream version may expose only event families with stable
World3D identity. Do not include UI/application events merely to make a single
global bus. Ashfall's game-specific events remain game-side.

### Plan 14 format freeze

Reuse the existing World3D snapshot bytes/file. The composed save manifest
coordinates pieces; it does not deserialize and rewrite the world format.

## Escalation paths

The implementation owner must stop and open an ADR/design review when:

- a new cross-layer reference from Graphics3D into Game/UI/IO is required;
- a public callback would execute from a worker;
- a proposed result cannot be represented with qualified runtime types;
- disabled-graphics behavior would differ from documented recoverable behavior;
- a new save/asset format lacks bounded validation;
- a change needs raw `_WIN32`, `__APPLE__`, or `__linux__` outside an approved
  adapter;
- a demo requires a breaking change to adopt the abstraction;
- performance budgets cannot be met without changing a public semantic.

## Risk review cadence

At the end of each program wave:

1. update this register with closed/new risks;
2. inspect surface dump growth and class IDs;
3. compare actual file overlap with the table;
4. run the wave's cross-demo spike or migration gate;
5. decide whether downstream API drafts remain valid;
6. update plan 20 with platform/backend evidence.

