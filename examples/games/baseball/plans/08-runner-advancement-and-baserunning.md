# Runner Advancement And Baserunning

## Goal

Define how runners move once the ball is live.

This layer determines:

- who advances
- who holds
- who scores
- who is out on the bases
- whether the batter reaches safely
- whether a play becomes a fielder's choice, sacrifice, double play, or extra-base hit

This is one of the main places where baseball games stop feeling authentic if the
logic is too shallow.

## Core principle

Between pitches, base occupancy is enough.
During a live play, it is not.

The engine needs a transient runner model that can represent:

- force status
- tag-up status
- who has committed to the next base
- where the defense controls the ball
- what throw choices exist

That is exactly what `PlayResolutionState` is for.

## Required runner attributes

The runner model should consult at least:

- `speed`
- `first_step`
- `baserunning_instinct`
- `steal_jump` later
- current fatigue
- handedness only when a specific rule later needs it

Most ordinary advancement should not require a giant trait system.
It should require clean timing and reasonable heuristics.

## Recommended transient runner state

Each live runner entry in `PlayResolutionState` should track:

- runner identity
- responsible pitcher id
- origin base
- current committed target
- current advancement mode
  - forced
  - optional
  - tagging
  - retreating

- current progress along basepath
- eligible to be forced or tagged
- has already scored
- has already been retired

This state should exist only for the current play.

## Resolution philosophy

The baserunning engine should be a race resolver plus a decision layer.

It needs to answer:

- What is each runner trying to do?
- How long will it take?
- What throw will the defense choose?
- Does the runner beat the ball?

That is enough to model most normal baseball without simulating every footstep.

## Common play families the engine must support

### 1. Forced advancement

Must work for:

- walks
- hit by pitch
- ground balls with force plays
- bases-loaded scoring pushes

This is basic rules correctness, not optional realism.

### 2. Station-to-station default advancement

The engine needs believable default outcomes for:

- single with runners on
- double with runners on
- triple with runners on
- deep fly ball with runners on

This should start with heuristics plus timing margins, not purely scripted baseball-card logic.

### 3. Tag-up and sacrifice fly behavior

The engine must support:

- runner on third tagging on a catchable outfield fly
- runner on second tagging on deeper flies later
- runners holding on shallow flies

This is a routine MLB event and needs to exist by the first full-game milestone.

### 4. Ground-ball force and double-play behavior

The engine must support:

- force at second only
- 6-4-3 or 4-6-3 style double plays
- slower exchange or weak throw preventing the second out
- batter speed affecting the chance to beat the relay

### 5. Extra-base advancement on hits

The engine should support:

- first to third on singles
- second to home on singles
- first to home on doubles
- batter stretching singles into doubles later

The send or hold decision should come from runner heuristics first and a manager hook later.

### 6. Outs on the bases

Must support:

- thrown out at home
- thrown out trying for an extra base
- force out on a grounder
- tag out on a caught ball if runner commits badly

Without this, offensive sequencing becomes too generous.

## Timing model

Use approximate timing, not animation.

This layer should consume the same shared time and distance contract as the
fielding layer. Do not invent separate runner-only timing units.

Recommended convention:

- each basepath segment is represented in the same distance scale used by the park model
- runner progress is stored either as feet along the basepath or a normalized value with a fixed mapping
- all race comparisons use the same simulation-time unit as `hang_time_estimate` and `control_time`

### Runner time components

Suggested factors:

- launch delay
  - slower on read-dependent balls in play

- acceleration proxy
  - blend of `speed` and `first_step`

- route penalty
  - reduced by `baserunning_instinct`

- commit delay
  - especially important on liners, shallow flies, and balls hit in front of the runner

### Defensive time components

Suggested factors:

- control time from the fielding layer
- transfer time
- throw travel time
- catch and tag or force completion time

The baserunning engine should compare runner time to defense time, not use isolated
safe-or-out percentages.

## Decision sources

Not every advancement decision should be automatic.

Recommended layers:

### Runner heuristic layer

Handles ordinary read decisions:

- whether to tag
- whether to take the extra base
- whether to stop at the next base

Inputs:

- score and inning
- outs
- ball depth
- defender arm
- runner quality

### Manager policy layer

Handles explicit tactical intent:

- set predeclared running aggression
- safety squeeze later
- hit and run later
- steal attempt later

The early engine can keep most of these as deterministic defaults while preserving the interface.

## Recommended play-resolution order

1. Build runner entries from current base state
2. Import fielding outcome
3. Mark force chains and tag-up eligibility
4. Assign initial runner intents
5. Let defense choose highest-value throw target
6. Compare timing margins
7. Record outs and runs in legal order
8. Continue to secondary throws if the play allows it
9. Finalize base occupancy and preserve or assign responsible pitcher ids correctly
10. Build `OfficialPlayResult`

The legal-order requirement matters for scoring, fielder's choice handling, and earned-run accounting.

## Suggested early heuristics by play type

### Single

Common first-cut behavior:

- runner on first
  - often stops at second
  - sometimes takes third on hard or well-placed outfield singles

- runner on second
  - often scores with two outs
  - scores more selectively with fewer than two outs depending on depth and arm

### Double

Common first-cut behavior:

- runner on second or third usually scores
- runner on first often scores on deep doubles, sometimes held on shallow doubles or strong arms
- batter usually stops at second unless the ball reaches an extreme gap or wall carom case

### Fly out

Common first-cut behavior:

- runner on third may tag on medium or deep outfield flies
- runner on second tags only on deeper flies and stronger run environments later
- runners rarely advance on shallow catches

### Ground ball

Common first-cut behavior:

- forced runners move first
- one out or two outs depends on exchange quality, throw quality, and batter speed
- hard-hit balls through the infield can push runners aggressively

## Rules that must be represented here

The runner-advancement layer must respect:

- force rules
- tag rules
- scoring before third out only when legal
- sacrifice fly logic
- fielder's choice logic
- dropped third strike live-runner behavior
- infield fly dead-ball consequences

If these are split across too many layers, the engine will become difficult to reason about.

Ownership split:

- the plate-appearance engine triggers uncaught-third-strike behavior and creates the live batter-runner when appropriate
- the fielding layer triggers infield-fly eligibility
- the runner layer applies the advancement and force consequences for both

## What to defer

The first backend version can defer:

- detailed rundowns
- pickoffs
- balks
- wild pitch and passed-ball advancement if needed for scope control
- collisions and obstruction
- appeal plays

Those are real baseball, but they are not the highest-value first targets for a believable game engine.

## Output requirements

This layer should produce enough information for `OfficialPlayResult` to capture:

- base state before and after
- batter safe or out status
- outs recorded and where they occurred
- runners who advanced, scored, or were retired
- responsible pitcher for each runner who remains or scores
- run charges by pitcher id
- RBI eligibility
- sacrifice eligibility
- fielder's choice markers

That object should then drive scorekeeping and text.

## Failure modes to watch

### 1. Runners are too aggressive

If first-to-home or second-to-home happens too freely, run scoring inflates and baseball texture breaks.

### 2. Runners are too passive

If everyone goes station-to-station, doubles and outfield arms stop mattering.

### 3. Double plays are off

Too many or too few double plays badly distort inning shape.

### 4. Tag-up logic is weak

Sacrifice flies are a very visible realism signal.

### 5. Outs are resolved in the wrong order

This creates subtle but serious scoring and earned-run bugs.

## Recommended next dependency

Once this layer is accepted, the next planning focus should be:

- tactical decision surfaces for the manager
- season-scale bullpen and roster usage
- scoring and stats reconciliation details
