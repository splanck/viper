# Implementation Roadmap

This roadmap orders the runtime API overhaul into slices that can land without
losing control of the surface.

## Phase 0: Freeze The Baseline

Exit criteria:

- checked-in current API snapshot;
- audit command can reproduce the snapshot;
- waiver file exists;
- ADR plan is written for ABI/signature/catalog changes.

Work:

- add snapshot fixture from `--dump-runtime-api`;
- add non-failing report mode for new audits;
- list current waivers for aliases, bare objects, stubs, docs anchors, and
  failure style.

## Phase 1: Metadata Foundation

Exit criteria:

- stability, migration target, capability, fallibility, ownership, and docs
  metadata are declarative;
- CLI dump no longer relies primarily on name heuristics;
- docs anchors have a generated-reference path.

Work:

- extend `runtime.def` or add sidecar metadata;
- update `rtgen`;
- update `zanna --dump-runtime-api`;
- add schema tests.

## Phase 2: Audit Gates

Exit criteria:

- all planned audits exist in warn/report mode;
- P0 audits can run in fail mode;
- CI path is identified.

Work:

- duplicate C-symbol/signature audit;
- method collision audit;
- docs anchor audit;
- typed handle audit;
- ownership audit;
- fallibility audit;
- capability/stub audit;
- naming audit.

## Phase 3: P0 API Corrections

Exit criteria:

- no unsafe/internal duplicate stable public rows;
- no public trap mutation under ordinary `Zanna.Error`;
- `CodeEditor` boolean getter mismatch fixed;
- disabled stub fake-success behavior removed or explicitly marked preview.

Work:

- canonicalize `Runtime.Unsafe`, `Memory`, `Error`, `Diagnostics`;
- fix boolean getter signatures;
- update tests/docs/examples affected by these changes;
- add migration metadata where compatibility rows remain.

## Phase 4: Naming And Duplicate Cleanup

Exit criteria:

- no duplicate stable aliases;
- legacy rows have migration targets;
- generated docs and completions prefer canonical names.

Work:

- Bits, BloomFilter, collections, BinaryBuffer, Vec2/Vec3, formatting;
- key constants;
- crypto acronym casing and legacy weak algorithms;
- namespace facade duplicates.

## Phase 5: Types, Ownership, And Units

Exit criteria:

- stable concrete handles are typed;
- stable object/string returns have ownership;
- unit metadata exists for stable numeric time, coordinate, angle, color, and
  size parameters.

Work:

- type handles by domain;
- annotate ownership;
- introduce value objects/options where needed;
- add enum/domain metadata.

## Phase 6: Failure Model Migration

Exit criteria:

- stable parse/load/open/connect/decrypt APIs have canonical `Result` forms;
- stable lookup/try/pop APIs use `Option` where absence is normal;
- side-channel diagnostics are legacy or scoped telemetry.

Work:

- parsers and serializers;
- IO/system/process/PTY;
- crypto/TLS/network;
- collections/channels/futures;
- game/graphics asset loading.

## Phase 7: Domain Reshaping

Exit criteria:

- namespace ownership decisions are reflected in canonical APIs;
- large classes have grouped APIs or documented waivers;
- duplicated domain roots are legacy or resolved.

Work:

- Text/Data split;
- Graphics/Graphics2D/Graphics3D cleanup;
- Game/Game2D/Game3D cleanup;
- Audio/Sound decision;
- Input key domains;
- tooling capability APIs.

## Phase 8: Docs, Examples, Release Freeze

Exit criteria:

- generated reference complete;
- conceptual docs updated;
- examples and demos use canonical APIs;
- release checklist passes;
- compatibility/legacy policy is documented.

Work:

- generated reference publish path;
- migration guide;
- capability matrix;
- result/error guide;
- final API snapshot diff review.

