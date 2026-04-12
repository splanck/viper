# Broadcast And Watch Mode

## Goal

Define how the simulation is presented as a watchable baseball broadcast while preserving the rule that the simulation owns the truth.

The user’s original target included "watch the game as it plays out like on TV, but you can't play the game yourself." The single-game and season plans intentionally deferred that. This plan defines the architecture that turns a deterministic baseball simulation into a TV-style watch mode.

## Core principle

Presentation consumes simulation events. It never invents baseball truth.

That means:

- the game engine determines the outcome
- the official result layer determines what happened
- the broadcast layer decides how to *show* it

If camera, pacing, overlay, or commentary systems influence the simulation outcome, the architecture is broken.

## Why this layer matters

Without a broadcast layer:

- the sim is readable but not watchable
- manager mode lacks theatrical payoff
- postseason and milestone moments have no presentation weight
- the product remains a debug console rather than a baseball experience

This plan is how the project turns backend correctness into a real spectator-facing mode.

## Relationship to prior plans

This plan depends on almost the entire stack:

- `plans/02-simulation-pipeline.md`
- `plans/09-in-game-manager-decision-model.md`
- `plans/10-season-simulation-architecture.md`
- `plans/14-manager-command-layer.md`
- `plans/15-save-format-and-persistence.md`
- `plans/20-history-and-awards.md`

It especially depends on:

- canonical `OfficialPlayResult`
- deterministic event logs
- manager decision logs
- archived history context

The currently implemented text output and summary writers are the earliest, simplest version of this layer.

## Broadcast session model

A watchable game should run through a dedicated `BroadcastSession`, not directly through the simulation loop.

Suggested layers:

- `ReplaySource` — feeds deterministic game events
- `BroadcastTimeline` — turns simulation events into presentable beats
- `ShotPlanner` — chooses camera / framing / focus changes
- `OverlayState` — scorebug, count, matchup, box inserts, standings inserts
- `CommentaryAdapter` — text or voice-like lines from structured events
- `PacingController` — live speed, skip rules, pause points, intervention points

The important boundary is this:

- simulation emits events and official results
- replay source hands those to the broadcast layer
- broadcast session decides how much time to spend on each beat

## Watch modes

The product should support more than one viewing mode.

### 1. Simcast mode

- mostly textual
- fast
- event feed with scorebug context
- ideal for full-season management play

### 2. Broadcast-lite mode

- pauses on each pitch or plate appearance
- includes overlay transitions and commentary inserts
- can be driven entirely from existing text and event digests at first

### 3. Full TV-style mode

- camera language
- batter / pitcher intro beats
- hit trajectory presentation
- between-inning recap beats
- contextual overlays

This is the long-term target, not the starting point.

## Event requirements

The broadcast layer needs richer presentation digests than a plain event log string list.

At minimum it should receive:

- inning / half / outs / count / base state
- batter and pitcher identity
- pitch result
- plate appearance result
- batted-ball classification
- runner movement summary
- substitutions and pitching changes
- official scoring decisions
- updated score and line score

For richer presentation later, also preserve:

- batted-ball direction and contact quality
- fielding position involved
- replay-worthy leverage markers
- milestone hooks
- history context hooks

## Shot-planning philosophy

The watch mode should not try to simulate television realism through full physical cinematography from day one.

Start with:

- stable canonical views per event type
- simple transitions between pre-pitch, contact, result, and reset
- deterministic "camera scripts" keyed off event type

Examples:

- pre-pitch framing
- contact cut for batted balls
- quick result focus for strikeouts and walks
- scoreboard recap between innings

This is enough to feel intentional without requiring an immediate full 3D broadcast engine.

## Commentary and text generation

Commentary should be driven by structured templates first.

Suggested categories:

- pitch commentary
- plate appearance summary
- inning recap
- substitution note
- milestone callout
- standings or season-context insert

Template-driven commentary is preferable for the first pass because it is:

- deterministic
- debuggable
- replay-safe
- style-consistent

More dynamic commentary can come later, but it should still be grounded in structured facts from the history and game layers.

## Pacing control

A baseball watch mode lives or dies on pacing.

The user should control:

- full speed / compressed pace
- pause after each pitch, plate appearance, or half inning
- auto-skip low-leverage moments
- always pause at manager checkpoints
- jump to scoring plays only

The pacing controller must operate on the broadcast timeline, not the simulation itself. The underlying game has already happened or is replaying deterministically.

## Manager intervention integration

Because the product is a manager sim, the watch mode must integrate with human management cleanly.

The broadcast session should:

- pause at manager checkpoints
- show the relevant context without leaking hidden state
- collect the manager action
- resume playback once the decision is logged

This is the product-facing bridge between `plans/17-human-manager-mode.md` and the broadcast presentation.

## Overlay system

The broadcast layer should support a composable overlay model.

Required overlays:

- scorebug
- count / outs / base runners
- batting order slot
- bullpen and substitution note
- line score inserts
- standings or playoff-race inserts
- player stat line insert

Later overlays may include:

- season milestone watch
- award-race callout
- playoff bracket insert
- matchup splits

## Replay and determinism

Broadcast playback must remain deterministic under `plans/15`.

The same saved game and decision log should produce:

- the same baseball results
- the same presentation beats
- the same commentary sequence

To guarantee this:

- shot planning must be deterministic from event data and seed
- commentary selection must be deterministic
- pacing settings may change runtime speed, but not the underlying presented event order

## Performance model

Watching many games is different from managing one game.

The architecture should support:

- fast-forward without dropping correctness
- season simulation with optional watch takeover
- replaying one game from a completed season save
- pausing without desyncing the event source

This implies that the broadcast layer should work equally well on:

- live in-progress games
- replayed completed games

## History integration

The broadcast layer becomes much stronger once it can ask the history layer for context:

- "defending champion"
- "seven-game winning streak"
- "closing in on 3,000 hits"
- "first postseason appearance since 2018"

This should be a structured query path, not hardcoded text attached to events.

## What to defer

For v1:

- full 3D camera simulation
- dynamic spoken commentary synthesis
- crowd simulation
- commercial-break presentation
- procedural on-screen graphics packages
- network-specific presentation skins
- advanced replay booth logic

## Failure modes to watch

### 1. Presentation mutating simulation truth

If the broadcast layer changes outcomes, pacing, or legality state, the architecture is broken.

### 2. Non-deterministic commentary or shot selection

If the same replay produces different presentation beats, save/load debugging becomes much harder.

### 3. Watch mode with no pacing controls

If the user cannot compress boring stretches or pause at key management moments, long seasons become unmanageable.

### 4. Overbuilt visual ambition before event fidelity

If the project chases cinematic presentation before the event feed and official result data are rich enough, the watch mode will be shallow.

### 5. Broadcast cues not aligned with manager prompts

If the game asks for a decision without the right visible context, the manager mode and watch mode will feel disconnected.

## Recommended next dependency

This is the last plan in the current `16`–`21` batch.

The natural next planning layer after this should be a production roadmap that sequences the 16–21 batch into implementation milestones: realism calibration first, then human management, then league/postseason expansion, then franchise depth, then history polish, then full broadcast presentation.
