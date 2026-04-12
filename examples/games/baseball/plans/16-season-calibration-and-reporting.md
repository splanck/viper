# Season Calibration And Reporting

## Goal

Define how the simulation is measured and tuned at full-season and multi-season scale.

`plans/06-calibration-and-data-plan.md` established the principle that realism must be measurable, not intuitive. That plan was focused on the single-game engine and early aggregate bands. This plan extends the same discipline to the season layer from `plans/10`–`15`: schedule effects, pitcher rest, bullpen usage, injuries, standings shape, player development, and franchise drift across years.

## Core principle

Season realism is an output-distribution problem, not a handful of hand-picked box scores.

A believable baseball manager sim does not prove itself because one game "looked right." It proves itself because:

- league-wide run environment is stable
- starter and reliever usage look plausible
- team records and playoff races look plausible
- injuries and roster churn feel plausible
- multi-season talent drift does not collapse or explode

The calibration layer therefore owns *distribution targets*, *reports*, *reproducible harnesses*, and *acceptance thresholds*.

## Why this layer matters

Without season-scale calibration:

- fatigue can exist in code but not produce realistic pitcher usage
- roster rules can exist in code but not force realistic roster construction
- injuries can exist in code but be too rare, too common, or too concentrated
- development can exist in code but create immortal stars or universal collapse
- standings can "work" but produce too many .500 clusters or too few elite teams

This is the layer that turns the implemented season engine into a simulation that can be defended statistically.

## Relationship to prior plans

This plan is the season-scale continuation of:

- `plans/06-calibration-and-data-plan.md` — single-game output targets
- `plans/10-season-simulation-architecture.md` — season loop and `CompletedGameRecord`
- `plans/12-fatigue-injuries-and-availability.md` — cross-game workload and injuries
- `plans/13-player-development-and-aging.md` — off-season drift
- `plans/15-save-format-and-persistence.md` — reproducible replay and archived outputs

It also builds directly on the implemented probes:

- `examples/games/baseball/calibration_probe.zia`
- `examples/games/baseball/season_probe.zia`
- `examples/games/baseball/persistence_probe.zia`

Those are useful starting probes, but they are not yet a complete realism harness.

## Calibration scopes

The reporting and calibration layer should operate at four scopes.

### 1. Single-game scope

Already partially covered by `plans/06`. Keep the following:

- play-termination invariants
- plate appearances per game
- scoring bands
- hit/walk/strikeout/home-run bands
- error-rate sanity

This remains the fast smoke layer.

### 2. Single-season scope

This is the first new core scope. Over one full season, measure:

- league totals and per-game rates
- team-level totals and distributions
- player usage patterns
- standings shape
- injury totals and missed-time totals
- roster churn counts

This is the main realism loop for everyday tuning.

### 3. Multi-season scope

Over 10, 25, or 100 simulated seasons, measure:

- aging curve outcomes
- prospect development outcomes
- career length distributions
- star persistence and decline
- injury accumulation
- league environment drift
- parity drift across clubs

This catches long-term simulation rot that a one-season harness cannot see.

### 4. Intervention scope

The manager game also needs *decision sensitivity* checks:

- how much does resting players reduce injuries?
- how much does bad bullpen management hurt results?
- how much does lineup quality matter?
- how much does platoon awareness matter?

If these effects are zero, the manager fantasy collapses. If they are too large, the sim becomes gamey.

## Reporting outputs

The calibration system should produce a structured `CalibrationReport`.

Suggested major sections:

- `LeagueEnvironmentReport`
- `TeamDistributionReport`
- `PlayerUsageReport`
- `PitchingUsageReport`
- `InjuryAndAvailabilityReport`
- `DevelopmentAndAgingReport`
- `StandingsShapeReport`
- `ReplayIntegrityReport`

These reports should be human-readable text first, with export-friendly line formats second.

### Human-readable summaries

For each run, emit:

- season count simulated
- ruleset / era profile used
- run environment summary
- top out-of-band metrics
- pass / warn / fail summary

### Machine-friendly outputs

Also emit:

- delimited row output for spreadsheets
- per-season snapshot files
- per-team summary files
- per-player career summary files

This makes it possible to compare two tuning branches without manually reading long logs.

## Target metric families

The realism harness should track at least the following metric families.

### League offense

- runs per game
- hits per game
- walks per game
- strikeouts per game
- home runs per game
- HBP per game
- doubles / triples rates
- BABIP
- OBP / SLG / OPS distributions
- left/right split performance

### League pitching

- average starter innings
- average reliever innings
- starter pitch counts
- reliever appearances per week
- saves / holds / blown saves distribution
- complete games and shutouts
- times-through-order exposure

### League defense and baserunning

