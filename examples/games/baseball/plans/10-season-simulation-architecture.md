# Season Simulation Architecture

## Goal

Define how the engine progresses from one game to a full season.

The single-game backend (plans `00`–`09` and the implemented `src/sim/game_engine.zia`) is the per-day work unit. This plan defines the calendar, the daily loop, the schedule, the standings, and the playoff hook that turn that work unit into a season.

## Core principle

A season is a calendar-driven sequence of game days. Everything that lives between games — fatigue carryover, IL stints, lineup decisions, standings updates — belongs to the season layer, not to the game layer.

The single-game engine should not know that a season exists. The season layer calls the game engine; the game engine returns a result; the season layer updates between-game state and advances the calendar.

## Layered relationship to `plans/00`–`09`

Plan `00 § Top-level architecture` defines six single-game layers. The season layer wraps those without modifying them:

- **Single-game layers (already defined):** Domain Model, Decision Interface, Simulation Engine, Official Result Layer, Event Stream, Output Adapters
- **Season layer (this plan):** Calendar, Daily Loop, Schedule, Standings, Playoff Bracket, Between-game state propagation

The season layer reads a canonical `CompletedGameRecord` built from `OfficialPlayResult`
data and box-score totals, never the raw event log.

## Calendar model

The season is represented by a `SeasonCalendar` object that owns:

- season identity (year, league name)
- start date and end date for the regular season
- a list of `GameDay` entries from start to end
- a pointer to the current `GameDay`
- ordered lists of off-days, doubleheaders, and league-wide rest days

Each `GameDay` carries:

- the date
- the list of `ScheduledGame` entries for that day (zero or more)
- a flag for "off-day" / "all-star break" / "league rest day"
- doubleheader split markers when relevant

The calendar should be deterministic from a seed plus a season config so two runs of the same season produce identical day-by-day output. This is the season-scale extension of the single-game determinism rule from `plans/02 § Determinism and debugging`.

### Off-days and rest

Off-days are not absent days — they are explicit calendar entries with no scheduled games. The fatigue and recovery model from `12-fatigue-injuries-and-availability.md` consumes these explicitly: an off-day is a day on which fatigue decreases and rest counters advance, but no game state moves.

### Doubleheaders

A `GameDay` with two `ScheduledGame` entries is a doubleheader. The daily loop processes them in order. Player availability for the second game must be recomputed after the first game, because pitchers used in game one cannot pitch game two without an explicit rule allowance.

For v1, doubleheaders can be modeled as "both games are 9 innings" — the seven-inning doubleheader experiment is configurable later.

## Daily simulation loop

The season engine runs the following sequence on each `GameDay`:

1. Mark the day's start; emit `DayStarted` event
2. For each `ScheduledGame` on this day:
   1. Compute or refresh the `AvailabilitySnapshot` for all active players (per `plans/12 § Availability snapshot`). For game two of a doubleheader this is a fresh recompute, not a reuse of the day's first snapshot.
   2. Build a `GameState` for both teams using their current rosters, fatigue, injuries, and the just-computed snapshot
   3. Have each team's manager submit a daily lineup card via the manager command layer (see `14-manager-command-layer.md`)
   4. Validate lineup-card legality (extends `plans/09 § Legality layer` with active-roster constraints from `11-roster-and-transaction-rules.md`)
   5. Call the existing single-game engine (`src/sim/game_engine.zia`)
   6. Normalize the completed game into a `CompletedGameRecord`
3. Apply post-game updates:
   1. Append every `CompletedGameRecord` to the season's completed-game ledger; aggregate season-long player stat totals from those records
   2. Update fatigue state for all players who appeared
   3. Advance pitcher rest counters
   4. Roll injury rolls for players who appeared (per `12-fatigue-injuries-and-availability.md`)
   5. Recompute standings from the completed-game ledger (or invalidate the in-memory standings cache so the next read recomputes them); never write standings as authoritative state
   6. Process pending IL transitions
4. If the day is an off-day (no games), still apply recovery rules
5. Advance the calendar pointer to the next `GameDay`
6. Emit `DayEnded` event

The loop terminates when the calendar pointer passes the season end date or when a "stop after N days" debug flag fires.

## Completed-game record

The season layer should not scrape arbitrary fields from a final `GameState`.
Each completed game should be normalized into a stable `CompletedGameRecord`.

Suggested fields:

- game id, date, home team id, away team id
- ballpark id and ruleset id used
- final score and innings played
- win/loss outcome
- per-team box totals
- per-player batting and pitching lines
- the list of `OfficialPlayResult` entries, or a stable digest if full play storage is too heavy
- manager decision-log reference for this game
- deterministic game seed used

This record is the authoritative input for:

- standings recomputation
- season stat aggregation
- persistence
- replay verification
- later historical reports

### What the loop does NOT do

- It does not call the single-game engine directly with raw player references — it goes through `LineupCard` selection so the active roster constraints from `plans/11` are honored.
- It does not mutate player ratings between games. Ratings are slowly-moving truth (see `13-player-development-and-aging.md`); only the dynamic state (fatigue, injuries, IL counters) changes during the season. Player age is *derived* from `date_of_birth` and the current simulation date — it is never incremented as stored mutable state.
- It does not run any post-game UI. Output is event-driven, same as the single-game adapters in `plans/02 § Output Adapters`.

## Schedule format

A `Schedule` is the input to the calendar. It maps each `GameDay` to zero or more `ScheduledGame` entries.

For v1, schedules should be representable in two ways:

