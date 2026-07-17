# Graphics3D/Game3D C Runtime and Public API Review

Date: 2026-07-13  
Scope: `src/runtime/graphics/3d`, its public C declarations, the canonical
`Zanna.Graphics3D.*` / `Zanna.Game3D.*` registry surface, generated runtime
documentation, and the VM/native binding path.

## Executive summary

The review found correctness defects in ownership, cache invalidation, query
capacity, collision dispatch, navigation optimality, importer limits, scene
depth handling, async OOM behavior, memory budgeting, thread-safe lazy state,
and BC6H integer arithmetic. It also found that the language-facing registry
and the C implementation boundary lacked a complete machine-readable binding
manifest and explicit return contracts.

All 32 recommendations below have been implemented or, where the requested
optimization already existed, verified and covered by the review. The most
important behavioral changes are:

- transient runtime strings are now released across the glTF, FBX, Model3D,
  Game3D, Canvas3D, and VSCN paths;
- physics queries honor the documented 16–4096 result cap, return valid empty
  lists, allocate one packed list block, and lazily box individual hits;
- mutable meshes and compound colliders invalidate physics and scene spatial
  caches;
- unsupported collider pairs fail closed instead of treating AABB overlap as a
  real contact, while compound pairs can retain multiple leaf contacts;
- off-mesh links can no longer make A* terminate on a suboptimal route;
- FBX imports preserve up to the Skeleton3D limit of 1,024 bones;
- VSCN save/load use bounded iterative hierarchy walks with the same 98-node
  format limit;
- async streaming survives commit-wrapper/queue failure without trapping,
  losing payload ownership, or leaking its retained stream;
- render-target reservations cover their maximum lazy CPU/GPU footprint;
- shared lazy state is synchronized and BC6H arithmetic is portable under the
  C abstract machine;
- Canvas3D offers allocation-reusing screenshot APIs;
- `zanna --dump-runtime-api` schema 4 is a complete live registry-to-C binding
  catalog for the 3D surface.

The current live registry contains 7,355 public functions and 513 classes. The
reviewed 3D subset contains 2,067 functions, 125 classes, 735 properties, and
1,120 methods. Its reviewed ABI fingerprint is
`0xfdd498b594603697`.

## Boundary decision

The public programming surface is the canonical runtime registry rooted at
`src/il/runtime/runtime.def`. The `rt_*` functions are Zanna's internal
embedding ABI for generated code, VM adapters, tests, and owned platform
backends; they are not a separately versioned C SDK. Runtime object payloads
remain opaque. This decision and its compatibility consequences are recorded
in `docs/adr/0102-graphics3d-runtime-boundary-and-contract-manifest.md`.

`zanna --dump-runtime-api` schema 4 now reports:

- `public_boundary: "registry"`;
- `c_abi_status: "internal-embedding"`;
- every function's backing `c_symbol`;
- resolved C symbols for non-empty constructors, getters, setters, and methods;
- explicit 3D return nullability, ownership, and fallibility;
- `contract_source: "three-d-boundary-policy"` on all reviewed 3D rows.

No public 3D function or non-empty 3D class-member binding is missing a C
symbol. Four legacy hand-authored, non-3D bridge rows remain intentionally
symbol-less and are outside this manifest.

## Findings and recommendations

Priority meanings: P0 is a correctness, memory-safety, false-result, or
resource-limit defect; P1 is significant correctness/performance/API debt; P2
is hardening or a lower-frequency optimization.