- errors per game
- double plays per game
- sacrifice flies
- stolen base attempts and success
- first-to-third and second-to-home advancement rates
- infield-hit rate

### Team-level shape

- wins and losses
- run differential
- cluster luck gap between expected and actual record
- team offensive and pitching rank spread
- rest-day utilization and roster usage

### Standings and pennant-race shape

- division race margins
- wild-card race margins
- 90-win and 100-win team frequency
- last-place collapse frequency
- tie frequency

### Injury and availability

- total injuries per season
- day-to-day vs IL-10 vs IL-60 split
- pitcher vs hitter injury rates
- catcher rest patterns
- average days missed per injury
- roster replacements triggered by injury

### Development and aging

- average rating delta by age band
- prospect growth rate by age
- veteran decline rate by age
- career peak age by skill family
- durability drift by age
- distribution of five-year outcomes for tagged player archetypes

## Era and league target profiles

Do not hardcode "modern MLB" as the only truth.

The calibration system should use an explicit `EraTargetProfile` or `LeagueTargetProfile` attached to the ruleset. The target profile carries:

- target rate bands
- target distribution bands
- tiebreak rules when needed for standings interpretation
- league size and schedule assumptions
- roster and bullpen assumptions

For v1, one profile is enough:

- `modern_mlb_like`

But the architecture should assume multiple future profiles:

- dead-ball style
- high-offense late-1990s style
- fictional custom league profile

## Harness modes

The calibration system should support several run modes.

### Quick regression

- 20 to 50 games
- catches catastrophic drift quickly
- used in local development

### Full-season regression

- one full season
- validates standings, injuries, bullpen usage, and roster churn
- used before merging significant gameplay changes

### Monte Carlo season sweep

- 50 to 500 seasons
- validates distributions and long-tail behavior
- used during major tuning passes

### Focused sensitivity runs

- fixed rosters, altered one subsystem
- examples: injuries off vs on, strict rest vs aggressive use, strong platoon AI vs neutral AI

These runs isolate whether a given system has the intended leverage.

## Acceptance policy

The plan should distinguish three result classes.

### Hard failures

A run fails immediately if any invariant breaks:

- completed games do not match scheduled games
- wins do not equal losses across the league
- impossible roster counts
- impossible stat lines
- replay mismatch after save/load

### Soft warnings

A run warns if:

- one metric leaves the preferred band but remains inside a wider tolerance
- a team or player outlier is plausible but uncommon
- one subsystem drifts while the rest remain stable

Warnings should not block iteration but should be recorded.

### Tuning failures

A run fails tuning if:

- multiple key metrics leave target bands
- one subsystem consistently drifts in the same direction across seeds
- multi-season drift compounds year over year

This is the realism gate for shipping-quality tuning.

## Comparison workflow

Every major simulation change should be checked with:

1. quick regression
2. full-season regression
3. one multi-season sweep if the change touches fatigue, injuries, development, or roster logic

The report should include:

- baseline branch values
- candidate branch values
- delta per metric
- pass / warn / fail by metric family

This keeps tuning debates factual instead of anecdotal.

## Persistence and archival

Calibration outputs should be saved in a stable run directory with:

- run metadata
- seed and ruleset identifiers
- summary report
- per-season summaries
- optional archived completed-game digests

These artifacts should be loadable and diffable, just like normal franchise saves in `plans/15`.

## What to defer

For v1:

- automatic parameter search / optimization
- external chart generation
- historical MLB data import tooling
- probabilistic uncertainty intervals around every metric
- distributed or cloud batch simulation
- machine-learning based tuning

## Failure modes to watch

### 1. Tuning to the mean only

If calibration only checks league-average rates, the sim can hide broken distributions underneath correct totals. Always report both totals and spread.

### 2. Overtuning to one season seed

If one seed is treated as truth, the sim becomes brittle. Use seed sweeps.

### 3. Good league totals with bad usage patterns

A sim can hit runs/game while using closers for 120 innings or starters for 40 starts. Usage reports are first-class, not optional.

### 4. Multi-season drift hidden by one-season reports

A one-season run can look excellent while prospects never develop or 35-year-olds never decline. Multi-season reports must exist.

### 5. Manager decisions with no measurable impact

If lineup quality, rest, bullpen choices, and platoons barely move outcomes, the game is not a manager sim. Sensitivity runs should detect this.

### 6. Calibration report not tied to a ruleset profile

If the target assumptions are undocumented, a "failing" report may actually be measuring the wrong baseball context. Every run must name its profile.

## Recommended next dependency

Once season calibration and reporting are accepted, the next planning layer is `17-human-manager-mode.md`.

The reason is practical: season-scale realism reporting gives the project a stable baseline, and the next major product-facing capability is letting a real player manage against that baseline through the already-defined manager command contract.
