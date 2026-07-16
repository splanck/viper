# Plan 16 — Documentation, Starters, and API Navigation

## Outcome

Create a clear, tested learning path from a minimal World3D game to a custom
fixed-step application. Explain the boundary between Game3D and Graphics3D,
when to use Run versus FrameDriver/GameBase3D, ownership, render phases,
queries/events, quality/assets, testing, and migration. Keep all snippets and
starters executable and packageable.

## Problem statement

The Game3D guide is extensive and accurate for individual features, but the
reviewed games reveal a discoverability problem: users must infer the complete
application pattern and the point at which to drop to manual phases. The
current GameBase3D section describes a variable-step, fixed-render-order helper
that this program upgrades. Public surface scale—more than one hundred 3D
classes across Game3D and Graphics3D—also makes task-based navigation essential.

## Dependencies

Documentation can begin with current behavior, but finalization follows the
approved APIs from plans 03–15. It must never document draft names before their
ADR lands. Plan 01–02 defect notes must reflect actual closure status.

No ADR is required for ordinary docs/examples. Changes to normative
`docs/il/il-guide.md#reference`, workflows, or public C ABI require their normal
ADR path.

## Scope

In scope:

- Game3D quickstart and task-based decision guide;
- frame-order/ownership/lifetime diagrams or tables;
- three starter tiers;
- asset, quality, environment, input, UI, query/event, combat, save, and test
  recipes;
- migration guidance from raw Graphics3D and current manual World3D loops;
- generated/public API cross-links and snippet tests;
- package dry-run and docs freshness checks.

Out of scope:

- documenting unimplemented draft APIs as available;
- replacing low-level Graphics3D reference pages;
- marketing copy;
- hiding advanced escape hatches;
- hand-editing generated files owned by tools.

## Primary owners

- `docs/viperlib/graphics/game3d.md`
- `docs/viperlib/graphics/rendering3d.md`, `physics3d.md`, `scene.md` only for
  cross-links/changed low-level contracts;
- `docs/internals/graphics3d-architecture.md` for frame/ownership architecture;
- `docs/viperlib/game/README.md`, UI/core/persistence pages;
- `docs/viperlib/io/assets.md`;
- `docs/internals/testing.md`, `docs/codemap*`, and generated runtime docs through their
  supported generator;
- `examples/3d/game3d_hello.zia`, `game3d_starter/`, `game3d_scenes/`, plus a
  custom-phases starter if needed;
- docs snippet fixtures and package tests.

## Information architecture

### Landing page decision tree

Answer these questions immediately:

1. Need a minimal animated scene? Use `World3D.Run`.
2. Need fixed simulation but standard scene/effects/overlay? Use
   `World3D.RunFixed` when callbacks fit.
3. Need custom terrain/water/viewmodel/capture phases? Use FrameDriver3D manual
   phases.
4. Need scene/menu/UI/resource lifecycle? Use incubating GameBase3D over
   FrameDriver/SceneScope.
5. Need low-level rendering/physics control? Use Graphics3D escape hatches and
   register/associate resources where Game3D identity is desired.

### Concept chapters

Organize around a real game lifecycle:

- create world and capabilities;
- assets and quality;
- scene scope and environment;
- entities/physics/controllers;
- fixed update and frame phases;
- queries/events/combat/effects/audio;
- overlay/UI/application scenes;
- save/load;
- deterministic testing and packaging;
- diagnostics/performance/disabled backend.

Keep exhaustive member tables linked rather than repeating them in every
chapter.

## Starter tiers

### Tier 1 — Hello World3D

Target roughly one screen of code:

- construct world;
- lighting/material/prefab;
- spawn entity;
- simple Run callback or RunFrames for test;
- no framework concepts.

### Tier 2 — Small game

Show:

- Action input preset;
- CharacterMotor or existing controller;
- quality resolve/apply;
- named asset catalog with procedural fallback;
- one UI HUD widget/overlay;
- standard fixed loop when sufficient;
- deterministic small probe.

### Tier 3 — Custom application

Show:

- FrameDriver3D reserve/update/commit;
- SceneScope3D;
- environment registry;
- before/after scene custom draw;
- query/event and pooled cue;
- GameBase3D scene transition/pause;
- SaveGame3D slot;
- scenario harness.

Do not turn one starter into a feature dump. Split modules and cross-link.

## Implementation sequence

