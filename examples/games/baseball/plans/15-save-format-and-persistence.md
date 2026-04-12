# Save Format And Persistence

## Goal

Define how multi-season state is saved, loaded, and replayed.

The single-game backend has *implicit* persistence — the game runs in memory, the state is discarded at exit. Plans `10`–`14` introduce a season-level state graph (calendar, standings, fatigue, injuries, aging, manager decisions) that must survive across runs. This plan defines the save format, the load process, the replay model, and the determinism guarantees that hold across save/load boundaries.

## Core principle

Game state at any persistent boundary must be fully serializable, deterministic, and versioned. A loaded game must produce identical future outputs to one that was never saved.

This is the strongest determinism guarantee in the project. It is the season-scale extension of the single-game determinism rule from `plans/02 § Determinism and debugging` ("a saved game replays exactly").

## Why this layer matters

Without persistence:

- multi-season simulation is impossible (ratings, standings, careers all reset on exit)
- save scumming and replay analysis are impossible
- a long-running simulation can't be paused
- the test suite can't replay specific season days for regression checks
- bug reports can't include reproducible save files

Persistence is the layer that turns the manager sim from a "one-shot demo" into a "multi-session game."

## Persistent state inventory

Everything that survives across runs must be in the save. The full inventory:

### From the single-game backend (plans 00–09)

- nothing — the single-game state is fully derivable from the season state plus the seed and the game number

### From plan 10 (season simulation)

- `SeasonCalendar` — current day pointer, schedule, off-days, doubleheaders
- completed-game ledger (`CompletedGameRecord` entries or equivalent summaries)
- `RegularSeasonResult` if the regular season has ended

Do **not** make standings authoritative saved state. They are derived from the
completed-game ledger and should be recomputed on load.

### From plan 11 (rosters and transactions)

- `RosterCompartment` membership for every player on every team
- active role tags for every active player
- IL placement dates and expected return dates for every IL'd player
- the transaction event log for the current season (for audit and replay)

### From plan 12 (fatigue and injuries)

- `PitcherRestState` for every pitcher
- `FatigueState` for every position player
- `cumulativeSeasonPitches` per pitcher
- `gamesStartedConsecutive` per position player
- day-to-day injury counters

Derived availability snapshots should be recomputed on load, not treated as canonical
saved truth.

### From plan 13 (player development and aging)

- per-player current ratings (from `plans/04`)
- per-player `date_of_birth`
- per-player ceiling ratings or potential profile
- per-player development variance and aging curve profile
- prior off-season tick history (one entry per off-season run)

### From plan 14 (manager command layer)