| # | Priority | Finding and recommendation | Resolution | Evidence |
|---:|:---:|---|---|---|
| 1 | P0 | Balance ownership of temporary `rt_const_cstr`, returned string, and JSON-key handles. | Implemented reusable helpers and explicit releases throughout glTF, FBX, Model3D, Game3D, Canvas3D, animation, and VSCN paths. Removed double-retains such as `rt_string_ref(rt_const_cstr(...))`. | Model, scene, Game3D, Canvas, and sanitizer coverage. |
| 2 | P0 | Remove the `PhysicsHitList3D` 256-item accessor ceiling that contradicted the public 4096 configuration limit. | Added an allocation-bound count, clamped only to `PH3D_QUERY_HITS_MAX`, and verified a 260-hit list after setting the cap to 4096. | `test_world_overlap_hit_list_reports_truncation`. |
| 3 | P0 | Invalidate the physics query broadphase when attached mesh or compound geometry changes without a body transform change. | Added process-wide atomic mesh/collider geometry epochs to the query-cache key. | Compound- and mesh-mutation broadphase regression tests. |
| 4 | P0 | Invalidate/refit Scene3D's BVH after in-place Mesh3D geometry mutation. | Scene spatial indices snapshot the global mesh epoch and refit before reuse when it changes. | `test_node_aabb_refreshes_after_mesh_mutation` and spatial-index tests. |
| 5 | P1 | Canonicalize long surface names before both insert and lookup so truncation does not create duplicate or unreachable IDs. | Registration and `IdOf` now compare the same 63-byte canonical representation; `NameOf` returns a fresh managed string. | `test_game3d_surfaces_probe.zia` long-prefix round trip. |
| 6 | P1 | Align the FBX skeleton importer with Skeleton3D's 1,024-bone capacity instead of silently stopping at 256. | FBX collection now uses `VGFX3D_MAX_SKELETON_BONES`; draw-palette limits remain a rendering concern rather than an import-data limit. | Generated 257-bone FBX regression. |
| 7 | P0 | Never accept unsupported collider pairs merely because their AABBs overlap. | Removed the terminal AABB-as-contact fallback. Mesh/mesh, mesh/heightfield, convex/heightfield, and heightfield/heightfield pairs now fail closed until a real narrow phase exists. | `test_unsupported_mesh_pair_rejects_aabb_false_positive`. |
| 8 | P0 | Keep the A* heuristic admissible when authored off-mesh links are cheaper than Euclidean separation. | Searches use the existing Euclidean heuristic for triangle-only graphs and Dijkstra (`h=0`) when any off-mesh link exists. | Adversarial suboptimal-goal regression with expected cost 3 rather than 100. |
| 9 | P1 | Make Scene3D hierarchy traversal independent of the C call stack and make save/load depth limits identical. | Replaced recursive VSCN save/load walks with explicit stacks and a shared 98-node format limit derived from the JSON nesting budget. | 98-level round trip succeeds; level 99 save is rejected. |
| 10 | P1 | Bound compound-collider graph traversal and reject pathological nesting/cycles. | Public attachment rejects transitive cycles and nesting beyond 64 compound levels; bounds and collision dispatch have defensive depth caps. | Cycle and excessive-nesting physics regressions. |
| 11 | P0 | Do not leak a retained stream or trap when async staging cannot allocate a commit queue wrapper/node. | Added status-returning internal queue enqueue, caller-ownership semantics, and an intrusive allocation-free emergency handoff drained on the main thread. | Commit-queue fault test and end-to-end async streaming OOM test. |
| 12 | P0 | Enforce RenderTarget3D's budget against memory it can actually own, including lazy mirrors. | Reservations now use 16 bytes/texel for LDR and 36 bytes/texel for HDR, covering native color/depth and every lazy CPU mirror. | LDR/HDR reservation assertions in Canvas3D tests. |
| 13 | P0 | Make screenshot/readback validation explicit and null-safe. | Centralized checked Canvas/Pixels validation and same-size/layout checks in `canvas3d_screenshot_into`; invalid/null inputs return status without dereference. | Null, wrong-size, GPU, software, RTT, and finalized-capture tests. |
| 14 | P1 | Preserve more than the first touching leaf when a compound collider contacts another body. | A bounded leaf walk adds distinct points to the body-pair manifold up to `PH3D_MAX_MANIFOLD_POINTS`. | `test_compound_collider_collects_multiple_leaf_contacts`. |
| 15 | P1 | Remove the C data race in the software backend's lazy `ZANNA_3D_DEBUG` cache. | Replaced plain concurrent reads/writes with a portable atomic once-state machine. | Focused concurrency lane/TSan coverage. |
| 16 | P1 | Make BRDF LUT first use exactly-once and safe under concurrent backend initialization. | Added an acquire/release atomic once gate; only a fully built immutable table is published. | Sixteen-thread concurrent first-use regression plus reference-value tests. |
| 17 | P0 | Eliminate undefined/implementation-defined signed shifts and overflow in BC6H decode. | Added checked bit reads, unsigned masks, widened multiply/interpolation, portable sign extension and floor division, and fail-closed reserved modes. | All 14 BC6H modes, signed and unsigned, are deterministic and finite; UBSan coverage. |
| 18 | P1 | Publish explicit return nullability for the whole 3D API rather than leaving object results ambiguous. | Schema 4 conservatively marks 3D object/raw-pointer results nullable and primitive/string/sequence/value results according to policy. | Live JSON audit and Agent CLI contract tests. |
| 19 | P1 | Return a valid empty collection for valid zero-result multi-hit physics queries. | `RaycastAll` and `Overlap*` construct zero-count `PhysicsHitList3D` objects; invalid arguments still return null. | LayerMask.None empty-list regressions. |
| 20 | P1 | Publish usable return ownership metadata. | 3D values are `value`, raw pointers `borrowed`, object/string/sequence results `managed`, and void `none`. | Live JSON audit reports no unknown 3D function/method ownership. |
| 21 | P1 | Distinguish status-returning, nullable, and infallible operations. | Boolean `Try*` rows report `status`; nullable results report `nullable`; remaining reviewed rows follow the explicit policy. | Canvas TryCopy/Screenshot CLI assertions and full dump audit. |
| 22 | P1 | Choose one public boundary instead of implicitly promising both registry and header surfaces. | Registry is public; the C layer is documented as internal embedding ABI. | ADR 0102 and schema-4 top-level fields. |
| 23 | P1 | Keep runtime object layouts out of public C declarations. | Moved `rt_body3d_kinematics` to a private physics header; offset static assertions still guard solver/core agreement. Public object parameters remain opaque handles. | Normal compilation plus manifest test. |
| 24 | P1 | Add a complete, deterministic 3D ABI drift guard. | New contract test walks every public 3D descriptor/class binding, validates signatures/handlers/C symbols, guards exact counts, and fingerprints the full binding surface. | `test_graphics3d_runtime_manifest`, hash `0xfdd498b594603697`. |
| 25 | P2 | Generate public API documentation from the canonical registry and detect drift. | Regenerated schema/reference docs; `rtgen --docs --check` is clean. Added screenshot reuse semantics to conceptual documentation. | `docs/generated/runtime/graphics3d.md` and docs check. |
| 26 | P1 | Remove raw `_WIN32` from shared Game3D code. | Surface registry now uses `RT_PLATFORM_WINDOWS`; platform-specific code remains inside the approved adapter branch. | Strict changed-file platform-policy lint. |
| 27 | P1 | Avoid N+1 managed allocations for multi-hit physics queries. | One object allocation now contains pointer slots plus packed raw hits; a `PhysicsHit3D` box is created once, lazily, only when indexed. Raw body/collider references remain retained. | Lazy-box and sorted-hit assertions; sanitizer coverage. |
| 28 | P1 | Reuse A* workspace instead of allocating four arrays on every path query. | NavMesh3D owns grow-only g-cost, parent, closed, and heap buffers, serialized per navmesh; `LastPathCost` is atomic. | Eight-thread, 800-query reuse regression. |
| 29 | P2 | Prevent NavAgent crowd avoidance from degrading to an all-pairs scan. | Verified the runtime already uses a spatial hash/grid candidate partition and bounded neighbor selection; no duplicate implementation was added. | Source audit and existing crowd/navigation suites. |
| 30 | P2 | Replace linear texture-cache lookup on hot hits. | D3D11/OpenGL contexts now keep direct-mapped, identity-validated index hints with safe linear fallback and rebuild hints after compaction/pruning. | Shared backend/cache tests and source-level validation of prune/rebuild paths. |
| 31 | P1 | Add an allocation-reusing screenshot path for capture loops. | Added `TryCopyScreenshotTo(Pixels)` and `TryCopyScreenshotFinalTo(Pixels)`, destination generation updates, and canvas-owned grow-only GPU staging. Existing allocating APIs remain compatible. | Repeated capture proves destination and staging pointer reuse. |
| 32 | P0 | Add regression, fault, sanitizer, platform, and full-build coverage for the boundary cases above. | Added focused tests across physics, model/import, navigation, scene, Canvas/GPU, async streaming, commit queue, API CLI, and ABI manifest; validation commands are recorded below. | Targeted suites, docs check, policy lint, sanitizer lanes, and full repository build/test. |