### Phase 0 — Documentation audit

1. Inventory every Game3D/Graphics3D page and starter.
2. Compare public runtime dump to docs class/member coverage.
3. Mark stale descriptions of current GameBase3D, frame order, environment,
   queries, persistence, and quality.
4. Identify generated versus hand-authored ownership.
5. Run current docs snippet/package tests and record failures.

### Phase 1 — Current-pattern report section

Add a concise "Building a 3D game today" chapter based on plan 00, including
the simple/standard/custom paths and the Game3D/Graphics3D boundary. This can
land before new APIs if it describes only current behavior.

### Phase 2 — Frame and ownership documentation

After plans 03–05 land:

- document driver state machine and exact valid sequence;
- show fixed versus frame/unscaled time;
- document scene scope ownership and stale handles;
- show environment phases and manual escape hatches;
- include illegal-order diagnostics/troubleshooting.

Use one small table/diagram that matches tests; do not maintain competing frame
orders in multiple pages without a shared source.

### Phase 3 — System recipes

After each API lands, add a tested focused recipe:

- intent motor independent from camera/input;
- camera base/modifier pipeline;
- source/package asset resolution;
- authored quality profile application;
- entity-aware query and event lifetime;
- named pooled effect/audio cue;
- ranged hit and feedback consumer;
- atomic versioned save slot;
- deterministic scenario.

Each recipe states ownership, time domain, error/fallback, and teardown.

### Phase 4 — Application/UI guidance

Document that GameBase3D is example-library incubation unless promoted. Link
existing `Viper.Game.UI` and `Viper.GUI` widget references. Show title,
gameplay HUD, pause modal, resize, and transition without inventing new widgets.

### Phase 5 — Starter updates

Update tiers in dependency order. Every starter gets:

- a README explaining the intended abstraction level;
- source headers following repository convention;
- a deterministic smoke path/frame limit;
- `viper.project` asset/package declarations;
- a CTest check/run/package dry run;
- no repository-CWD-only assumptions.

### Phase 6 — API navigation/generated docs

1. Use `--dump-runtime-api` and supported doc generator to update inventories.
2. Add task-to-class tables and namespace ownership links.
3. Keep qualified class names and exact casing/signatures.
4. Add See Also links among Game3D, rendering, physics, IO assets, UI,
   persistence, and testing.
5. Run link/path validation and generated-doc freshness checks.

### Phase 7 — Migration/troubleshooting guide

Document safe migration:

- raw Canvas/Scene/Physics -> World3D host;
- manual accumulator -> FrameDriver;
- arrays/manual unload -> SceneScope;
- manual terrain/water draw -> EnvironmentStack;
- body registries -> WorldHit;
- custom pools -> named cues;
- flat/custom saves -> SaveGame composition.

Include "keep custom code when" guidance. The goal is not maximal runtime use;
it is removal of proven cross-game boilerplate.

Troubleshooting includes backend capability fallback, overlay finalization,
asset working directory/package paths, illegal phase transitions, stale
entities, event lifetime, pool drops, and corrupt save diagnostics.

### Phase 8 — Accuracy review

For every snippet:

1. copy it into or source it from a compiled fixture;
2. run `viper check` and relevant execution test;
3. verify qualified names against runtime dump;
4. test source-tree and packaged working directories where assets exist;
5. verify docs do not promise an unsupported backend feature;
6. date/update `last-verified` metadata according to docs convention.

## Validation

Run docs snippet fixtures, `viper check` on all starters, Game3D/Graphics3D docs
probes, package dry runs, link/path/freshness checks, runtime API surface audits,
examples label/smoke, and full build scripts.

## Acceptance criteria

- A new user can choose Run, FrameDriver, GameBase3D, or Graphics3D from one
  decision guide.
- The three starter tiers compile, run deterministically, and package.
- Every new public class/member has accurate docs and a tested recipe.
- Frame order, ownership, lifetime, error, and fallback are explicit.
- Game.UI is reused and correctly linked.
- Generated docs match the live runtime dump; no draft API is documented as
  shipped.

## Stop conditions

Stop a docs section if its owning API/ADR is unsettled or tests cannot compile
the example. Mark future work in the plan, not the public guide.

## Handoff evidence

Provide doc inventory/diff, runtime surface coverage comparison, starter test
outputs, package dry runs, link/freshness results, and a list of any intentionally
undocumented internal APIs.

