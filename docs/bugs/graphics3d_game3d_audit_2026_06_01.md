# Graphics3D/Game3D Runtime Audit - 2026-06-01

Scope: `Viper.Graphics3D.*`, `Viper.Game3D.*`, and all code under
`src/runtime/graphics/3d/*`.

This list is intentionally implementation-facing. Items are concrete bugs,
correctness hardening, optimizations, or refactors found during source review.
`Status` starts at `queued`; entries move to `fixed` as patches land.

## Findings

1. Status: fixed. `rt_game3d.c`: `game3d_i64_saturating_add` only handles positive `b`; negative values can overflow in `INT64_MAX - b` and underflow in `a + b`.
2. Status: fixed. `rt_game3d.c`: stream budget checks use `budget - bytes` style signed subtraction; use a helper so near-limit budgets cannot overflow or miscompare.
3. Status: fixed. `rt_game3d.c`: terrain remaining-budget checks use `remaining_budget - tile_bytes`; guard via non-overflowing budget-fit helper.
4. Status: fixed. `rt_game3d.c`: cell distance-squared can become NaN/Inf for extreme manifest coordinates and silently suppress desired residency.
5. Status: fixed. `rt_game3d.c`: terrain tile distance-squared has the same non-finite failure mode as cell streaming.
6. Status: fixed. `rt_game3d.c`: streaming threshold squared is computed inline and can overflow to Inf; centralize finite squared-radius handling.
7. Status: fixed. `rt_game3d.c`: `game3d_world_stream_resident_cell_bytes` reads through a ternary before the null check; move the null check first for clarity and safer future edits.
8. Status: fixed. `rt_game3d.c`: heightmap `strtoll` parsing ignores `ERANGE`; overflowed dimensions can be accepted as clamped libc results.
9. Status: fixed. `rt_game3d.c`: heightmap `strtod` parsing ignores `ERANGE`; overflows currently rely only on the final finite check.
10. Status: fixed. `rt_game3d.c`: heightmap token readers accept partial numeric tokens such as `1abc`; require token delimiter after parse.
11. Status: fixed. `rt_game3d.c`: zero-length asset files are reported the same as read failures; preserve an explicit empty-file diagnostic path.
12. Status: fixed. `rt_game3d.c`: unsupported async model formats defer all work to the commit queue, so "async" may still main-thread load a large model.
13. Status: fixed. `rt_game3d.c`: async worker loads full files into memory; add bounded read or streaming preflight for very large assets.
14. Status: fixed. `rt_game3d.c`: model cache residency hint mutation is lock-sensitive but the helper name does not state its lock precondition.
15. Status: fixed. `rt_game3d.c`: `game3d_model_cache_evict_to_budget` is linear victim search per eviction; replace repeated scans with a stable priority heap if cache size grows.
16. Status: fixed. `rt_game3d.c`: world animation updates can run animator wrappers on worker threads while they create/release runtime strings; gate or serialize until heap/refcount safety is explicit.
17. Status: fixed. `rt_game3d.c`: animation task fallback can execute one failed-submit task inline while other animator tasks are active; document slice disjointness or force serial fallback after first failure.
18. Status: fixed. `rt_game3d.c`: `game3d_world_render_once` continues drawing after `beginFrame` no-ops on invalid camera; make the begin result observable or re-check live camera.
19. Status: fixed. `rt_game3d.c`: run-loop tick advances frame counters before a close result; clarify and test whether close frames should be counted.
20. Status: fixed. `rt_game3d.c`: run-frame trap recovery is installed only when a canvas state was saved; native update callbacks can trap without restoration context on canvas-less worlds.
21. Status: fixed. `rt_game3d_indices.inc`: body index deletion is correct but bespoke; add a focused tombstone/cluster regression test for remove-after-collision chains.
22. Status: fixed. `rt_game3d_indices.inc`: name index duplicate policy is "first wins"; document the public behavior or add an explicit duplicate-name query result policy.
23. Status: fixed. `rt_g3d_commit_queue.c`: cost-budget drain peeks then dequeues from a concurrent queue; another producer/consumer could invalidate cost assumptions if main-thread-only drain is violated.
24. Status: fixed. `rt_g3d_commit_queue.c`: pending count is approximate but surfaced as an integer; document approximation in every caller that uses it for telemetry.
25. Status: fixed. `rt_g3d_commit_queue.c`: default unit-cost commits can starve large upload budgeting semantics; expose named cost units for asset commits.
26. Status: fixed. `assets/rt_gltf.c`: `jnum` returns boxed float values without finite validation, allowing NaN/Inf into glTF parsing defaults.
27. Status: fixed. `assets/rt_gltf.c`: `jint` casts boxed double to `int64_t` without finite/range checks; C conversion is undefined for NaN and out-of-range values.
28. Status: fixed. `assets/rt_gltf.c`: `jvalue_num` repeats the non-finite JSON-number issue.
29. Status: fixed. `assets/rt_gltf.c`: `jvalue_int` repeats the unsafe double-to-int conversion.
30. Status: fixed. `assets/rt_gltf.c`: raw JSON numeric extraction with `strtod` should check `errno == ERANGE` and reject trailing garbage.
31. Status: fixed. `assets/rt_gltf.c`: texture `texCoord` values are narrowed to `int32_t` after generic JSON integer coercion; clamp/reject before narrowing.
32. Status: fixed. `assets/rt_gltf.c`: node child indices from JSON are narrowed to `int32_t`; add a shared checked-index reader.
33. Status: fixed. `assets/rt_gltf.c`: skin/joint counts derive from array lengths; reject lengths above `INT32_MAX` before allocation.
34. Status: fixed. `assets/rt_gltf.c`: quaternion normalization at import should reject non-finite squared length before `sqrt`.
35. Status: fixed. `assets/rt_gltf.c`: accessor sparse float-to-uint conversions clamp negatives but should reject non-finite floats explicitly.
36. Status: fixed. `assets/rt_gltf.c`: preload bundle allocation paths should use one checked capacity helper rather than repeated local growth code.
37. Status: fixed. `assets/rt_gltf.c`: GLB length comparisons cast host `size_t` to `uint32_t`; reject files above 4 GiB before cast.
38. Status: fixed. `assets/rt_gltf.c`: decoded image byte accounting should saturate all intermediate width/height/mip multiplications.
39. Status: fixed. `assets/rt_gltf.c`: material extension support list is hand-maintained; add a test that every supported required extension has parser coverage.
40. Status: fixed. `assets/rt_fbx_loader.c`: multiple FBX count-to-`int32_t` casts should share a checked cast helper and consistent diagnostics.
41. Status: fixed. `assets/rt_fbx_loader.c`: FBX decompression expected-size checks are strong, but compressed-length allocation should cap input before `malloc`.
42. Status: fixed. `assets/rt_fbx_loader.c`: FBX polygon remap arrays rely on several parallel reallocs; refactor into a single grow helper to avoid partial-growth divergence.
43. Status: fixed. `assets/rt_fbx_loader.c`: FBX morph delta `delta_count * 3` comparison should avoid signed multiplication before casting.
44. Status: fixed. `assets/rt_fbx_loader.c`: FBX animation time insertion is O(n^2); use sort/unique for large key sets.
45. Status: fixed. `render/rt_canvas3d_internal.h`: mesh bounds radius uses float `dx*dx + dy*dy + dz*dz`; overflow yields Inf radius.
46. Status: fixed. `render/rt_canvas3d_internal.h`: mesh bounds should sanitize non-finite AABBs before caching them as clean.
47. Status: fixed. `render/rt_canvas3d_internal.h`: render-target color allocation uses `malloc` plus `memset`; use `calloc` or a checked zero-allocation helper.
48. Status: fixed. `render/rt_mesh3d.c`: tangent accumulator indexing uses `i0 * 3u` in 32-bit arithmetic; cast to `size_t` before multiplication.
49. Status: fixed. `render/rt_mesh3d.c`: tangent generation changes tangent vertex data but also marks bounds dirty; split attribute revision from positional geometry revision.
50. Status: fixed. `render/rt_mesh3d.c`: tangent generation leaves `tangents_ready` false on empty indexed meshes; cache the "nothing to do" result.
51. Status: fixed. `render/rt_mesh3d.c`: normal recalculation allocates a full double accumulator even for meshes with no triangles; early-out empty index buffers.
52. Status: fixed. `render/rt_mesh3d.c`: clone allocates capacity one for empty source meshes; consider preserving normal initial capacities to avoid immediate realloc on edits.
53. Status: fixed. `render/rt_mesh3d.c`: OBJ vertex-cache load-factor arithmetic `(count + 1) * 10` can overflow `size_t`; rewrite division-style.
54. Status: fixed. `render/rt_mesh3d.c`: OBJ face parallel arrays grow with three separate mallocs; refactor into one struct-array grow.
55. Status: fixed. `render/rt_mesh3d.c`: OBJ/STL line-buffer growth caps are large; add diagnostics for truncated lines instead of silent load failure.
56. Status: fixed. `render/rt_mesh3d.c`: procedural sphere/cylinder segment caps are high enough to produce large transient memory; expose predictable budget checks.
57. Status: fixed. `render/rt_canvas3d.c`: shadow light direction normalization checks length but not `isfinite(length)`.
58. Status: fixed. `render/rt_canvas3d.c`: shadow scene radius can become Inf for extreme world bounds; sanitize before eye/projection construction.
59. Status: fixed. `render/rt_canvas3d.c`: shadow camera forward/right/up normalization should reject non-finite lengths consistently.
60. Status: fixed. `render/rt_canvas3d.c`: screen-space rectangle conversion casts floor/ceil floats to `int32_t`; clamp before cast.
61. Status: fixed. `render/rt_canvas3d.c`: text quad counts are cast from `size_t` to `int32_t`; keep all overflow guards next to the cast.
62. Status: fixed. `render/rt_canvas3d.c`: instanced fallback transform allocation duplicates matrix copies; share one snapshot allocator.
63. Status: fixed. `render/rt_canvas3d.c`: draw queue sort/partition loops rescan the same command array several times; combine opaque/translucent counting and staging.
64. Status: fixed. `render/rt_canvas3d.c`: final overlay replay retains temp buffers per frame; cap or reuse buffers under heavy text/HUD workloads.
65. Status: fixed. `render/rt_canvas3d_snapshot.c`: mesh snapshots duplicate full vertex/index buffers; add a byte budget and telemetry for snapshot cache pressure.
66. Status: fixed. `render/rt_canvas3d_motion.c`: motion-history capacity growth should use the shared checked capacity helper.
67. Status: fixed. `render/rt_material3d.c`: texture-slot UV transforms should reject non-finite rotation before storing.
68. Status: fixed. `render/rt_light3d.c`: spot and point light attenuation should have consistent finite/range clamps across constructors and setters.
69. Status: fixed. `render/rt_rendertarget3d.c`: depth/color byte estimates should saturate all intermediate products for huge requested targets.
70. Status: fixed. `render/rt_instbatch3d.c`: instance batch growth zero-fills multiple large arrays; reuse existing contents with targeted initialization.
71. Status: fixed. `render/rt_sprite3d.c`: frame rectangle float-to-int clamping handles high positives but should handle values below `INT32_MIN`.
72. Status: fixed. `render/rt_decal3d.c`: vector normalization should use a finite-aware length helper shared with lights/cameras.
73. Status: fixed. `scene/rt_scene3d_node.c`: world-scale computation returns NaN/Inf if the matrix basis overflows; sanitize before returning Vec3.
74. Status: fixed. `scene/rt_scene3d_node.c`: add-child cycle detection is linear; cache subtree revisions for deep scene graphs.
75. Status: fixed. `scene/rt_scene3d.c`: repeated explicit-stack growth helpers are duplicated; consolidate into one checked stack grow helper.
76. Status: fixed. `scene/rt_scene3d.c`: scene draw traversal recalculates some visibility data after unchanged transforms; reuse world revisions more aggressively.
77. Status: fixed. `scene/rt_scene3d_spatial.c`: BVH rebuild is all-or-nothing; add incremental update for small moved-node counts.
78. Status: fixed. `scene/rt_scene3d_query.c`: spatial query candidate buffers should expose capacity reuse to avoid per-query allocations.
79. Status: fixed. `scene/rt_scene3d_nodeanim.c`: node animation channel arrays duplicate capacity-growth logic; refactor to shared helpers.
80. Status: fixed. `scene/rt_scene3d_vscn.c`: VSCN base64 decoder should report exact bad-token offsets for authoring diagnostics.
81. Status: fixed. `anim/rt_animcontroller3d.c`: matrix decomposition scale lengths use `sqrtf`/`sqrt` without a finite-aware helper in several places.
82. Status: fixed. `anim/rt_animcontroller3d.c`: controller state lookup is linear by name; add an optional name index for many-state controllers.
83. Status: fixed. `anim/rt_animcontroller3d.c`: event queue truncates long names silently; surface a documented maximum or return truncation state.
84. Status: fixed. `anim/rt_animcontroller3d.c`: layer mask rebuild should cache skeleton topology revisions.
85. Status: fixed. `anim/rt_animcontroller3d.c`: LOD animation accumulator should avoid drifting after long pauses by reducing modulo update periods.
86. Status: fixed. `anim/rt_skeleton3d.c`: animation keyframe insertion is sorted per insert; batch-loaders should build sorted arrays once.
87. Status: fixed. `anim/rt_skeleton3d.c`: retarget fallback names are normalized repeatedly; cache normalized bone names per skeleton.
88. Status: fixed. `anim/rt_morphtarget3d.c`: clone/apply paths should share checked `(vertex_count * 3)` allocation helpers.
89. Status: fixed. `anim/rt_morphtarget3d.c`: normal/tangent normalization needs finite-aware length checks everywhere, matching mesh tangent fallback.
90. Status: fixed. `anim/rt_iksolver3d.c`: IK matrix scale extraction should reject non-finite basis length before division.
91. Status: fixed. `physics/rt_collider3d.c`: heightfield allocation computes `(width * height)` before casting to `size_t`; keep the checked product and reuse it.
92. Status: fixed. `physics/rt_collider3d.c`: heightfield raw indexing uses `z * width + x`; derive from a checked `size_t` index.
93. Status: fixed. `physics/rt_physics3d.c`: island-batch allocation multiplies body/contact counts repeatedly; centralize checked allocation sizes.
94. Status: fixed. `physics/rt_physics3d.c`: BVH stack limits are fixed; add tests for deep unbalanced mesh trees.
95. Status: fixed. `physics/rt_joints3d.c`: joint distance calculations should use finite-aware hypot helpers to avoid Inf/NaN impulses.
96. Status: fixed. `nav/rt_navmesh3d.c`: triangle normal length checks omit `isfinite`; NaN normals can be accepted into the navmesh.
97. Status: fixed. `nav/rt_navmesh3d.c`: voxel bake casts large `span / cell_size` doubles to `int64_t`; reject out-of-range values before cast.
98. Status: fixed. `nav/rt_navmesh3d.c`: voxel allocation products should be checked once and reused for every allocation/copy.
99. Status: fixed. `nav/rt_navmesh3d.c`: A* centroid/offmesh distance calculations should return `FLT_MAX` or zero for non-finite vectors instead of NaN.
100. Status: fixed. `world/rt_terrain3d.c`, `rt_water3d.c`, `rt_vegetation3d.c`: world generators each define local clamp/growth helpers; consolidate shared finite and capacity helpers for consistency.