## Detailed implementation notes

### Ownership and lifetime

`rt_const_cstr` creates a managed string for non-empty input. Many adapter-style
calls constructed one inline, passed it to a retaining/borrowing callee, and
lost the caller reference. JSON helpers did the same for every property lookup.
The repair uses explicit local handles (or small C-string adapter helpers), then
releases them on all paths. Returned managed strings such as animation names
are also released after conversion/use. Slot assignment helpers retain into the
destination before releasing the caller's newly created reference, preserving
self-assignment safety.

Physics hit lists now retain each raw hit's body/collider for the list lifetime.
If an indexed box is requested, that box takes its own retains. The list
finalizer independently releases materialized boxes and packed raw references,
so lazy and eager access have the same externally visible lifetime.

Async staging preserves a simple rule: enqueue success transfers job cleanup to
the queue; failure leaves ownership with the producer. Because releasing the
retained stream on a worker could run a runtime finalizer off the main thread,
the already allocated job becomes its own node in an atomic emergency stack and
is committed/cleaned by the next main-thread drain.

### Spatial correctness

Body transform revisions alone cannot detect an attached mesh edit or a child
added to an already attached compound collider. Mesh and collider mutation now
advance portable atomic epochs. Physics query broadphases key reuse on both
epochs, and Scene3D spatial indices key reuse on the mesh epoch. This is
deliberately coarse-grained: an unrelated mesh edit may cause one extra refit,
but stale bounds cannot survive.

