# Plan 20 — Roadmap, Release Gates, and Rollout

## Outcome

Land the program in dependency order with explicit ADR/API freezes, continuous
green gates, cross-platform/backend closure, reversible demo migrations, and a
single final compatibility audit. This plan is the program tracker; no
workstream is complete merely because its code merged.

## Dependencies

This plan depends on the evidence, acceptance criteria, and handoff artifacts
from plans 00–19 plus all three shared appendices. It does not authorize
skipping a dependency to meet a calendar target. It begins as the live tracker
at M0 and finishes only after every required workstream and platform/backend
cell is complete.

## Governing principles

- Correctness defects land before frameworks that depend on them.
- Public API freezes only after representative spikes in all affected games.
- Runtime foundations land before demo cleanup.
- Every commit is buildable/testable; do not merge a multi-plan flag day.
- Existing public surfaces remain compatible and low-level escape hatches stay.
- Software is correctness baseline; Metal/D3D11/OpenGL all close before release.
- Demo migrations prove abstractions; they do not define runtime internals by
  special case.
- Historical plan directories deleted during the 2026-07 documentation
  reorganization (now committed) remain deleted unless their owner explicitly
  restores them. This package does not resurrect them.

## Milestone sequence

### M0 — Baseline freeze

Plans: 00 and appendices.

Deliverables:

- fresh runtime API dump and test inventory;
- all three demo baseline gate matrices;
- backend/platform availability matrix;
- record of any unrelated uncommitted changes and file-overlap ownership;
- assigned ADR and implementation owners;
- risk register reviewed.

Exit gate: baseline is reproducible on the integration branch and unrelated
failures are documented, not waived.

### M1 — Overlay correctness

Plans: 01–02.

Recommended landing units:

1. fail-before alpha fixture;
2. alpha root-cause fix and backend tests;
3. fail-before AA identity/lifetime fixture;
4. Metal fix and parity tests;
5. known-issue/docs status update.

Exit gate: original repros pass; software/Metal tests green; D3D11/OpenGL
closure scheduled before M4 if not locally available. No public API delta.

### M2 — Orchestration and identity foundation

Plans: 03, 04, 08, 10.

Order:

1. FrameDriver three-game mock spikes;
2. FrameDriver ADR/API/implementation;
3. SceneScope lifecycle inventory/ADR/implementation;
4. Asset catalog/resolver ADR/implementation (may run parallel if files do not
   overlap);
5. entity-aware queries first, unified events second.

Exit gate:

- bowling/Ridgebound/Ashfall loop spikes use FrameDriver;
- three ownership spikes use SceneScope;
- source/package asset spikes pass;
- body/query/event identity spikes pass;
- runtime surface/disabled graphics/full graphics3d gates green.

### M3 — World/application composition

Plans: 05–07, 09, 11–12.

Order:

1. environment phase proof/ADR/registry;
2. quality policy inventory/ADR and environment integration;
3. motor shared-intent extraction then class;
4. camera shared-helper extraction then modifier rig;
5. effect/audio reset inventory then pools;
6. GameBase3D incubation over the landed foundations.

Motor/camera can develop in parallel after plan 10 if they avoid common
controller files or coordinate helper extraction. Effect pools wait for quality
field freeze. GameBase3D must wait for FrameDriver and scopes but can prototype
earlier with mocks.

Exit gate: all three application spikes fit without callbacks/private access;
Ridgebound/Ashfall environment captures match; warmed pool allocation gates
pass; existing controllers/presets/Run loops remain compatible.

### M4 — Gameplay, persistence, and testability

Plans: 13–16.

Order:

1. hurt-region query metadata and ballistics core;
2. save transaction design and existing persistence refactor;
3. scenario harness using stable driver/query APIs;
4. documentation/starter update only for landed surfaces.

Exit gate: Ashfall ballistics spike matches, save fault injection/fuzz passes,
representative probes in all games use harness, and three starter tiers compile,
run, and package.

### M5 — Demo migrations

Plans: 17–19.

Recommended order:

1. Ridgebound first for an already-World3D environment/application proof;
2. 3dbowling second for raw-physics host migration and release-gate rigor;
3. Ashfall last for full FPS/custom-render/ballistics stress.

This ordering may change based on owner availability, but never run overlapping
runtime API changes from demo migrations. Runtime gaps return to their owning
plan/ADR; migrations do not patch ad hoc public APIs in game commits.

Exit gate: 27 bowling gates, all Ridgebound primary/specialized gates, and 14
Ashfall probes pass with before/after evidence and package modes.

### M6 — Cross-platform release closure

Plans: 16 and 20 final pass.

Deliverables:

- clean macOS/Metal, Windows/D3D11, Linux/OpenGL/software full build scripts;
- disabled-graphics/no-audio variants;
- sanitizer/fuzz/parser/lifetime lanes;
- runtime surface/API/name/leaf/completeness audits;
- source health/format/warnings/platform policy;
- docs/generated freshness, snippets, links, starters, packaging;
- final demo matrices and performance comparison;
- compatibility and deprecation statement;
- updated risk register with explicit follow-ups.

Exit gate: no pending required backend/platform cell and no silent waiver.

## ADR program

Do not reserve ADR numbers in these plans. At implementation time use the next
available number from `docs/adr/README.md` (the sequence stood at 0109 on
2026-07-16, and two older records were renumbered to 0104/0105 in the
2026-07 documentation reorganization). Likely ADR topics may be combined only
when they form one coherent contract:

