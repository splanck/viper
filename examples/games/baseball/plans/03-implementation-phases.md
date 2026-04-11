# Implementation Phases

## Strategy

Build the simulation in thin vertical slices that always end with something runnable.

The correct sequence is:

1. Build the rules-safe core
2. Build the plate appearance loop
3. Build the inning loop
4. Build the full game loop
5. Add realism and calibration

Do not start with season mode or presentation.

## Suggested file layout

This is only a draft, but a clean backend layout would look like:

```text
examples/games/baseball/
  main.zia
  model/
    player.zia
    team.zia
    lineup_card.zia
    lineup.zia
    ballpark.zia
    ruleset.zia
    manager_policy.zia
    game_state.zia
    play_resolution_state.zia
    play_result.zia
    events.zia
  sim/
    rng.zia
    timing_model.zia
    pitch_engine.zia
    plate_appearance_engine.zia
    batted_ball_engine.zia
    runner_engine.zia
    inning_engine.zia
    game_engine.zia
    scorer.zia
  output/
    play_by_play_writer.zia
    line_score_writer.zia
    box_score_writer.zia
  tests/
    baseball_smoke_probe.zia
    baseball_pa_probe.zia
    baseball_inning_probe.zia
    baseball_game_probe.zia
```

## Phase 0: Simulation skeleton

### Deliverable

A runnable shell with:

- domain types
- lineup-card legality shell
- manager-policy interface with a deterministic default policy
- deterministic RNG wrapper
- empty event log
- minimal game state
- empty official play-result type
- text output driver

### Exit criteria

- program boots
- game state can initialize cleanly
- lineup cards validate for a legal starting game
- seed is accepted
- the engine can emit a fake game start and game end

### Why this phase matters

It establishes the architecture before baseball logic starts spreading everywhere.

## Phase 1: Plate appearance prototype

### Scope

Support one batter vs one pitcher with:

- count progression
- strikeout
- walk
- hit by pitch
- ball in play
- foul ball

### Simplifications allowed

- no base runners yet
- no substitutions
- no user-facing manager choices yet
- ball in play can resolve to:
  - out
  - single
  - double
  - triple
  - home run

### Exit criteria

- a single plate appearance can run from 0-0 to final result
- each plate appearance yields one canonical `OfficialPlayResult`
- the result updates batter and pitcher stats
- pitch-by-pitch text output is coherent
- deterministic replay from a seed works

### Tests

- walk requires four balls
- strikeout requires three strikes
- foul ball with two strikes does not become strike three
- ball in play immediately ends the plate appearance
- illegal count transitions are rejected

## Phase 2: Base and runner model

### Scope

Add:

- first, second, third base occupancy
- transient play-resolution state
- shared timing and distance primitives for runners, throws, and intercepts
- forced runner movement
- scoring
- outs on bases
- simple extra-base advancement rules

### Minimum supported outcomes

- single
- double
- triple
- home run
- groundout
- flyout
- forceout
- double play
- sacrifice fly

### Exit criteria

- the engine updates base state correctly
- runs score correctly
- double plays can end innings correctly
- a half inning can be simulated with coherent base/out state
- runner and out bookkeeping obey invariants during play resolution

### Tests

- walk with bases loaded scores one run
- double play records two outs
- sacrifice fly scores a runner from third when eligible
- inning ends immediately on third out
- no base is occupied by two runners at once
- the same runner cannot exist on multiple bases at once

## Phase 3: Half inning and inning engine

### Scope

Add:

- lineup rotation
- three-outs termination
- half inning transitions
- inning summaries
- remaining ordinary-game rules:
  - infield fly
  - dropped third strike

### Exit criteria

- a complete half inning runs correctly
- lineup spot advances properly across innings
- text output for inning flow is readable
- ordinary MLB game situations do not require scorer-side rule hacks

### Tests

- leadoff hitter for next inning is correct
- scoring resets only where appropriate
- base state clears at half-inning end
- dropped third strike obeys occupancy and out-count rules
- infield fly suppresses easy force-double-play artifacts

## Phase 4: Full game engine

### Scope

Add:

- home and away teams
- nine innings
- extra innings (rule-aware, default no automatic runner)
- active ruleset support for the modern three-batter minimum when enabled
- legal lineup-card state through the full game
- final line score
- simple box score

### Exit criteria

