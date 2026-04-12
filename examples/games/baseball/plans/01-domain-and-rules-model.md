# Domain And Rules Model

## Guiding principle

Model underlying baseball truth, not presentation shortcuts.

The backend should treat a game as a sequence of state transitions driven by:

- Player talent
- Current game state
- Rules
- Random variation

## Core entities

### Player

The player model should separate true talent from current game state.

Suggested first-cut fields:

- Identity
  - `id`
  - `name`
  - `bats`
  - `throws`
  - `date_of_birth`
  - primary and secondary positions

- Hitting talent
  - zone judgment
  - chase resistance
  - attack aggression
  - contact
  - whiff avoidance
  - raw power
  - game power
  - hard contact
  - ground ball tendency
  - line drive tendency
  - fly ball tendency
  - pull tendency
  - center tendency
  - opposite-field tendency

- Pitching talent
  - starter or reliever profile
  - stamina
  - stuff
  - control
  - command
  - movement
  - ground ball tendency
  - pitch mix
  - hold runners
  - pop-up induction

- Defense
  - range by position
  - reaction
  - hands
  - arm strength
  - arm accuracy
  - catcher framing
  - catcher blocking
  - catcher pop time
  - double-play skill
  - outfield route efficiency later

- Running
  - speed
  - first step
  - baserunning instincts
  - steal jump
  - steal skill

- Durability
  - fatigue resistance
  - injury proneness

Use the ratings names and semantics from
`04-player-ratings-schema.md` as the authoritative schema definition.
This section exists only to anchor the domain model around the same talent families.
Age should be derived from `date_of_birth` and the current season date, not manually
incremented as mutable player state.

For this first stage, do not use too many opaque "magic" ratings. Favor ratings that clearly map to baseball outcomes.

### Team

Suggested first-cut fields:

- team identity
- full roster
- default lineup templates
- default defensive templates
- bench pool
- bullpen pool

The static team object should describe who belongs to the club.
It should not be the in-game authority on whether a substitution is legal.

### LineupCard

The lineup card should be the authoritative in-game personnel object.

For a manager sim, this is more important than the static `Team` object.

Suggested first-cut fields:

- batting slots in order
- current defensive assignments by position
- active pitcher
- DH slot state (when the DH rule is active)
- bench availability
- bullpen availability
- player entered-game flags
- player exited-game flags
- substitution ledger
- legality validator hooks

The lineup card should answer questions like:

- Who is batting in slot 6 right now
- Who is playing left field right now
- Can this player re-enter
- Did losing the DH already occur
- Is this pitcher still the batting-slot owner in a no-DH game

### Ballpark

The field should be modeled as a real gameplay factor, even in text mode.

Suggested first-cut fields:

- left field distance
- left-center distance
- center field distance
- right-center distance
- right field distance
- wall heights
- foul territory profile
- surface type
- infield speed
- altitude
- weather and wind hooks

Stage 1 can simplify foul territory and surface into modifiers. It does not need a full geometric mesh.

### Ruleset

The ruleset must be explicit and configurable.

Suggested first-cut fields:

- innings per game
- designated hitter enabled
- extra innings automatic runner enabled or disabled
- three-batter minimum enabled or disabled
- mound visit and roster rules hooks
- substitution rules
- walk-off rules
- scoring rules for sacrifice flies, errors, and earned runs

Use a configurable rules object from the start so the engine is not hardcoded to one era.

### ManagerPolicy

The simulation should expose a decision-provider interface from the first phase.

It does not need a smart AI or user interaction yet, but it should already define:

- when a decision can be requested
- what context is visible to the decision provider
- what decisions can be returned

Suggested early decision surfaces:

- pitch-around or attack intent
- intentional walk
- steal attempt enable or disable
- bunt attempt enable or disable
- defensive positioning flags
- pitcher change request
- pinch hitter and pinch runner request

The first implementation can be a deterministic default policy.
The point is to avoid hardcoding "no manager decisions exist" into the engine core.

### BaseOccupant

The live base state should not store only a runner id.

Each occupied base should carry responsibility data that survives substitutions.

Suggested first-cut fields:

- `runner_id`
- `responsible_pitcher_id`
- `reached_on_result`
  - walk
  - intentional_walk
  - hit_by_pitch
  - single
  - double
  - triple
  - error
  - fielders_choice
  - uncaught_third_strike

- `reached_on_outs_context`
- `responsibility_notes`

The key rule is simple:

- a pitching change updates the active pitcher
- it does not rewrite the `responsible_pitcher_id` on existing base occupants

That is what makes inherited runners and charged runs tractable later.

### GameState

This should be the central mutable state object.

Suggested first-cut fields:

- inning number
- top or bottom half
- outs
- count
  - balls
  - strikes

- base occupancy
  - runner entry on first (`BaseOccupant` or null)
  - runner entry on second (`BaseOccupant` or null)
  - runner entry on third (`BaseOccupant` or null)

- score
  - home
  - away

- batting order pointers
  - current away lineup index
  - current home lineup index

- active home lineup card
- active away lineup card
- active pitcher and batter
- pitcher fatigue snapshot
- defensive alignment
- per-inning line score ledger
- game termination state
- event log reference
- RNG seed or RNG handle

### PlayResolutionState

Between pitches and between completed plays, `GameState` can stay compact.
During play resolution, that is not enough.

Use a transient play-resolution object for in-flight baseball logic.

