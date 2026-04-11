# In-Game Manager Decision Model

## Goal

Define how the manager interacts with the simulation during a game.

This matters because the product fantasy is not "watch baseball happen."
It is "manage baseball decisions and live with their consequences."

The simulation therefore needs decision checkpoints that are:

- legal
- deterministic
- inspectable
- isolated from UI code

## Core principle

The manager should act only at safe checkpoints against stable game state.

The manager should not mutate deep simulation internals directly.
Instead, the engine should present a well-scoped decision context and accept
a typed response.

That keeps the sim trustworthy and makes AI, scripted policy, and human control
all share the same contract.

## Information boundary

The manager decision layer should only receive information the manager is allowed to know.

That includes:

- inning, score, and outs
- count
- base occupancy
- current lineup-card state
- current pitcher and batter
- fatigue estimates that the game's rules expose
- bullpen availability
- player handedness and talent information that the mode intends to reveal

It should not receive:

- future random outcomes
- hidden exact probability rolls
- downstream event previews

If the manager layer cheats, realism and design clarity both degrade.

## Recommended decision checkpoints

### 1. Pregame lineup-card submission

Decisions:

- starting lineup
- batting order
- defensive positions
- starting pitcher
- bench availability

This is the first place lineup legality must be enforced.

### 2. Between plate appearances

Decisions:

- intentional walk
- defensive substitution
- pitching change
- pinch hitter
- pinch runner

This is the safest and most important in-game checkpoint.

### 3. Before a pitch

Decisions:

- steal attempt
- bunt intent
- hit and run
- pitch around intent
- guard against running game later

Not every tactic needs to be active in v1, but the checkpoint should exist.

### 4. Live-ball micro-decisions are not direct manager prompts

The following should stay engine-owned or staff-heuristic-owned in the first manager sim:

- lead throw target on defense
- wave or hold on a live advance
- immediate runner read-and-react behavior

The manager can influence these later through aggression profiles or team tendencies,
but not through omniscient pause-and-pick checkpoints during the play.

### 5. Between innings

Decisions:

- pitcher stays in or leaves
- bullpen warm-up state later
- defensive alignment change later

This is a natural checkpoint for low-pressure game management.

## Typed decision model

The `ManagerPolicy` interface should return typed actions, not generic flags.

Examples:

- `NoAction`
- `IssueIntentionalWalk`
- `MakeSubstitution`
- `ChangePitcher`
- `DeclareBunt`
- `DeclareSteal`
- `SetRunningAggressionProfile`

Typed actions are easier to validate and easier to log in deterministic replays.

## Legality layer

Every manager action must pass through a legality check before it mutates game state.

The legality layer should verify:

- batting-order integrity
- one-way substitution rules
- DH and pitcher interactions for the active ruleset
- pitcher eligibility and removal rules
- preservation of inherited-runner responsibility when pitchers change
- roster availability
- whether the requested action is allowed at the current checkpoint

This layer should reject impossible actions cleanly rather than letting the engine
enter broken lineup-card states.

## Recommended first rollout

Do not try to ship full tactical baseball AI in the first backend slice.

Instead, stage it.

### Stage 1: Static default policy

Behavior:

- never bunt
- never steal
- no pinch hitting
- no pinch running
- no intentional walks
- leave lineup fixed
- use a single starter unless a scripted pitching-change rule triggers

This gives the engine a simple baseline while preserving the decision API.

### Stage 2: Conservative substitution and pitcher hooks

Add:

- pitching changes based on fatigue and leverage
- pinch hitter against extreme platoon disadvantage later
- basic defensive replacement logic later

This is probably the highest-value first managerial layer for realism.

### Stage 3: Offensive tactics

Add selectively:

- steal attempts
- bunts
- hit and run
- predeclared running aggression profiles

These should be added only after the baseline run environment is stable.

## Pitching staff decisions

Pitcher management deserves its own structure because it strongly shapes realism.

The decision model should eventually support:

- starter hook thresholds
- bullpen role preferences
  - closer
  - setup
  - middle relief
  - long relief

- rest and availability constraints
- handedness matchups
- multi-inning reliever usage
- the active ruleset's three-batter-minimum constraint when enabled

Even if bullpen AI is deferred, the interface should leave room for this.

Pitching changes must also preserve the scoring model:

- the new pitcher becomes responsible for future batters
- existing base occupants keep their original `responsible_pitcher_id`

## Bench and substitution decisions

The manager layer should eventually support:

- pinch hitting
- pinch running
- defensive replacement
- double switches if the ruleset needs them later

Each of these must operate through the `LineupCard`, not through ad hoc player swaps.

## Decision context design

Each checkpoint should receive a dedicated context object rather than the full game state.

Benefits:

- easier to reason about
- easier to test
- easier to keep the information boundary honest
- easier to log during replay and debugging

Example context slices:

- `PregameLineupContext`
- `BetweenPAContext`
- `PrePitchTacticContext`
- `PitchingChangeContext`
- `RunningAggressionContext` later

## Determinism and debugging

The manager layer needs to be fully traceable.

For every decision, log:

- checkpoint type
- context summary
- legal options considered
- action chosen
- policy rationale string or code

This matters for both human debugging and future AI tuning.

## Human and AI parity

If the game later supports human management, human input should flow through the same legality and action pipeline as AI policy decisions.

That avoids an entire class of "human mode can do things AI mode cannot" bugs.
It also prevents human mode from receiving non-manager omniscient live-ball prompts.

## What to defer

The first planning and backend slice can defer:

- sophisticated opponent scouting logic
- personality-driven decisions
- morale and clubhouse effects
- bluffing and deception systems
- detailed coaching staff influence

Those systems are secondary to sound baseball-state transitions.

## Failure modes to watch

### 1. The manager layer is too thin

If important baseball decisions are hardcoded inside the engine, later manager gameplay will be painful to add.

### 2. The manager layer is too broad too early

If every baseball decision becomes interactive before the baseline engine is stable, debugging will slow to a crawl.

### 3. Illegal actions can corrupt lineup state

That is a structural bug, not just a UX bug.

### 4. The manager layer cheats

Perfect hidden-information behavior will not feel like baseball management.

## Recommended next dependency

Once the in-game decision model is accepted, the next planning layer should be:

- season simulation architecture
- roster construction and transaction rules
- player fatigue, aging, and injury carryover across games
