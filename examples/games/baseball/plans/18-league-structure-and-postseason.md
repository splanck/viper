# League Structure And Postseason

## Goal

Define the full competition structure around the season engine: leagues, divisions, schedule shape, standings interpretation, tie rules, and postseason flow.

`plans/10-season-simulation-architecture.md` established a season loop and left the playoff hook abstract. This plan turns that abstract hook into a real competition model closer to what players expect from a full baseball management game.

## Core principle

Competition structure must be data-driven, era-aware, and separate from single-game logic.

The game engine should not know whether a club is in the AL East, a fictional two-division league, or a one-table independent circuit. That structure belongs to league configuration and postseason orchestration.

The same rule applies to tie rules and playoff formats: they are not hardcoded baseball truth; they are league-profile truth.

## Why this layer matters

Without a real league structure:

- standings are just a flat table
- schedule realism is limited
- playoff races have no real shape
- postseason management does not exist
- franchise planning has no larger context than "win as many games as possible"

This is the layer that turns a season simulator into a real baseball world.

## Relationship to prior plans

This plan depends on:

- `plans/10-season-simulation-architecture.md`
- `plans/11-roster-and-transaction-rules.md`
- `plans/15-save-format-and-persistence.md`
- `plans/16-season-calibration-and-reporting.md`

It extends the implemented season engine in `src/season/season_engine.zia`, which currently supports a single small league and no true postseason.

## League model

The competition layer should introduce explicit league objects.

Suggested first-cut structure:

- `League`
  - id
  - name
  - list of divisions
  - league ruleset reference
  - schedule profile
  - postseason format

- `Division`
  - id
  - name
  - ordered club list

- `ClubMembership`
  - team id
  - league id
  - division id

This allows the same club object from the season layer to be slotted into different competition structures without changing the club itself.

## Schedule requirements

This layer should move beyond the simple round-robin assumptions of `plans/10`.

### Required schedule capabilities

- 162-game profile support
- unbalanced intra-division frequency
- inter-division play
- optional interleague play
- explicit all-star break
- optional doubleheaders
- rainout make-up slots later

For v1 of this layer, full historical schedule fidelity is not required, but the model must no longer assume "every team plays every team equally."

### Schedule profile object

A `ScheduleProfile` should carry:

- games per team
- games versus division opponents
- games versus non-division same-league opponents
- games versus other leagues
- off-day pattern
- all-star break window
- doubleheader policy

The generator consumes this profile and league/division membership to produce the schedule.

## Standings interpretation

Once divisions and leagues exist, standings are no longer just one ordered table.

The reporting layer should support:

- overall league standings
- per-division standings
- wildcard standings
- elimination numbers and magic numbers
- head-to-head and tiebreak summaries

### Derived state

As in `plans/10`, standings remain derived from completed games. Division leaders, wildcard seeds, and elimination status are also derived, not stored as independent authoritative truth.

## Tie rules

Do not claim "MLB standard" without encoding a specific era.

Tie logic should be attached to the league competition profile:

- regular-season tie handling
- playoff seeding tiebreak handling
- game-163 era vs no-game-163 era
- home-field tiebreak handling

For a modern-MLB-like default, use deterministic tiebreak ordering rather than ad hoc manual logic.

## Postseason model

The postseason should be its own engine, not an if-statement bolted into the season loop.

Suggested objects:

- `PostseasonFormat`
- `PostseasonBracket`
- `SeriesState`
- `PostseasonGameRecord`
- `PostseasonResult`

### Series model

Each series should carry:

- round
- seed matchup
- current wins per side
- home-field pattern
- active roster lock rules
- game results

The single-game engine remains unchanged; the postseason layer schedules postseason games and aggregates series state.

### Default supported formats

The model should support at least:

- no postseason
- simple top-N single elimination
- MLB-like wildcard / division series / LCS / championship series stack

The product can start with one default, but the format object should not assume only one forever.

## Postseason roster rules

Postseason baseball is not exactly regular-season baseball.

The plan should allow:

- explicit postseason active rosters
- postseason roster lock deadline
- optional roster re-submission between rounds
- expanded bullpen emphasis

For v1, this can be simplified to "active roster selected at postseason start and held per round," but the hook must exist.

## Competitive context outputs

This layer should support richer season outputs:

- division race summary
- wildcard race summary
- clinch and elimination events
- postseason bracket view
- per-series summaries
- championship history

These outputs are required both for franchise immersion and for the broadcast/narrative layers later.

## Interaction with calibration

This layer changes what "realistic" means at season scale.

`plans/16` should add competition-structure metrics such as:

- division winner win totals
- wildcard cutoff distribution
- tie frequency
- postseason upset rate
- pennant / title concentration across clubs

Without these, the competition structure can function mechanically while still feeling wrong.

## Interaction with franchise mode

League structure changes what franchise decisions matter.

Examples:

- in a weak division, a team may buy for a wildcard race differently than in a one-table league
- postseason roster rules affect bench and bullpen construction
- interleague rules can affect DH assumptions and roster composition

That is why this layer should land before deep franchise systems are fully designed.

## What to defer

For v1:

- exact historical schedule import
- weather postponements and make-up logic
- neutral-site special events
- all-star game implementation
- luxury tax / competitive balance mechanics
- playoff revenue sharing
- international series and travel demands

## Failure modes to watch

### 1. Hardcoded modern-MLB assumptions

If divisions, tie rules, and playoff format are hardcoded, every future ruleset becomes a rewrite.

### 2. Schedule profile and standings profile drifting apart

If the generator assumes one league shape and standings logic assumes another, wildcard and division reports become nonsense.

### 3. Postseason using regular-season assumptions blindly

If postseason roster and home-field logic are not modeled explicitly, playoff management will feel like regular season with a different label.

### 4. Derived state being saved as authoritative truth

If playoff seeds or division leaders are stored independently from completed-game truth, replay and load integrity will drift.

### 5. Division and wildcard races not reflected in reports

If the game knows the structure internally but the outputs stay flat, the product value is lost.

## Recommended next dependency

Once league structure and postseason are accepted, the next planning layer is `19-franchise-mode.md`.

The competitive frame defines what roster-building, budget, scouting, and transaction decisions actually mean. Franchise mode should be designed against that frame, not before it.
