# Decision log

Cross-cutting decisions that must be closed or waived before the affected work
moves from design to broad implementation. See `../README.md` §8.

## Open decisions

| ID | Decision | Source | Status | Options / default | Owner | Due before | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| D-DET | Determinism-under-threads contract | `README.md` §8, `runtime-changes.md` §1 | todo | **Default:** sim deterministically scheduled; workers do load/decode/cull/bake/transcode only, ordered merge |  | Phase 0/1 |  | `runFrames` parity gate (GATE-002); ADR if VM/IL touched |
| D-JOB | Job-system shape | `README.md` §8 | todo | Thread-pool + work-stealing vs. per-domain queues; on `rt_platform.h` |  | Phase 1 |  | No raw platform macros |
| D-IDX | Spatial-index choice + scope | `README.md` §8 | todo | Loose octree vs. BVH vs. grid; unify cull+query+broadphase or sibling for physics |  | Phase 3 |  |  |
| D-ORIG | Floating-origin strategy | `README.md` §8 | todo | Periodic active-scene rebase vs. per-cell local origin + camera-relative upload |  | Phase 2 |  | Interacts with `double` nodes / `float` GPU |
| D-STREAM | Streaming granularity + container | `README.md` §8, `runtime-changes.md` §5 | todo | Cell/tile sizes; extend VSCN vs. tiled side-format (no new top-level format) |  | Phase 5 |  |  |
| D-LIGHT | Lighting path | `README.md` §8 | todo | **Default:** forward+ (clustered, keep forward shaders) vs. deferred (rewrite) |  | Phase 7 |  | Lean forward+ to honor "no rewrite" |
| D-TEX | Texture-compression scope | `runtime-changes.md` §11 | todo | Decode/transcode-only vs. also encode; which formats per backend |  | Phase 11 |  | From-scratch; descope encode if needed |
| D-SEQ | Sequencing override | `roadmap.md` Risks | todo | **Default:** dependency order; allow pulling Phase 8/9 earlier for physics-/NPC-first games |  | Phase 0 |  | Only noted hard edges are fixed |
| D-MS | v0.3.x milestone scope | `README.md` §8 | todo | Phases 0–5 = "small streamed world"; 6–12 raise ceiling/fidelity |  | Phase 0 |  | User owns version labels |
| D-CB | Carryover CO-2 ownership | `carryover.md` CO-2 | todo | Implement VM trampoline here vs. delegate to a VM plan vs. re-waive |  | Phase C |  | Event-buffer polling is the fallback |

## Closed decisions

| ID | Decision | Outcome | Proof / link | Date | Notes |
|---|---|---|---|---|---|
| — | (none yet) |  |  |  | Move closed decisions here with proof; never delete |

## Decision update rules

- Do not delete closed decisions; move them to "Closed decisions" with proof.
- If an outcome changes, add a new row referencing the old decision.
- Any `blocked` tracker item must name the blocking decision ID.