Unsupported narrow-phase pairs now return no contact. This is safer than a
false positive and makes the support boundary explicit. Adding those pairs in
the future requires a real geometric test and dedicated fixtures; the broad
phase remains only a rejection accelerator.

### Navigation

Triangle traversal costs are clamped to at least one, so centroid distance is an
admissible heuristic for the ordinary triangle graph. An authored off-mesh edge
can violate that geometric lower bound. Any navmesh containing such an edge now
uses zero heuristic, retaining optimality at the cost of Dijkstra-like search.

The reusable workspace removes four heap allocations per nontrivial query.
Queries against the same NavMesh3D serialize while queries against different
navmeshes remain independent. This trades uncontrolled concurrent scratch
mutation for predictable reuse; it does not make concurrent mutation of the
navmesh topology a supported operation.

### Rendering and decoding

The RenderTarget3D budget is a reservation, not a report of bytes allocated at
construction time. Reserving the maximum lazy footprint prevents an application
from creating many cheap shells and later exceeding the ceiling through
readback/mirror allocation.

Canvas3D's reusable capture APIs require an existing `Pixels` object whose
dimensions exactly match the active output. Software and RTT captures need no
staging allocation. GPU readback grows one canvas-owned RGBA buffer and reuses
it for subsequent captures that fit. A successful copy calls `pixels_touch`,
so backend caches observe destination mutation.

The D3D11/OpenGL hint tables are accelerators only. Every hinted index validates
the identity stored at that entry; collisions or stale indices fall back to the
existing linear search. Compaction rebuilds the table, preserving correctness
under eviction.

BC6H decoding no longer relies on signed left shift, signed overflow, or the
implementation-defined right shift of a negative integer. Invalid bit ranges
and reserved modes produce black rather than exposing partially decoded state.

### API contracts and compatibility

The schema change is additive except for the schema version. Existing canonical
names and signatures are unchanged apart from the two new Canvas3D methods.
Strict schema-v3 consumers must opt into v4; field-tolerant consumers continue
to find all prior fields.

The compact `runtime-def-v1` signature dialect does not encode every possible
object-return condition. The 3D policy therefore marks object and raw-pointer
returns conservatively nullable. This can overstate absence for constructors
that normally succeed, but it never encourages a consumer to dereference a
possibly null handle. Strings and collection results use managed empty-value
contracts where the reviewed implementation provides them.