| Topic | Plans | Minimum decision content |
|---|---|---|
| Frame scheduling/phases | 03 | poll/time authority, reserve/commit, phase state, compatibility |
| Resource scopes | 04 | typed ownership, nesting, release order, world invalidation |
| Environment registry | 05 | low-level type dependencies, render phases, streamed terrain exclusion |
| Motor/camera composition | 06–07 | shared controller helpers, tick order, intent/modifier semantics |
| Asset catalog | 08 | namespace, resolver delegation, search/security/cache policy |
| Quality profile | 09 | requested/active/fallback and authored post-FX policy |
| World queries/events | 10 | result/event classes, hurt-region index, lifetime/order/bounds |
| Feedback pools | 11 | reset, pool/voice eviction, quality/scope integration |
| Ballistics | 13 | damage spec, penetration/radial order, events, bounds |
| Save composition | 14 | format, atomic generation commit, validation/migration |

GameBase3D/scenario/docs remain incubation unless separately promoted.

Each ADR must cite current source/tests and update the API register. A change to
the approved signature requires dependent-plan review before merge.

## Integration branch discipline

For each landing unit:

1. rebase/update against the current integration branch;
2. inspect `git status --short` and preserve unrelated changes;
3. run fail-before focused test;
4. implement the smallest acceptance slice;
5. format and run focused tests;
6. run surface audits for public changes;
7. run graphics3d label and platform policy;
8. update plan tracker/evidence links;
9. use a conventional commit with no AI attribution;
10. do not begin dependent cleanup until CI/integration evidence is green.

Avoid a single commit that adds class, migrates three demos, updates generated
docs, and changes backend behavior. Separating foundation, registration, tests,
docs, and migrations makes regression isolation possible while keeping every
commit green.

## Compatibility gates

Before each public API merge:

- pre/post `--dump-runtime-api` diff contains only approved additions;
- no existing class ID/signature/name changes;
- qualified types work in registry and all frontends;
- real and disabled symbols link;
- VM and native/AOT calls behave consistently;
- existing tests/examples compile without source changes unless the change is
  coordinated example-library incubation;
- current Run/RunFixed/manual phase behavior remains;
- low-level Graphics3D queries/draws remain available;
- docs state ownership/error/lifetime/time domain.

## Cross-platform/backend release matrix

Maintain a live table in the implementation tracker:

| Gate | macOS/Metal | Windows/D3D11 | Linux/OpenGL | Software |
|---|---|---|---|---|
| overlay alpha | required | required | required | required |
| AA texture identity | required | parity | parity | required |
| frame/scope/query API | required | required | required | required/disabled |
| environment ordering | required | required | required | required |
| quality fallback | required | required | required | required |
| effects/audio | required | required | required | no-audio/fallback |
| demo visual gates | required | required release smoke | required release smoke | structural/headless |
| full platform script | required | required | required | included lanes |

`parity` means the same defensive test must pass even if the original defect was
Metal-only.

## Performance release gates

Collect before/after on the same machine/backend/configuration:

- FrameDriver steady-state allocations/time;
- environment registered versus manual draw counts/time;
- query/event throughput and allocations;
- pooled versus legacy effect/audio burst allocations;
- scene scope release complexity at representative and 10k resource counts;
- save file size/time and load validation time;
- each demo's existing perf/stress probe.

Default rule: investigate any >10% regression; do not automatically waive it.
Semantic hot paths must meet their plan-specific zero-allocation/bounded goals.

## Rollback strategy

- New abstractions are opt-in until demo migration completes.
- Keep game adapters per slice so a failing adoption can revert without
  reverting independent runtime correctness work.
- Do not delete manual game code in the same commit that first switches to a
  runtime abstraction; run side-by-side trace comparison first, then delete in
  a later green commit (never two active simulations in production).
- Existing public methods remain; compatibility wrappers can route to new
  internals.
- On backend failure, disable only an optional feature through documented
  capability fallback; do not silently change core alpha/phase semantics.

## Program tracker template

For every plan maintain:

```text
Plan:
Owner:
ADR:
Dependencies green:
Fail-before evidence:
Implementation commits:
Focused tests:
Surface diff reviewed:
Software:
Metal:
D3D11:
OpenGL:
Disabled graphics/no audio:
Performance/allocation:
Docs/examples/package:
Demo adoption evidence:
Risks/follow-ups:
Status: not started | design | implementing | backend closure | complete
```

No row becomes `complete` with a required blank field.

## Stop conditions

Stop a milestone promotion when a required ADR is missing, a fail-before test
was never demonstrated, an API has not passed its representative game spikes,
a required backend/platform cell is unowned, a demo release gate regresses, or
a Critical/High risk has no approved mitigation. Keep the program at its
current milestone and repair the evidence; do not reclassify a required gate as
optional merely to mark progress.

## Acceptance criteria

The program is complete only when the README program-level definition of done
and all individual acceptance criteria are met, all required platform/backend
cells are green, all three demo migrations retain their release gates, public
surface/docs/package audits pass, and the final risk register contains no open
Critical/High item without a separately approved follow-up milestone.

## Handoff evidence

Produce a release report containing:

- ADR/API summary;
- public runtime surface diff;
- root causes for plans 01–02;
- platform/backend/full-build matrices;
- unit/fixture/demo probe matrices;
- before/after performance/allocation data;
- demo adopted-versus-retained architecture tables;
- starter/docs/package evidence;
- compatibility/deprecation statement;
- closed/open risk list and named follow-up owners.
