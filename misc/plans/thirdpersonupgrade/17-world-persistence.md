# Plan 17 — Streamed-World Entity-State Persistence + Save Slots

## 1. Objective & scope

An open world must remember itself: doors opened, pickups taken, enemies dead — across cell unload/reload *and* across save files. Add per-cell entity-state deltas captured on unload and reapplied on load, plus a `World3D.saveState/loadState` snapshot serializing those deltas with player-authored tags into a versioned binary under the SaveData directory. `Viper.Game.SaveData` (JSON key-value) remains the scalar-progress store; this plan covers world-shaped state.

**In scope:** (a) per-entity persistence opt-in + state capture (alive/transform/tag); (b) WorldStream3D delta store keyed by cell; (c) `VW3DSAV1` snapshot save/load; (d) SaveData-dir integration + slot naming.
**Out of scope:** full scene serialization (`.vscn` covers authoring), physics velocity capture (positions only — resting state), cross-version migration beyond the version field.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **Nothing persists across cell churn:** `game3d_world_stream_unload_cell` (`rt_game3d_streaming.inc:1284`) releases the subtree; a reload re-instantiates the authored `.vscn` — runtime mutations (despawns, moved props) are lost by design today.
- **SaveData:** JSON key-value with atomic writes + platform save paths (`docs/viperlib/game/persistence.md` — §Platform Paths, §Atomic Writes); exposes the per-game save directory the binary snapshot will live in (verify the exact path accessor in `rt_config.c`/persistence impl at write time).
- **Entity identity:** entities have `name` (registry lookup `findEntityOption`) and belong to a spawning context: authored cell subtrees are *raw nodes*, while Game3D-spawned entities are registry-owned (`game3d.md` §Entities) — two different capture strategies needed (below).
- **Binary format precedent:** `NavMesh3D.Export` writes explicit little-endian versioned payloads (`VNAVMSH2`, `rendering3d.md` §NavMesh3D).
- **Stale/lifecycle rules:** despawn marks handles stale; world destroy invalidates — snapshot capture must run on live state only (`game3d.md` §Entities).
- **Cell metadata:** manifest-indexed `getCell*` inspection (name is the stable cell key; `game3d.md` §WorldStream3D).

## 3. Design

### 3.1 Persistence opt-in and identity