## Validation

The final validation results on macOS arm64 were:

- A scratch `./scripts/build_zanna_mac.sh` compile completed with warnings as
  errors. Its default non-slow CTest run passed 1,891/1,891 tests (one
  capability-inapplicable audio test was reported as skipped), including
  140/140 `graphics3d` tests. The script's platform lint, strict runtime-surface
  audit, focused 8/8 surface tests, and host smoke slices all passed. The script
  was stopped only when its post-validation `/usr/local` install requested an
  administrator password.
- The documented non-install handoff,
  `ZANNA_SKIP_CLEAN=1 ZANNA_SKIP_INSTALL=1 ./scripts/build_zanna_mac.sh`, exited
  zero. It rebuilt the scratch tree, repeated all 1,891 tests successfully, and
  repeated the policy, surface-audit, and smoke gates.
- A combined AddressSanitizer/UndefinedBehaviorSanitizer Graphics3D build
  passed 9/9 focused Canvas, Canvas GPU, Model, NavMesh, Physics, Scene, async
  streaming, commit-queue, and ABI-manifest tests. Apple's sanitizer runtime
  does not implement leak detection, so this lane used `detect_leaks=0`; the
  ownership cases remain exercised under ASan/UBSan.
- The dedicated ThreadSanitizer concurrency lane passed 10/10 tests, including
  debug-cache publication, BRDF LUT first use, NavMesh workspace reuse, async
  streaming, and commit-queue synchronization.
- The complete slow arm64 native-smoke subset passed 9/9, including native
  fixed-step Physics3D, 3D bowling, Xenoscape, achievement drawing, chess, and
  movement cases.
- `build/src/rtgen --audit --strict-header-sync --strict-unclassified
  src/il/runtime/runtime.def` passed for 7,355 functions, 513 classes, and
  8,135 C header declarations.
- `build/src/rtgen --docs --check src/il/runtime/runtime.def
  docs/generated/runtime`, `./scripts/source_health_audit.sh --check`,
  `./scripts/lint_platform_policy.sh --strict --changed-only`, and
  `git diff --check` all passed.

## Deliberate limits and residual validation boundaries

- Native D3D11 execution requires Windows; native OpenGL execution requires a
  configured OpenGL platform. The macOS validation covers shared backend logic,
  Metal/software paths, compile-time integration, and cache-policy helpers;
  Windows/Linux CI remains the authority for native platform adapters.
- Unsupported collider pairs intentionally produce no collision rather than an
  approximate AABB collision. This is a documented correctness boundary, not a
  promise of feature parity for every shape combination.
- The VSCN hierarchy limit is 98 serialized nodes, not an arbitrary C-stack
  limit. It is tied to the shared JSON parser's 200-level nesting budget and is
  enforced symmetrically.
- Global geometry epochs favor correctness and simplicity over perfect
  invalidation granularity. They may trigger one unnecessary refit/rebuild
  after unrelated geometry changes.
- NavMesh3D path queries serialize per object to reuse scratch memory. If
  parallel pathfinding on one mesh becomes a measured bottleneck, the next
  step is a bounded workspace pool, not a return to per-query allocation.
- `rt_*` symbol names are visible in the manifest for auditability but are not
  an independently versioned public C SDK contract.

## Primary changed surfaces

- API/registry/tooling: `src/il/runtime/RuntimeSignatures.*`,
  `src/tools/rtgen/rtgen.cpp`, `src/tools/zanna/main.cpp`, and
  `src/il/runtime/defs/graphics3d/rendering.def`.
- Runtime correctness: Graphics3D assets, animation, physics, navigation,
  scene, render, backend, Game3D, and concurrent queue modules.
- Public/internal headers: `rt_canvas3d.h`, `rt_physics3d.h`,
  `rt_concqueue.h`, private physics layout headers, and related internal state.
- Tests: Graphics3D manifest, Canvas/GPU, physics, model/import, navigation,
  scene, async streaming, commit queue, CLI API dump, and surface probes.
- Documentation: ADR 0102, generated runtime reference, architecture/guide,
  rendering reference, and tooling schema documentation.
