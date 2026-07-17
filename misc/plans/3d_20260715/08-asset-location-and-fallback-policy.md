# Plan 08 — Asset Location and Fallback Policy

## Outcome

Provide one explicit, cached runtime asset-catalog policy that works from the
repository, a project directory, a packaged executable, embedded assets, and
mounted packs. It must delegate actual resolution to `Viper.IO.Assets` and
`Viper.Assets.Resolver`, distinguish optional from required assets, and emit a
missing diagnostic once per logical asset.

## Problem statement

The low-level asset system already supports embedded, packed, and CWD-relative
loose files, and Game3D typed loaders support package-aware asset loading with
filesystem fallback. The demos still own policy:

- bowling checks repository, project, executable, and direct-relative paths and
  deduplicates missing logs;
- Ashfall keeps logical model tables, enable flags, caches, optional fallbacks,
  and failure counts;
- Ridgebound has its own assets module and tree fallback behavior.

The missing abstraction is not another decoder/cache. It is a game-facing
logical catalog and one documented source/package search policy.

## Dependencies

- plan 00 namespace/error rules;
- current IO Assets and editor Resolver docs/implementation;
- current Game3D Assets3D/SceneTemplate/AssetHandle3D cache semantics;
- API register and risk R8.

This plan can proceed independently of plans 03–07.

## ADR and API

Required: public class/C ABI plus cross-layer use of generic asset resolution.
The ADR must justify Game3D placement versus a generic `Viper.Assets.Catalog`,
define resolution order, packaged-path representation, security constraints,
required/optional behavior, cache invalidation, and diagnostics.

The proposed `AssetCatalog3D` returns a path/result only. It must not duplicate
SceneAsset, sound, image, or prefab typed loaders.

## Scope

In scope:

- logical name registration;
- package logical path and development/source candidate;
- repository/project/executable context supplied explicitly, not guessed from
  unsafe traversal;
- `IO.Assets`/mounted-pack check;
- resolver result object, cache, missing-once diagnostics and counts;
- required versus optional policy;
- package/project tests and demo adoption spikes.

Out of scope:

- new asset formats or decoding;
- downloading assets;
- recursive unconstrained filesystem search;
- replacing model/texture/audio caches;
- automatic animation intent mapping (Ashfall retains clip policy).

## Primary source owners

- `src/runtime/io/rt_asset*`, existing resolver implementation and headers;
- `src/il/runtime/defs/api/audio_io.def` if generic resolver behavior changes;
- a focused Game3D catalog implementation only if ADR keeps it Game3D-owned;
- Game3D defs/IDs/CMake if a Game3D class is added;
- IO asset security/path tests, Game3D asset tests, packaging fixtures;
- `docs/viperlib/io/assets.md`, Game3D asset docs, starter/project files.

## Resolution policy

The catalog entry stores:

```text
logical name
package logical path
development path relative to an explicit game root
required flag
```

Resolution order:

1. packaged/embedded/mounted asset using the logical package path;
2. development file relative to explicit game/project root;
3. optional source-tree root supplied by the application/test runner;
4. executable-relative development fallback only when explicitly enabled;
5. missing result.

Do not infer arbitrary repository roots by walking parents. Do not accept
traversal, absolute package names, embedded NULs, symlink escapes, or special
files beyond current IO security policy. A returned packaged resolution keeps
the package logical name so callers use `Load*Asset`; a loose resolution returns
the validated filesystem path so callers use filesystem loaders.

Required missing assets return a recoverable result with `Required=true` and a
diagnostic; the application decides whether to stop. Optional missing assets
support procedural fallback and log once. The resolver itself must not print
unconditionally; expose diagnostics/counters or use the established diagnostic
sink.

## Implementation sequence

### Phase 0 — Characterize existing resolution

1. Test `IO.Assets.Exists/Load` and `Assets3D.Load*Asset` for embedded, mounted,
   and CWD loose paths.
2. Test `Viper.Assets.Resolver.Resolve` result fields for scene, project,
   asset-root, mounted, and missing sources. Its current public signature
   returns `obj<Viper.Collections.Map>` (registered in
   `src/il/runtime/defs/api/audio_io.def`), which is why phase 1 requires a
   structured internal seam instead of re-parsing that Map.
3. Reproduce documented edge cases: relative scene path interpretation and
   empty asset path. Decide whether this plan must repair them or avoid them
   with explicit absolute context.