- `Entity3D.setPersistent(key)` — opt-in with an explicit string key (unique per world; duplicate keys trap at spawn). Only persistent entities are captured — capturing everything would be wrong (particles, projectiles) and expensive.
- Authored-cell content (raw nodes) is not entity-granular; persistent *changes* to cell content are expressed by games as spawned overlay entities or via **cell flags**: `WorldStream3D.setCellFlag(cellName, flagKey, i64)` / `getCellFlag` — a per-cell int map for door-opened/chest-looted style state that authored-content logic reads on load (polling `justLoadedCell` events; add `loadedCellEventCount/loadedCellEvent(i) -> str` to the stream).
- Captured record per persistent entity: `{key, alive, position, rotationEuler, scale, stateTag}` where `stateTag` is the plan-06-adjacent free `i64` (`Entity3D.setStateTag/getStateTag` — added here if plan 06 hasn't landed).

### 3.2 Delta store + streaming hooks

`rt_game3d_world` owns a hash map `key → record` (all persistent entities, resident or not) plus the per-cell flag maps:

- **On despawn/unload** of a persistent entity: record updated (alive=false for kills, pose for moves).
- **Per step (cheap):** resident persistent entities refresh their record lazily — only on `stepSimulation` frames where their node's transform revision changed (piggyback the existing node revision counters), keeping steady-state cost near zero.
- **On cell load (plan 11 commit tail):** after subtree commit, the stream emits `loadedCellEvent`; game code applies cell flags; engine-owned respawn suppression: a spawned-persistent entity whose record says `alive=false` is *not* respawned by `SceneTemplate`-driven spawner helpers (games that hand-spawn check the record via `World3D.getPersistentAlive(key)`).

### 3.3 Snapshot format + API

`World3D.saveState(slotName)` / `loadState(slotName)`:

- Path: `<SaveData dir>/<slotName>.vw3dsav`; format `VW3DSAV1`: header (magic, version, counts), entity records (length-prefixed keys, LE doubles), cell-flag maps, world extras (worldOrigin for floating-origin correctness, elapsed, time-of-day hours when plan 16 present). All fields length/count-validated on read; corrupt file ⇒ recoverable false + diagnostic (never trap — SaveData missing-file convention).
- `loadState` applies: records replace the delta store; resident persistent entities are re-posed/killed immediately; non-resident apply on their cell's next load. Returns false on version/magic mismatch.
- Slot enumeration/metadata (screenshots, timestamps) stays game-side on SaveData JSON — documented recipe, not runtime surface.

## 4. Implementation steps

1. `setPersistent(key)` + record store + despawn/refresh capture + `getPersistentAlive/Pose` accessors; C unit tests.
2. Cell flags + `loadedCellEvent` polling on WorldStream3D; tests with the openworld_slice manifest.
3. `VW3DSAV1` writer/reader (+ fuzz-guarded reader: truncated/corrupt cases recover false) + save-dir pathing via the SaveData platform-path helper.
4. `saveState/loadState` wiring incl. floating-origin and resident re-pose; round-trip tests.
5. Respawn-suppression hook in the template-spawn path.
6. runtime.def + audits + ADR + docs (`game3d.md` new §Persistence, cross-ref `persistence.md`).
7. Zia probe `g3d_test_game3d_persistence_probe`: kill + move + flag in cell A, stream out/in ⇒ state holds; save, mutate, load ⇒ state restored; deterministic replay.

## 5. Public API changes (runtime.def)

```
Entity3D: setPersistent(str) fluent, get_persistentKey, setStateTag(i64)/getStateTag
World3D:  saveState(str)->i1, loadState(str)->i1, getPersistentAlive(str)->i1,
          getPersistentPosition(str)->obj<Vec3>
WorldStream3D: setCellFlag(str,str,i64), getCellFlag(str,str)->i64,
          loadedCellEventCount()->i64, loadedCellEvent(i64)->str
```

No new classes (records are internal). ADR `00xx-world-persistence.md`. Reader validation gets a fuzz harness under `src/tests/fuzz/` (new input surface — CLAUDE.md fuzz policy).

## 6. Tests

- **Cell round-trip:** Given a persistent crate moved 3 m and a persistent enemy despawned in cell A — When A streams out and back — Then crate pose restored, enemy not respawned (fail-before: both reset) .
- **Snapshot round-trip:** save → mutate all records → load ⇒ byte-compare of the delta store; second save produces byte-identical file (determinism).
- **Corrupt reads:** truncated header/bad magic/oversized counts ⇒ false + no state change + no trap (fuzz harness seeds).
- **Floating origin:** save at 50 km rebase, load into fresh world ⇒ world-space poses correct (`worldOrigin` applied).
- **Duplicate key:** second `setPersistent("door_1")` traps with a clear diagnostic.
- **Non-resident apply:** loadState while cell unloaded ⇒ state applies on cell load (probe asserts post-load pose).

## 7. Verification gates

Full build + ctest; streaming lane green; fuzz smoke on the reader; determinism gate (capture piggybacks stepSimulation); `-L slow`; `-L graphics3d`; surface audits.

## 8. Risks & constraints

- **Identity discipline is on the game:** keys must be stable across sessions (docs: derive from authored names, never from spawn order). The trap-on-duplicate catches the common error early.
- **Authored-content granularity:** cell flags are deliberately coarse — resist per-raw-node capture (would couple the saver to VSCN internals).
- **Record growth:** the store is per-persistent-entity only; document that persistent-flagging every projectile is user error (no engine cap in v1, diagnostics counter if needed later).
- Snapshot excludes physics velocities — a loaded save resumes at rest; docs state it (matches the genre convention of save-at-rest).
