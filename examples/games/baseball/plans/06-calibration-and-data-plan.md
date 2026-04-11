# Calibration And Data Plan

## Goal

Define how the baseball simulation will be checked against reality.

Without a calibration plan, "realistic baseball outcomes" becomes subjective and
drifts as features are added.

## Core principle

Calibrate against distributions, not anecdotes.

A few believable box scores do not prove the engine is good.
The engine needs to look plausible at:

- plate-appearance scale
- game scale
- season scale

## Three validation layers

### 1. Invariant validation

These are correctness checks, not realism checks.

Examples:

- outs never exceed 3 in a half inning
- no runner is duplicated
- no illegal lineup-card state appears
- every completed play has exactly one official result
- stats reconcile with score and line score

If these fail, realism metrics are not meaningful yet.

### 2. Structural baseball validation

These check whether the simulation "looks like baseball" before statistical fine tuning.

Examples:

- plate appearances commonly last realistic numbers of pitches
- grounders, flies, liners, and pop-ups all occur in believable proportions
- walks and strikeouts occur often enough to matter but do not dominate all offense
- starter workloads feel plausible
- extra-base hits are not abnormally common or rare

### 3. Aggregate calibration

These compare the simulation against target statistical ranges over many games.

Examples:

- batting average
- OBP
- SLG
- OPS
- BABIP
- K%
- BB%
- HBP%
- HR%
- IBB%
- SF%
- GIDP rate
- error rate
- runs per game
- PA per game
- pitch counts
- starter innings
- bullpen share
- inherited runners scored rate

## Recommended initial targets

The exact target season and league environment should be chosen explicitly later.
For now, the planning assumption should be:

- modern MLB-like run environment
- modern MLB-like strikeout environment
- modern MLB-like bullpen usage
- modern MLB-like three-batter-minimum bullpen constraints

Do not calibrate against "all-time baseball" averages unless the ruleset is era-aware.

## Data sources for later calibration

This planning phase does not require loading live data, but the model should assume
eventual comparison against:

- league-level seasonal summary data
- team-level game logs
- play-by-play or event-level distributions
- park-factor references

The key planning point is that the engine should make these comparisons possible.

## What to measure early

As soon as the full game loop exists, produce a calibration report with at least:

- games simulated
- total plate appearances
- total runs
- batting average
- on-base percentage
- slugging percentage
- strikeout rate
- walk rate
- intentional-walk rate
- hit-by-pitch rate
- sacrifice-fly rate
- grounded-into-double-play rate
- error rate
- singles, doubles, triples, and home runs
- BABIP
- average pitches per plate appearance
- average starter innings
- average reliever appearances
- inherited runners
- inherited runners scored

## Recommended calibration workflow

### Step 1: Lock the ruleset

Do not calibrate while the ruleset is still moving.

Examples:

- DH or no DH
- automatic runner or not
- lineup and roster legality assumptions

### Step 2: Use fixed synthetic rosters first

Before importing any real or fictional league universe, verify the engine against:

- symmetric league rosters
- clearly different hitter archetypes
- clearly different pitcher archetypes

This isolates engine behavior from roster-quality noise.

### Step 3: Run bulk simulations

Useful scales:

- 100 games for smoke trends
- 1,000 games for clearer distributions
- 10,000+ games for stable aggregate tuning

### Step 4: Compare to target bands

Do not chase exact single values immediately.
Use acceptable target windows first.

### Step 5: Adjust the correct layer

Examples:

- if strikeouts are too high, inspect whiff and two-strike foul logic
- if doubles are too low, inspect batted-ball and outfield conversion logic
- if runs are too low but hits are normal, inspect runner advancement and sequencing

Do not solve league-level problems with random global hacks if a specific layer is responsible.

## Important calibration splits

Aggregate league numbers are not enough.

Also compare:

- LHP vs LHB
- LHP vs RHB
- RHP vs LHB
- RHP vs RHB
- hitter counts vs pitcher counts
- starter plate appearances vs reliever plate appearances
- home vs away run scoring
- hitter types
  - contact hitters
  - power hitters
  - all-around hitters

If only the total averages match, the engine can still be deeply wrong.

## Archetype validation

The engine should support believable baseball archetypes.

Test players should include:

- high-contact low-power hitter
- three-true-outcome slugger
- balanced star hitter
- speed-first leadoff hitter
- high-stuff wild pitcher
- command-first low-whiff starter
- ground-ball specialist
- dominant late-inning reliever

The point is to verify that ratings create qualitatively different baseball outcomes.

## Suggested tooling outputs

Eventually the calibration harness should emit:

- summary text report
- CSV or structured table output
- per-game sampled box scores
- optional pitch and PA distribution histograms

For now, the plan should assume a text and CSV-capable reporting path.

## Data-model implications

To support calibration, every completed plate appearance should make the following easy to count:

- pitch count
- batter result
- pitcher result
- hit type
- batted-ball type
- handedness matchup
- inning and score context
- runner state before and after

If those are hard to extract later, the model is not instrumented enough.

## Recommended early acceptance gates

Before moving beyond single-game backend work, the simulation should be able to pass:

- invariant suite: no state corruption
- short-run structure suite: believable baseball flow over dozens of games
- bulk-run aggregate suite: reasonable statistical bands over large samples

That three-part gate is more meaningful than "it felt good in one game."

## What not to do

- Do not calibrate from one hand-picked showcase game
- Do not import a huge real-player dataset before the core engine is stable
- Do not tune only league batting average and ignore shape of offense
- Do not let text narration masquerade as realism evidence

## Recommended next planning dependency

Once the plate-appearance model is accepted, the next useful planning layer is:

- batted-ball and fielding conversion
- runner advancement heuristics
- first calibration harness design
