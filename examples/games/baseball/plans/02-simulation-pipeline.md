# Simulation Pipeline

## Principle

The simulation should advance through small, explicit steps.

Do not jump directly from "batter comes up" to "single to center" without preserving how the engine reached that result. The engine should be inspectable at the pitch and plate appearance levels.

## Proposed engine layers

### 1. Game Engine

Owns:

- game setup
- inning loop
- game completion
- end-of-game conditions

### 2. Half-Inning Engine

Owns:

- current batting team
- outs
- lineup progression
- half-inning termination

### 3. Manager Policy Interface

Owns:

- tactical decision checkpoints
- substitution decision checkpoints
- default strategy decisions in early phases

The engine should call this interface at explicit safe points.
The first implementation can return deterministic defaults.

### 4. Plate Appearance Engine

Owns:

- count progression
- pitch loop
- batter vs pitcher interaction
- plate appearance result

### 5. Ball-In-Play Engine

Owns:

- contact generation
- trajectory generation
- fielding resolution
- advancement opportunities

### 6. Runner Advancement Engine

Owns:

- forced advancement
- optional advancement
- scoring
- outs on bases
- double play and sacrifice handling

### 7. Official Result Builder

Owns:

- construction of the canonical `OfficialPlayResult`
- adjudication of batter result, runner result, and scoring result
- pitcher-responsibility attribution, including inherited runners
- separate official scoring flags such as intentional walks, strikeout type, and sacrifice credit
- the bridge between raw simulation trace and official baseball bookkeeping

### 8. Scoring And Stats Engine

Owns:

- applying `OfficialPlayResult` to live game state
- official scoring updates
- line score
- player and pitcher stat lines

### 9. Event Emitter

Owns:

- structured event production for every meaningful transition

The event emitter should not contain baseball logic. It should report what happened.

## Recommended play sequence

### Game start

1. Load ruleset, park, lineups, pitchers, and seed
2. Initialize empty game state
3. Emit `GameStarted`

### Half inning

1. Set batting team and fielding team
2. Reset outs to 0
3. Emit `HalfInningStarted`
4. Loop plate appearances until outs equals 3
5. Emit `HalfInningEnded`

### Plate appearance

1. Select batter and pitcher
2. Offer a manager-decision checkpoint if strategy or substitution applies
3. Emit `PlateAppearanceStarted`
4. Enter pitch loop
5. End on:
   - walk
   - strikeout
   - hit by pitch
   - ball in play
   - rare early-termination events (added in a later phase)
6. Emit `PlateAppearanceEnded`

### Transient play-resolution state

Every pitch result should resolve through a temporary `PlayResolutionState`.

That object exists so the engine can represent:

- force chains
- tag-up eligibility
- in-flight runner advancement
- pending outs
- dead-ball vs live-ball transitions

It should be created for a pitch or ball in play, resolved into one
`OfficialPlayResult`, then discarded.

### Pitch loop

Each pitch should conceptually run through these steps:

1. Choose pitch intent
   - attack zone
   - waste pitch
   - chase pitch
   - pitch around

2. Generate pitch result inputs
   - pitch type
   - effective location
   - effective quality

3. Batter decision
   - swing
   - take

4. If take:
   - called ball
   - called strike
   - hit by pitch

5. If swing:
   - miss
   - foul
   - ball in play

6. Update count and emit `PitchResolved`

This gives enough structure for realistic pitch counts and later manager strategy.

## Ball-in-play pipeline

When contact happens:

1. Generate contact quality
   - weak
   - average
   - hard

2. Generate contact shape
   - ground ball
   - line drive
   - fly ball
   - pop-up

3. Generate direction
   - pull
   - center
   - opposite field
   - exact zone or angle later

4. Convert to physical-ish values
   - exit velocity
   - launch angle
   - spray angle
   - hang time

5. Resolve against field and defense
   - immediate home run check
   - catchable air ball check
   - infield-fly eligibility check
   - infield play check
   - outfield pickup and relay check
   - error chance check

6. Resolve runner movement
   - force advancement
   - tag and score
   - extra base attempts

7. Build `OfficialPlayResult`
   - batter result
   - outs added
   - runs scored
   - run charges by responsible pitcher
   - intentional-walk and strikeout classification when relevant
   - errors and fielder's choice
   - base state before and after

8. Apply official result to game state and stats

9. Emit events
   - `BallInPlay`
   - `FieldingAttempt`
   - `RunnerAdvanced`
   - `RunScored`
   - `OutRecorded`

## State machine outputs

The engine should emit structured results, not only text.

Use two categories of output:

- trace events
  - pitch and fielding-level detail used for debugging and later broadcast presentation

- official result objects
  - one canonical `OfficialPlayResult` per completed play or plate appearance
  - used for stats, box score, and authoritative text output

Suggested event types:

- `GameStarted`
- `HalfInningStarted`
- `PlateAppearanceStarted`
- `PitchResolved`
- `BallInPlay`
- `OutRecorded`
- `RunnerAdvanced`
- `RunScored`
- `PlateAppearanceEnded`
- `HalfInningEnded`
- `GameEnded`

Each event should carry enough data for:

- text narration
- debugging
- future animation
- box score updates

`PlateAppearanceEnded` should carry or reference the `OfficialPlayResult` rather
than forcing downstream systems to reconstruct official scoring, stat credit, or
pitcher responsibility from raw events.

## Text output shape

For the first stage, text should be generated from events.

Example shape:

```text
Top 1st - Away batting
Smith leads off against Johnson
Pitch 1: Ball, low and away. Count 1-0.
Pitch 2: Called strike on the outside corner. Count 1-1.
Pitch 3: Smith lines a single into left.
Runner on first, 0 out.
Jones batting.
...
End Top 1st: Away 1, Home 0
```

The text adapter should be replaceable later.
It should consume `OfficialPlayResult` plus optional trace events, not arbitrary
mutable game state.

## Determinism and debugging

Every simulation run should support:

- a fixed seed
- a compact debug trace
- replaying the same game exactly

Useful debug features for the first stage:

- log each random draw category
- log count state before and after each pitch
- log the transient play-resolution state for complex plays
- log the final `OfficialPlayResult`
- log why a ball in play became hit, out, or error

That will save a large amount of time once weird baseball edge cases appear.