4. Build a table mapping each demo's candidates to the common policy.

### Phase 1 — ADR and generic seam

Prefer enhancing the existing resolver with a runtime-safe internal operation
that returns structured data, then wrapping it in a typed result. Do not parse a
Map in C if the resolver already has an internal structured representation.

Define:

- construction context and canonicalization;
- result source values;
- cache key including roots and mount generation;
- invalidation when packs mount/unmount or explicit roots change;
- missing-once key and reset behavior;
- maximum entries/path lengths;
- thread-safety and main/worker usage.

### Phase 2 — Resolver/catalog core

1. Implement entry registration with duplicate logical-name rejection or
   deterministic replacement as approved.
2. Resolve package path through IO Assets without decoding.
3. Resolve loose candidates through existing safe path/file helpers.
4. Produce a non-null result object for found and missing outcomes.
5. Cache successes and misses keyed by catalog generation plus asset mount
   generation; add explicit `ClearCache`.
6. Deduplicate diagnostics per logical name while counting total unique misses.
7. Make construction/resolve OOM recoverable and leave catalog consistent.

### Phase 3 — Typed loader recipes

Add examples/helpers, not duplicate runtime methods:

- resolution.Packaged -> `Assets3D.LoadEntityAsset` or `Prefab.LoadAsset`;
- loose -> `Assets3D.LoadEntity` or `Prefab.Load`;
- images -> `IO.Assets.Load` for packaged or Pixels loader for loose according
  to existing API;
- sound -> `Sound3D.LoadAsset` or `Load`.

If the language cannot express this cleanly without repeated branching, a
small typed Game3D helper may be proposed in the ADR, but it must call existing
loaders and preserve their cache/error semantics.

### Phase 4 — Registration and tests

Add class/result definitions, permanent IDs if Game3D-owned, real/disabled
behavior, CMake, docs, and surface audits. Tests cover:

- embedded, mounted pack, project loose, source root, executable fallback;
- precedence when the same logical asset exists in multiple places;
- package path preserved versus filesystem path;
- required/optional missing result and one diagnostic per logical name;
- clear cache and pack mount/unmount invalidation;
- traversal/absolute package path/NUL/symlink/directory rejection;
- Unicode and long path handling through current platform adapters;
- concurrent read resolution if promised;
- catalog destruction during/after resolution;
- bounded entry/path failures;
- zero-byte assets distinguished from missing;
- packaged and source-tree `viper.project` dry runs.

### Phase 5 — Demo adoption spikes

- Bowling: replace `resolveOptionalAsset` candidate logic while preserving
  procedural fallback and one-log behavior.
- Ridgebound: resolve the FBX/tree material source and fallback.
- Ashfall: replace path existence/loading selection only; retain archetype
  tables, enable flags, caches if not superseded by SceneTemplate, scale, and
  clip intent mapping.

Run each game's asset/package probes before and after. Do not merge full
migrations here.

## Performance and thread safety

- resolution is off the render hot path and may allocate on first lookup;
- cache hits should be constant-time and avoid filesystem probes;
- mount/unmount invalidation must be synchronized with existing asset registry;
- do not hold the global asset lock while performing slow arbitrary filesystem
  canonicalization if current policy avoids it;
- no background decoder/GPU work is added by resolution.

## Validation

Run IO asset/security/path tests, Game3D asset/cache/async tests, package dry
runs, source and packaged demo probes, runtime surface audits, disabled graphics
where the class lives, platform policy, and full macOS/Windows/Linux builds.

## Acceptance criteria

- One catalog configuration resolves the same logical asset in source-tree and
  packaged executions.
- All three adoption spikes remove custom candidate search without losing
  fallback or diagnostics.
- The implementation delegates to existing IO resolver/assets and typed caches.
- Security policy is not weakened; cache invalidates on mount changes.
- Required/optional outcomes are explicit and non-trapping.
- Surface/docs/package/platform gates pass.

## Stop conditions

Stop if the design requires unrestricted parent-directory search, a second pack
registry, loader-specific caches, or platform-specific path code outside IO
adapters. Move the missing capability into the existing generic resolver.

## Handoff evidence

Provide the resolution-order table, demo-candidate mapping, security test list,
source/package dry-run outputs, cache invalidation trace, adoption diffs,
surface diff, and ADR.