## Verification Addendum

During the final source-level pass, one additional VSCN loader hardening issue
was fixed: `scene/rt_scene3d_vscn.c` now rejects non-finite/out-of-range JSON
floating-point values before coercing them to `int64_t`, and generic VSCN
double reads fall back on non-finite input. `test_rt_scene3d` includes a
regression fixture for an out-of-range numeric mesh index.

## Importer and Display Remediation Addendum

The follow-up implementation pass completed the importer/display fixes that were
still open around `Viper.Graphics3D.*`, `Viper.Game3D.*`, and
`src/runtime/graphics/3d/*`:

1. Status: fixed. glTF external URI handling now percent-decodes before validation, accepts safe `./` relative paths, and rejects absolute paths, schemes, `..`, NUL bytes, and overlong references.
2. Status: fixed. glTF scalar/vector/index reads now use explicit little-endian decoding instead of relying on host byte order.
3. Status: fixed. glTF accessor contracts for positions, normals, UVs, colors, tangents, indices, joints, and weights are validated before mesh emission.
4. Status: fixed. glTF point and line primitive modes are rejected as unsupported renderable geometry instead of being silently skipped.
5. Status: fixed. glTF invalid node graphs now fail the load instead of returning an asset with an empty or misleading scene tree.
6. Status: fixed. glTF TRS import sanitizes non-finite components and normalizes quaternions before scene nodes are created.
7. Status: fixed. glTF materials without `pbrMetallicRoughness` now receive the glTF default PBR values, including metallic/roughness defaults.
8. Status: fixed. glTF required-extension support now includes texture transform, emissive strength, unlit, punctual lights, specular, clearcoat, and transmission where the runtime has a mapping.
9. Status: fixed. glTF skin and morph instantiation no longer mutates shared asset meshes while cloning per-node runtime meshes.
10. Status: fixed. glTF mesh import reserves direct vertex/index capacity and avoids unnecessary intermediate sidecar work for common direct-import paths.
11. Status: fixed. FBX compressed array import now releases both compressed and inflated temporary buffers on every path.
12. Status: fixed. FBX external texture references reject unsafe absolute, scheme, traversal, and NUL-containing paths before falling back to safe basenames.
13. Status: fixed. FBX polygon triangulation now grows dynamically and supports n-gons larger than the old fixed stack limit.
14. Status: fixed. FBX materialless mesh nodes now receive a default material so valid geometry remains visible through `Model3D`.
15. Status: fixed. FBX mesh/material binding growth was refactored so allocation failure cannot leave partially updated parallel arrays.
16. Status: fixed. FBX scene object lookup uses checked class IDs before casting payloads to mesh, material, or model records.
17. Status: fixed. FBX import now includes a minimal ASCII geometry fallback for simple `Vertices` and `PolygonVertexIndex` files.
18. Status: fixed. FBX model transforms import common pre/post rotation, geometric transform, and offset properties by folding them into runtime TRS.
19. Status: fixed. FBX `LayerElementMaterial` polygon assignments are retained during geometry extraction.
20. Status: fixed. FBX multi-material meshes are split into material-specific child mesh nodes during `Model3D` scene adaptation.
21. Status: fixed. OBJ normal repair now fills only missing normals and preserves authored normals already present in the file.
22. Status: fixed. OBJ material groups can be loaded through the internal grouped loader so per-`usemtl` mesh buckets survive `Model3D.Load`.
23. Status: fixed. `Model3D.Load(".obj")` now resolves safe relative `.mtl` files and maps `newmtl`, `Kd`, `d`, and `Tr` to runtime materials.
24. Status: fixed. OBJ-backed `Model3D` assets now create material-group template nodes instead of collapsing all faces into one default-material mesh.
25. Status: fixed. Missing OBJ materials now fall back to a default material per group instead of dropping geometry or leaving null material bindings.
26. Status: fixed. `Model3D.Load(".stl")` is now supported and wraps binary/ASCII STL geometry in a reusable model template.
27. Status: fixed. `ModelTemplate` now exposes scene count, scene names, per-scene camera access, and indexed scene instantiation through the Game3D facade.
28. Status: fixed. The runtime ABI catalog includes the new `ModelTemplate` scene/camera methods and `sceneCount` property.
29. Status: fixed. Documentation now distinguishes geometry-only `Mesh3D.FromOBJ` from material-preserving `Model3D.Load(".obj")`.
30. Status: fixed. Documentation now reflects `.stl` model templates, FBX ASCII fallback, FBX material splitting, and the updated glTF required-extension/path contracts.

Focused verification:

```sh
cmake --build build --target test_rt_gltf test_rt_model3d test_rt_game3d test_rt_canvas3d test_graphics3d_abi_surface -j4
ctest --test-dir build -R 'test_rt_(gltf|model3d|game3d|canvas3d)$|test_graphics3d_abi_surface$' --output-on-failure
```

Result: all five focused CTests passed.