Suggested first-cut fields:

- batted-ball snapshot
- current basepath positions of all involved runners
- force map by runner
- tag-up eligibility
- pending advance or hold decisions
- pending outs and where they occurred
- live ball or dead ball state
- fielder possession chain

This object should exist only while resolving a pitch outcome or ball in play.
It prevents the core game state from turning into a pile of special-case flags.

### OfficialPlayResult

Every completed play should produce one canonical adjudicated result object.

Suggested first-cut fields:

- batter id
- pitcher id for the completed plate appearance
- plate appearance end reason
  - strikeout
  - walk
  - intentional walk
  - hit by pitch
  - uncaught third strike
  - ball in play
  - other rule result

- batter result code
  - out
  - single
  - double
  - triple
  - home run
  - reached on error
  - fielder's choice

- strikeout type
  - swinging
  - looking
  - uncaught_third_strike

- walk type
  - standard
  - intentional

- sacrifice type
  - none
  - fly
  - bunt

- batter reached on uncaught third strike flag
- outs added
- base state before and after, including responsible pitcher ids
- runner advancement list, including responsible pitcher per runner
- runs scored
- run charge list by pitcher id
- RBI awarded
- errors charged
- fielder's choice and sacrifice flags
- earned-run accounting notes
- scoring notes for text or debugging

This object should be the source of truth for:

- score updates
- stat updates
- text narration
- replay and debugging
- later broadcast presentation

### Box Score State

Track statistics separately from the live state.

Suggested first-cut tracked stats:

- batting
  - PA, AB, R, H, RBI, BB, IBB, SO, HBP, SF, SH, GIDP, 2B, 3B, HR

- pitching
  - BF, IP, H, R, ER, BB, IBB, SO, HBP, HR, pitches, strikes
  - inherited runners
  - inherited runners scored

- fielding
  - errors
  - double plays turned

Keep the scoring ledger explicit so later season aggregation is straightforward.

## Official scoring responsibility

The scoring layer needs explicit ownership rules before substitutions and bullpen usage become real.

Required rules:

- every occupied base stores the `responsible_pitcher_id`
- every scoring runner is charged back to that pitcher unless an explicit scoring rule overrides it
- intentional walks remain distinct from ordinary walks in official stats
- uncaught third strikes still credit a strikeout even if the batter reaches
- the scorer layer derives hits, errors, fielder's choices, sacrifice credit, and run charges from `OfficialPlayResult`, not from text narration

Can defer to later:

- win, loss, save, hold, and blown-save rules
- full official-scorer nuance on edge-case sacrifice and hit-error judgments

## Field model

For this first stage, the field should be represented in one of two ways.

### Preferred approach

Use a light coordinate model:

- Infield and outfield sectors
- Approximate landing coordinates for balls in play
- Fielder starting positions
- Simple route and arm resolution

### Acceptable fallback

Use named zones:

- 1B line
- 2B hole
- SS hole
- 3B line
- shallow LF
- deep LF
- left-center gap
- shallow CF
- deep CF
- right-center gap
- shallow RF
- deep RF
- foul pop zones

The coordinate model is better long-term, but the zone model is acceptable if it keeps the first game loop simple and correct.

## Physics model for stage 1

Do not attempt true rigid-body baseball physics yet.

Instead, represent "physics" as baseball outcome variables:

- pitch quality
- pitch location
- swing quality
- contact quality
- exit velocity
- launch angle
- spray angle
- hang time

This is enough to support:

- strikeouts
- walks
- weak contact
- hard contact
- home runs
- singles, doubles, and triples
- playable fly balls
- grounders that can become outs or hits

That abstraction is the right first step for a manager sim.

## Rules coverage for version 1

Must-have rules before the first full-game engine is called rules-correct:

- balls and strikes
- strikeout
- walk
- hit by pitch
- foul balls
- ball in play
- force outs
- tag-up basics
- sacrifice fly
- double play
- infield fly
- dropped third strike
- home run
- hit vs error vs fielder's-choice distinction
- earned-run tracking basics
- inning end after three outs
- game end rules including walk-off

Can defer to later:

- balks
- catcher interference
- pickoffs
- wild pitch and passed ball nuance
- replay and challenge rules

## Rule adjudication ownership for tricky v1 cases

Two common rules need explicit ownership so they do not become scattered special cases.

### Uncaught third strike

The plate-appearance engine owns the trigger.

It should decide:

- whether strike three occurred
- whether the catcher completed the catch cleanly
- whether the batter is automatically out because first base is occupied with fewer than two outs
- whether a live batter-runner should be created instead

If the batter-runner is live, the runner-advancement layer handles the rest.
The official result must still record a strikeout.

### Infield fly

The batted-ball and fielding layer owns the trigger.

It should decide:

- whether the ball is a fair fly-ball type eligible for the rule
- whether the force situation qualifies
- whether an infielder could make the play with ordinary effort

If the rule is triggered, the official result builder marks the batter out and the
runner-advancement layer applies the no-force consequences.

## Manager decision hooks

Even before manager AI exists, the simulation should leave hooks for:

- intentional walk
- steal attempt
- bunt attempt
- pinch hitter
- pinch runner
- defensive substitution
- pitcher change
- infield in
- corner infield positioning

Those hooks should be exposed through the `ManagerPolicy` interface from the start,
even if the first pass uses scripted defaults.