- per-team policy class identifier (`"DefaultManagerPolicy"` / `"HumanManagerPolicy"` / etc.)
- the manager decision log for the current season (every checkpoint, every action chosen)
- any policy-specific state (e.g., a scripted policy's "next action" pointer)

### From this plan

- save format version
- save creation timestamp (for human-readable browsing)
- root franchise or season seed
- named RNG stream states or cursors for long-lived between-game systems

### Player metadata (always)

- player id
- name
- handedness (`bats`, `throws`)
- current position
- role tags if active
- career stats accumulated across all completed seasons (a separate ledger from the current-season stats in `plans/01`)

## Save boundaries

A save can be triggered at well-defined moments. Mid-game saves are deferred for v1 because they require the single-game engine to be pausable mid-plate-appearance, which it isn't.

Supported save boundaries:

- **End of game** — after the official result of the final play, before the next game starts
- **End of day** — after all games on a `GameDay` complete and post-game updates apply
- **End of season** — after the regular season (and playoffs, if any) end
- **End of off-season tick** — after `plans/13 § Off-season events` complete and the next season is ready to start
- **Manual** — the user can request a save at any of the above boundaries via a command

For v1, all saves are atomic at one of these boundaries. The save process never interrupts a game in progress.

## Save format

Two viable formats; the plan picks one:

### Option A: plain text (chosen)

- human-readable
- diff-able with `git diff` or `cmp`
- debuggable by hand
- works with any text editor
- larger on disk
- slower to load for huge season states
- format must be carefully designed to be unambiguous (e.g., quoted strings, escaped newlines)

### Option B: compact binary

- fast to load
- small on disk
- opaque (impossible to debug by hand)
- requires a hex dump tool to inspect
- versioning is harder (binary diff is uninformative)

**v1 chooses Option A — plain text.** The reasons:

- the user has explicitly stated a preference for being able to inspect game state
- season files won't be huge enough to make text load times painful (a season is < 1 MB of state)
- debugging save/load issues is much easier with a text format
- the deterministic-replay guarantee is easier to verify when you can `diff` two save files
- the project's philosophy is "100% from scratch" — a custom binary serializer is more work than necessary

The plain-text format is a structured key-value layout. Each line is `key: value` or `key:` followed by an indented block. A simple, line-oriented dialect (similar in spirit to YAML's indentation rules but much narrower) is fine.

Example fragment:

```
season_save_version: 1
created_at: 2026-04-12T03:14:00Z
season_seed: 4242
current_day: 47

team:
  id: away
  city: Port City
  nickname: Captains
  active_roster:
    - player_id: away_c
      compartment: active_hitters
      role_tag: backup_catcher
      position: C
    - player_id: away_1b
      compartment: active_hitters
      role_tag: regular
      position: 1B
    ...
```

The format does not need to be a real YAML or JSON parser — a simple custom Zia parser is acceptable since the format is fully under our control.

## File layout

The natural persistent unit is a **franchise root** with season subdirectories:

```
saves/
  default_franchise/
    franchise.save                    # top-level pointer and metadata
    careers.txt                       # cross-season player ledger
    seasons/
      2026/
        season.save
        decision_log.txt
        transaction_log.txt
        completed_games/
          g0001.txt
          g0002.txt
          ...
      2027/
        season.save
        ...
```

`franchise.save` identifies the current season and top-level metadata for the running
franchise. Each season directory owns one season's canonical state, logs, and
completed-game records.

This is more accurate than treating each season as an isolated top-level directory.
The product target is a continuing franchise whose state flows from season to season.

## Versioning

The save format has a single integer version field at the top:

```
season_save_version: 1
```

When the engine loads a save:

1. Read the version field
2. If the version matches, proceed normally
3. If the version is older, attempt forward migration via a registered migration function
4. If the version is newer (older engine, newer save), refuse to load and emit a clear error

Migration functions are versioned: `migrate_v1_to_v2(saveText) -> saveText`. Each bump in the version requires writing the migration function for the previous version. Skipping versions (v1 → v3 directly) is not allowed; v1 must be migrated to v2 first.

For v1 of this plan, the version starts at 1 and no migration functions exist yet.

### What triggers a version bump

- adding a new field to the persistent state inventory
- changing the meaning of an existing field
- changing the format syntax (e.g., switching key separators)

Bug fixes that don't change the data layout do NOT bump the version.

## Replay model

Three replay scopes are supported:

### 1. Single-game replay

Already works at the single-game scope per `plans/02 § Determinism and debugging`. The save format extends this to "load a season save, replay game N, verify identical output to the originally-played game N."

### 2. Single-day replay

Load a season save at the end of day D-1, run the daily loop for day D, verify the resulting end-of-day-D state matches the saved end-of-day-D state byte-for-byte.

### 3. Partial-season replay

Load a season save at the start of day D, run the daily loop until day E, verify identical output to the original run from D to E.

All three scopes share the same replay infrastructure: load the save, run the engine, compare outputs. The comparison is byte-level on text dumps of the relevant state.

## RNG handling

The deterministic guarantee is the hardest part of save/load. RNG state must be saved and restored exactly.

### What's saved

- the root franchise or season seed
- the current day / game counters needed to derive per-game seeds
- the live state of any long-lived between-game RNG streams

If the implementation uses `Viper.Math.Random.New(seed)` instances, each long-lived
stream should persist its own state explicitly. The key requirement is not "save one
global RNG"; it is "restore every between-game stream to the exact same point."

### What's NOT saved

- intermediate RNG draws within a game (the game's RNG is re-seeded fresh from `(seasonSeed, gameNumber)` at each game start)

### RNG stream separation

The season layer uses *multiple* derived RNG streams to keep different concerns from desyncing each other:

- per-game gameplay stream (seeded from `(seasonSeed, gameNumber)`)
- injury roll stream (seeded from `(seasonSeed, dayNumber, "injuries")`)
- development tick stream (seeded from `(seasonSeed, year, "development")`)
- transaction RNG stream (rare; used for tiebreakers and waiver claims)

Each long-lived stream is independently saved/restored. Per-game gameplay streams are
re-derived, not persisted.

### Verification

The deterministic guarantee is verified by:

- saving a game in the middle of a season
- running the rest of the season from the save
- separately running the same season from the start without any save/load
- byte-comparing the two final season states

If they differ, an RNG stream wasn't restored correctly.

## Career stats ledger

A separate file tracks career stats accumulated across all completed seasons:

```
saves/
  default_franchise/
    careers.txt                       # all-time stats per player
    seasons/
      2026/
        season.save
        ...
      2027/
        season.save
        ...
```

`careers.txt` is the long-running ledger. It updates at the end of each completed season's off-season tick. It never resets.

For v1, only counting stats are tracked (PA, AB, H, HR, BB, SO, IP, ER, etc.). Rate stats and advanced metrics are derived on demand.

## Save corruption recovery

For v1: minimal. If a save fails to parse, the engine emits an error and refuses to load. There is no auto-repair, no alternative save slot, no rollback.

A "previous save" backup (`.bak` file) is acceptable polish; if added, it's the only safety net.

## What to defer

For v1:

- compression of save files
- cloud sync / cross-machine save sharing
- multi-user save sharing or trading saves between people
- save corruption recovery beyond "refuse to load and explain why"
- mid-game saves (require pausable single-game engine)
- save file encryption
- save file digital signatures (proving the save came from this engine)
- arbitrary "checkpoint anywhere" saves (only supported boundaries above)
- savegame thumbnails / metadata previews
- save versioning beyond a simple integer

## Failure modes to watch

### 1. Non-deterministic replay across save (subtle)

The classic bug: a game runs differently after a save/load cycle even though both runs use the same seed. Caused by an unsaved RNG stream, an unsaved cache, or an environment-dependent computation. Test: save at end of day D, load, run day E, save again, compare to a no-save run; they must be byte-identical.

### 2. Version mismatch on load

A save file from an older engine refuses to load on a newer engine if no migration function exists. Caught by the migration registry; never silently corrupts data.

### 3. RNG divergence after load

If the per-game RNG seed isn't derived deterministically from `(seasonSeed, gameNumber)`, replaying game N after a save produces a different game. Verified by the determinism test above.

### 4. Cached state desync from saved state

If a derived value (like standings) is cached and saved, but the underlying truth (completed games) is also saved, the two can disagree on load. Resolution: do not save derived state. Re-derive on load.

### 5. Partial-write corruption

If the engine crashes mid-save, the save file is incomplete and unloadable. Mitigation: write to a temp file, then atomically rename. (POSIX `rename` is atomic for files in the same directory.)

### 6. File path collision across teams

If two different leagues share the same save directory name, the wrong save loads. Mitigation: include the league name in the save directory name; require the user to namespace.

### 7. Career stats ledger never updated

If the off-season tick fails to append to `careers.txt`, the all-time history loses a season silently. Test: simulate two seasons, verify `careers.txt` has both seasons' totals.

### 8. Decision log replay inconsistency

If the manager decision log has a different number of entries than the engine expects on replay, the wrong action plays at the wrong checkpoint. Mitigation: log entries include the checkpoint id, and the replay engine verifies the checkpoint id matches before consuming the action.

### 9. Save file growing without bound

A long-running save (10+ seasons) can accumulate enormous transaction and decision logs. Mitigation: archive old per-season log files separately so the active save only carries the current season's logs.

### 10. Hand-edited save files passing version validation but containing impossible state

A user editing a save file by hand can introduce illegal compartment counts or duplicate players. The engine should run the full invariant check from `plans/11 § Failure modes` after every load and reject loads that fail.

## Recommended next dependency

This is the last plan in the season-level batch (10–15). The natural next planning layer would be one of:

- **`16-season-calibration-and-reporting.md`** — multi-season distributions, league-wide reports, era-aware targets, the season-scale extension of `plans/06`. This was on the original brainstorm but excluded from the 10–15 batch; it's the most direct continuation.
- **`17-franchise-mode.md`** — scouting, drafts, free agency, contracts, the team-building meta-game. Requires plans 10–15 as foundation.
- **`18-narrative-and-history.md`** — player narratives beyond raw stats, awards, hall of fame, persistent history. The "story" layer.

Whichever direction the next planning round takes, the persistence model defined here is the foundation.
