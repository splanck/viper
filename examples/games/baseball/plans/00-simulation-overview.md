# Baseball Simulation Overview

## Goal

Build the first backend slice of a baseball manager simulation that can progress cleanly through:

- One plate appearance
- One half inning
- One inning
- One game

The first deliverable should be headless and text-driven. It should produce deterministic, debuggable play-by-play output from a fixed seed.

## What "realistic" means in this phase

For the first phase, realism means:

- The common rules and scoring cases that occur in ordinary MLB games are modeled
  correctly by the time the first full-game milestone is declared complete
- Outcomes feel baseball-like at the plate appearance level
- State transitions are trustworthy
- Legal lineup and substitution state is preserved throughout the game
- The engine can later be calibrated at season scale

It does not yet mean:

- Full franchise depth
- TV presentation
- Rich manager AI
- Perfect league-wide statistical calibration
- Continuous ball-flight (rigid-body) physics

## Core design choice

The first engine should not be a free-flight physics sandbox.

It should be a layered simulation:

1. Pitch and plate appearance logic
2. Contact and batted-ball generation
3. Fielding and baserunning resolution
4. Rule enforcement and state updates
5. Event emission for text output

This keeps the game realistic enough to grow while avoiding the trap of building a 3D baseball toy before the baseball logic is sound.

## Recommended abstraction level

For this stage, use pseudo-physical baseball rather than literal ball physics.

Each batted ball should eventually be represented by values like:

- Exit velocity
- Launch angle
- Spray angle
- Hang time
- Landing zone or landing point

But those values should exist to support baseball outcomes, not to drive visual animation yet.

## Top-level architecture

The backend should be organized around six layers.

### 1. Domain Model

Pure data and rules configuration:

- Players
- Teams
- Lineup cards
- Lineups
- Ballparks
- Rulesets
- Game state
- Official play-result types

### 2. Decision Interface

The simulation should consult a manager-policy interface at safe checkpoints:

- Before a plate appearance
- Before a pitch when strategy matters
- Before substitutions
- Between innings or during pitching changes

The first implementation can be a static default policy. The interface still needs
to exist from the start so the engine does not hardcode "no decisions" assumptions.

### 3. Simulation Engine

State transition logic:

- Pitch resolution
- Plate appearance resolution
- Batted-ball resolution
- Runner advancement
- Inning and game progression

### 4. Official Result Layer

Every plate appearance and every completed play should produce one canonical
adjudicated result object. That object should be the source of truth for:

- Outs added
- Batter result
- Runner advancement
- RBI and run scoring
- Error and fielder's-choice handling
- Earned-run impact
- Box-score updates

### 5. Event Stream

Every important state transition should emit structured events:

- Pitch events
- Plate appearance result events
- Runner movement events
- Substitution events
- Inning start/end events
- Game start/end events

The event stream should derive from the simulation and official result layer.
It should not invent baseball logic on behalf of either text output or stats.

### 6. Output Adapters

Consumers of the event stream:

- Text play-by-play
- Box score generator
- Debug trace
- Later: broadcast presentation

## Non-negotiable engine properties

The simulation core should be:

- Deterministic from a seed
- Reproducible
- Testable without graphics
- Decoupled from rendering
- Easy to inspect during a failed play sequence
- Built around one canonical official result per completed play

## What to defer

The initial slice should explicitly defer:

- Franchise economy
- Scouting
- Drafts
- Minor leagues
- Detailed personality and morale
- Rich injury ecosystems
- Complex manager AI
- 2D or 3D broadcast scenes

Those features are only valuable if the game engine is already trustworthy.

## First playable target

The first solid milestone should be:

- Two teams with fixed lineups
- Legal lineup-card state, even if substitutions are scripted or disabled
- One ballpark
- One ruleset
- A deterministic game simulation
- Full text play-by-play
- A final line score and simple box score

That is the right point to start evaluating whether the engine feels like baseball.