- **Programmatically generated** — round-robin with home/away balancing across N teams over M weeks. The generator is deterministic from a seed.
- **Loaded from a fixture file** — a simple text format the engine can parse for testing or for replaying historical schedules.

Each `ScheduledGame` carries:

- home team id
- away team id
- ballpark id (defaults to home team's home park)
- ruleset id (defaults to the league ruleset; allows interleague variation later)
- planned game number (for the season; needed for tiebreakers)

### Round-robin generation rules

For N teams, each team plays every other team K times across the season (K is configurable). Home/away balance: each pair `(A, B)` plays exactly half its games at A's park and half at B's. Odd-K cases get one extra home game alternated by season.

For v1, the engine should support:

- a single league of 8–16 teams
- 60–162 games per team
- one or two off-days per week
- one mid-season "all-star break" off-day window

Divisions and inter-league play are optional v1 features that the schedule generator may or may not produce; the data model should leave room for them.

## Standings model

Standings are derived state, not stored state. They are recomputed from completed games at the end of each game day.

For each team, the `Standings` row carries:

- wins
- losses
- run differential (runs scored − runs allowed across all completed games)
- division id (when divisions exist)
- division wins / losses
- head-to-head record vs every other team in the league
- games-back from the division leader
- "magic number" to clinch (computed late-season; defer if complex)

### Tiebreakers

Do not hardcode a claimed "standard MLB" order unless the exact rule set for that
era is encoded. MLB tiebreak rules have changed over time.

For v1, the tiebreaker order should be league-configurable. A sensible default is:

1. Head-to-head record
2. Intra-division record (when divisions exist)
3. Run differential
4. Deterministic seeded draw

If exact MLB-era behavior is required later, it belongs in the ruleset config, not
in an undocumented hardcoded assumption.

## Playoff hook

The regular season produces a `RegularSeasonResult` containing the final standings, the qualifying teams, and a seeded bracket for whatever post-season format the ruleset specifies.

For v1, the playoff hook can be:

- a noop (regular season ends with a final standings table; no post-season)
- a simple single-elimination bracket from the top N seeds
- the actual MLB Wild Card → Division Series → LCS → World Series structure

The plan owns the *interface* — the season ends, returns a `RegularSeasonResult`, and a separate `PlayoffEngine` (defined in a later plan or in the ruleset config) consumes it. Specifics are deferred.

## Between-game state propagation

The season layer is the only place where the following state survives across games:

- player season stat totals (extends `plans/01 § Box Score State` from per-game to season-long)
- pitcher rest counters
- player fatigue state (cumulative)
- player injury state and IL stints
- roster compartments and active role tags (see `11-roster-and-transaction-rules.md`)
- completed-game log and any in-memory standings cache
- calendar pointer

The single-game engine never reads or writes these. The season layer copies the relevant slice into a fresh `GameState` at the start of each game (e.g., today's active roster becomes the team's roster for that game) and copies the result back out at the end.

## Determinism

Season simulation must remain deterministic from a single seed. The seed plumbing extends the single-game rule from `plans/02 § Determinism and debugging`:

- the season seed is stored on `SeasonCalendar` at construction
- the single-game RNG is seeded per-game from a derivation of the season seed and the game number, so re-running game N produces the same result regardless of whether games 1..N-1 were run
- injury rolls, development draws, and any other between-game RNG use a separate stream derived from the same seed

Two runs of the same season seed must produce byte-identical season output.

## What to defer

For the v1 season layer:

- travel modeling and time-zone effects on player fatigue
- doubleheader splits (single-admission vs day-night)
- inter-league play and unbalanced schedules (model supports them; generator does not produce them)
- weather postponements
- mid-season trade deadline and waiver claims (deferred to a later franchise plan)
- All-Star game and All-Star break selection
- expanded September roster rules
- minor-league season and minor-league standings

## Failure modes to watch

### 1. Season state desync from game state

If a game completes but the season layer fails to normalize the result into a `CompletedGameRecord` and append it to the ledger, the next game starts with stale roster data. Symptom: a player's hits go up by 0 across a winning offensive game. The `CompletedGameRecord` (which carries the per-player batting and pitching lines built from `OfficialPlayResult`) is the single source of truth for season aggregation; verify with an invariant check that ledger length equals completed-game count.

### 2. Off-by-one calendar bugs

Common pitfall: the season ends one day early or one day late because the loop terminator uses `<` instead of `<=`. The invariant check should be: total scheduled games equals total completed games at season end.

### 3. Hung games inflate season time

If the single-game engine ever fails to terminate (unbounded innings, infinite plate appearance), the season loop hangs. The season layer should enforce a per-game inning cap (e.g., 30 innings) and emit a `GameAborted` event if reached.

### 4. RNG stream collision between games

If the season layer accidentally reuses the same per-game seed for two games, both produce identical output. Each game's seed must derive uniquely from `(seasonSeed, gameNumber)`.

### 5. Standings recomputation bugs

If standings are stored rather than derived, an off-by-one game outcome corrupts them permanently. Recompute standings from the completed-game log on demand, not incrementally, until the engine is well-tested.

### 6. Player references aliased across season state and game state

Players are reference-typed in Zia (`class Player`). If the season layer hands the same `Player` object to consecutive games and the game engine mutates it, the change leaks across games. The game layer should treat `Player` as read-only; mutable per-game stats live in `BattingLine` / `PitchingLine` from `plans/01`.

## Recommended next dependency

Once the season simulation architecture is accepted, the next planning layer is `11-roster-and-transaction-rules.md`. The daily loop above assumes a "current active roster" concept that does not yet exist in `plans/00`–`09` — only single-game lineup cards do. Plan 11 owns that concept.
