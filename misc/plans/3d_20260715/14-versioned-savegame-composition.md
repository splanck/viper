# Plan 14 — Versioned Save-Game Composition and Atomic Slots

## Outcome

Add `SaveGame3D` slots that atomically coordinate custom scalar/string data with
the existing `World3D.SaveState/LoadState(app,slot)` snapshot. Preserve the
validated `VW3DSAV1` format and SaveData platform directory. Expose schema
version and diagnostics so games can perform explicit migrations.

## Problem statement

Zanna has two useful persistence layers:

- `Zanna.IO.SaveData`: atomic flat JSON key/value storage for settings/progress;
- World3D persistence: entity/cell delta state in validated `VW3DSAV1` files,
  with persistent keys, state tags, streamed cell flags, and save/load.

Games still compose them manually. Bowling stores menu/profile progression;
Ashfall has custom settings/campaign serialization; complex games need one slot
that cannot point at new metadata with an old world snapshot after a partial
write.

## Dependencies

- plan 04 scope/lifecycle for load boundaries;
- plan 08 asset/path policy only for game/package context, not save locations;
- existing SaveData and World3D persistence implementation/fuzz surface;
- API register and risk R15.

## ADR and API

Required: new public class/C ABI, on-disk manifest/slot format, transactional
commit protocol, schema/lifetime/error behavior, and use of platform save paths.
The ADR must explicitly state that `VW3DSAV1` is reused rather than wrapped in a
new world serialization.

## Scope

In scope:

- named slot validation/enumeration/delete;
- custom int/string plus Float/Boolean encoding if approved;
- schema version, timestamp/metadata and diagnostic;
- atomic coordination of metadata/custom data and world snapshot;
- failure rollback/no-state-change load;
- explicit game-owned version migration;
- corrupt/truncated/bounds/fuzz tests and demo recipes.

Out of scope:

- arbitrary object graph serialization;
- automatic script migration callbacks;
- cloud saves;
- screenshots in v1 (metadata may reference a game-created capture);
- changing World3D's binary record format;
- saving live GPU/audio objects.

## Primary source owners

- `src/runtime/game` SaveData/config/platform save path implementation;
- `src/runtime/graphics/3d/rt_game3d_persistence.c` internal serializer/path;
- new focused SaveGame3D implementation under the architectural owner chosen by
  ADR;
- Game3D or Game/IO defs, class IDs/CMake;
- save parser/fuzz/unit/Zia fixtures;
- Game3D persistence and Game SaveData docs.

## Transaction design

A slot contains generation-addressed files and a small current manifest:

```text
<save-dir>/<slot>.current
<save-dir>/<slot>.<generation>.meta
<save-dir>/<slot>.<generation>.vw3dsav
```

Exact names/encoding are ADR-owned. Save protocol:

1. validate app/game ID, slot, schema, key/value bounds;
2. serialize custom metadata to an exclusive temp file;
3. serialize the world using the existing VW3DSAV1 writer to an exclusive temp
   file for the same new generation;
4. flush/sync both files;
5. atomically rename both generation files into place;
6. write/flush/sync and atomically replace the tiny `.current` manifest last;
7. retain previous generation until the manifest commit succeeds and cleanup is
   safe.

If any step before manifest replacement fails, the old slot remains current.
If cleanup fails after commit, load still uses the new generation and stale
files can be removed later. Never overwrite the current generation in place.

Load protocol:

1. read and validate current manifest;
2. read/validate metadata into temporary state;
3. validate world snapshot fully without applying it;
4. check schema/game compatibility;
5. only then replace SaveGame3D in-memory custom data and apply World3D state;
6. on any failure, return false, preserve current in-memory/world state, and set
   `LastDiagnostic`.

If current World3D LoadState cannot validate-without-apply through an internal
entry, refactor its existing validation routine; do not duplicate the parser.

## Schema and migration contract

- `SchemaVersion` is the writer's current game schema.
- `LoadedVersion` reports the slot's schema.
- v1 runtime does not call script migration automatically.
- A game may load metadata into a temporary SaveGame3D/read-only mode, inspect
  `LoadedVersion`, transform keys with explicit code, then apply world only when
  compatible. If the initial API cannot support this safely, add a two-phase
  `Open/ApplyWorld` design in the ADR rather than loading incompatible state.
- Unknown newer versions fail recoverably.
- Missing slot is distinguishable from corrupt slot; define whether Load on
  missing returns false or an empty result and document consistently.

## Implementation sequence

### Phase 0 — Persistence inventory

1. Trace SaveData path validation, atomic temp creation, fsync/rename behavior,
   and failure/trap boundaries.