- full game simulation completes
- winner is resolved correctly
- walk-off logic works
- lineup cards remain legal through all game-end states
- pitching changes obey the active three-batter-minimum rule when enabled
- final stats reconcile with play-by-play

### Tests

- home team does not bat in bottom ninth when already ahead
- walk-off run ends game immediately
- box score totals match event log totals
- batting-order pointers reconcile with completed plate appearances
- a reliever cannot be removed early in violation of the three-batter minimum when the rule is active

## Phase 5: First realism pass

### Scope

Improve outcome logic with:

- handedness splits
- batter and pitcher talent interaction
- park effects
- defense effects
- pitch count and basic pitcher fatigue
- hit-type mix and batted-ball profile tuning

### Exit criteria

- different player profiles produce visibly different results
- strikeout and power hitters feel distinct
- strong defenders suppress some balls in play
- pitcher fatigue worsens outcomes over time
- multi-game outputs fall into believable baseball ranges before season mode exists

## Phase 6: User-facing manager controls

The simulation hooks for manager decisions should already exist from Phase 0.
This is the earliest point to expose them as real user-facing controls.

### First hooks to add

- bullpen change
- intentional walk
- pinch hitter
- pinch runner
- steal attempt toggle
- bunt attempt toggle

### Exit criteria

- pitching changes preserve inherited-runner responsibility
- a scoring runner after a pitching change is charged to the correct pitcher
- intentional walks remain separately credited from ordinary walks
- user-facing manager prompts do not include live omniscient throw-target micromanagement

### Tests

- inherited runner is still charged to the original pitcher after a pitching change
- intentional walk increments `IBB` and not only generic `BB`
- uncaught third strike still credits a strikeout when batter reaches
- live send or throw heuristics remain engine-owned even when manager controls are enabled

### Why the user-facing layer comes last

Before this point, there is not enough trustworthy game state to make management interesting.

## Calibration plan

The first backend version should include a calibration harness as soon as full games exist.

### Harness outputs

- batting average
- on-base percentage
- slugging
- intentional-walk rate
- singles, doubles, triples, and home runs
- BABIP
- strikeout rate
- walk rate
- hit-by-pitch rate
- sacrifice-fly rate
- double-play rate
- error rate
- plate appearances per game
- home run rate
- runs per game
- batters faced
- starter innings
- reliever innings distribution
- inherited runners
- inherited runners scored
- pitch counts

### Calibration method

1. Run the invariant suite before trusting any aggregate numbers
2. Sim many games with fixed rosters and fixed park assumptions
3. Compare league outputs against target ranges
4. Adjust model weights, not one-off hacks

## Cross-phase invariant suite

These checks should exist as soon as the relevant state is introduced:

- no runner is duplicated
- no base holds more than one runner
- outs stay within legal bounds and half innings terminate at 3
- batting-order pointers advance exactly once per completed plate appearance
- lineup-card legality is preserved after every substitution point
- score, line score, and player stats reconcile with `OfficialPlayResult`
- every completed play has exactly one canonical official result

## Recommended early simplifications

To get to a working baseball core faster, these are reasonable to defer:

- steals and pickoffs
- bunt detail
- wild pitches and passed balls
- advanced error taxonomy
- relief warmup logic
- platoon bench AI
- weather effects beyond simple park modifiers

## Risks to avoid

### 1. Overbuilding physics too early

If the engine starts with ball flight and collision math before baseball logic is stable, progress will slow dramatically.

### 2. Hardcoding text into the game engine

All text should be derived from events. Otherwise later TV presentation will require a rewrite.

### 3. Skipping a canonical official-result layer

If stats, text, and game state all infer outcomes independently, scoring bugs will
multiply quickly and become hard to debug.

### 4. Building season mode before the single-game engine is trustworthy

A broken game engine multiplied over 162 games only creates larger, harder-to-debug problems.

### 5. Using raw real-world stats as the only player model

The engine needs true talent ratings underneath observed results or multi-season behavior will be brittle.

## Definition of success for this initial stage

This stage is successful when:

- a full game can run from seed to final score
- the play-by-play reads like baseball
- the state transitions are trustworthy
- ordinary MLB game rules needed for day-to-day play are handled correctly
- lineup-card legality is preserved
- the engine is easy to test
- the architecture is ready for manager decisions later