2. Trace VW3DSAV1 validation, write/apply separation, path `(app,slot)`, and
   fuzz seeds.
3. Map bowling/Ashfall fields to custom metadata versus world state.
4. Identify platform helpers that can atomically replace files and sync
   directories without raw platform checks outside adapters.

### Phase 1 — ADR and bounded formats

Define manifest/meta magic/version, little-endian or JSON choice, maximum file
size, entry count, key/value lengths, slot/app character policy, generation
source, timestamps, and cleanup/recovery. Prefer a compact validated internal
format or reuse SaveData JSON parser; do not add a dependency.

Add fuzz corpus seeds for manifest and metadata if they are new parse surfaces.

### Phase 2 — Custom data object

Implement setters/getters, type behavior, bounded map, schema fields, clear,
last diagnostic, and slot metadata. Float storage must define exact serialization
and reject non-finite values. Boolean may encode as integer if the runtime type
surface prefers it; keep public behavior clear.

### Phase 3 — Internal world serializer seam

Refactor existing persistence into:

- serialize-to-explicit-temp/path;
- validate-from-buffer/path without apply;
- apply-validated snapshot;
- existing public SaveState/LoadState wrappers.

Prove existing VW3DSAV1 bytes and public behavior remain unchanged with golden/
round-trip tests. This is sensitive code; do it before slot coordination.

### Phase 4 — Transactional save/load

Implement generation creation, two staged writes, manifest-last commit,
rollback, cleanup, load validation, and no-state-change failure. Add fault
injection at every filesystem step where test infrastructure permits.

Never derive paths by concatenating unvalidated user strings. Use SaveData
directory/platform adapters and exclusive secure temp creation.

### Phase 5 — Enumeration/delete/recovery

- enumerate only validated current manifests for the game;
- return stable sorted slots and metadata;
- delete current manifest first or use a tombstone protocol so partial delete
  cannot resurrect a slot;
- clean orphan generations conservatively;
- recover from a missing current generation by reporting corruption, not
  guessing the newest file unless ADR explicitly defines a validated recovery.

### Phase 6 — Registration and tests

Add public defs/IDs/real/disabled symbols/CMake/docs/audits. Tests:

- new slot save/load round trip with custom + world data;
- overwrite retains last complete generation;
- fault at every save step leaves old slot loadable;
- corrupt manifest/meta/world leaves in-memory/world state unchanged;
- missing/truncated/oversized/bad magic/version/count/string/UTF-8 cases;
- older/newer schema inspection and explicit migration flow;
- float/integer/string/Boolean edges;
- invalid app/slot/key/traversal/NUL names;
- enumeration stable sort, delete, orphan cleanup;
- concurrent save/load policy (serialized or rejected clearly);
- floating-origin/streamed persistent world round trip;
- deterministic save produces stable logical data (generation/timestamp may
  differ); compare decoded content, not necessarily whole files;
- fuzz harness/corpus for every new parser.

### Phase 7 — Demo recipes/spikes

- Bowling: one slot combines profile/progression/settings with a small persistent
  world fixture; do not force match-in-progress save if game policy forbids it.
- Ridgebound: persist survival/world flags plus custom player stats.
- Ashfall: map settings/campaign/custom weapon progress to metadata and world
  persistence; retain explicit schema migration.

Full demo migration follows plans 17–19.

## Performance/security

- save/load is off the frame hot path and may allocate;
- all sizes/counts validated before allocation/multiplication;
- exclusive temp files, symlink/reparse refusal, non-inheritable handles, and
  atomic replace use existing hardened patterns;
- no arbitrary absolute paths;
- failure diagnostics do not disclose unrelated filesystem contents;
- save can be synchronous in v1 but docs warn callers to invoke at safe points.

## Validation

Run SaveData tests, World3D persistence unit/Zia/fuzz tests, fault injection,
platform path/security tests, surface audits, disabled graphics, all three
platform full builds, and demo save probes.

## Acceptance criteria

- A slot never exposes mismatched metadata/world generations after a failed
  save.
- Corrupt load changes neither current custom data nor world state.
- Existing SaveData and World3D SaveState formats/APIs remain compatible.
- Schema version is visible and migration is explicit/non-callback.
- Paths and parsers are bounded/hardened across platforms.
- Demo spikes cover realistic custom plus world state.

## Stop conditions

Stop if atomicity would require overwriting the live generation, if validation
cannot precede world mutation, if paths bypass current security adapters, or if
the design duplicates VW3DSAV1 parsing.

## Handoff evidence

Provide transaction/failure table, format spec/ADR, decoded round-trip data,
fault-injection results, fuzz corpus summary, platform matrix, demo mappings,
and surface diff.

