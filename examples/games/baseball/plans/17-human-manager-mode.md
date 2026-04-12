# Human Manager Mode

## Goal

Define how a human player actually manages a club through the existing backend decision interfaces.

`plans/14-manager-command-layer.md` defined the typed manager contract. This plan turns that contract into a real playable mode: a human player can set lineups, manage pitching changes, choose substitutions, and intervene in high-value tactical spots while the rest of the game continues through the same legality and replay infrastructure as the AI.

## Core principle

Human management must be powerful enough to feel like baseball management, but narrow enough to remain baseball management.

The human player should control:

- legal lineup and roster usage decisions
- bullpen and substitution choices
- a limited set of tactical baseball choices

The human player should *not* control:

- omniscient fielding choices
- individual fielder throw targets on every batted ball
- hidden scouting truths
- pre-rolled outcomes

This is the user-facing extension of `plans/09 § Human and AI parity` and `plans/14 § Human-manager contract`.

## Why this layer matters

Without a real human-manager mode:

- the project remains an AI-vs-AI simulator
- the typed manager command layer is only half-used
- the save and replay systems are not tested under real player choices
- the "you are the baseball manager" fantasy is still theoretical

This plan defines the actual interaction model that makes the backend game playable.

## Relationship to prior plans

This plan depends directly on:

- `plans/11-roster-and-transaction-rules.md`
- `plans/12-fatigue-injuries-and-availability.md`
- `plans/14-manager-command-layer.md`
- `plans/15-save-format-and-persistence.md`

It also builds on the implemented season and manager foundations:

- `src/season/manager_layer.zia`
- `src/sim/manager_policy.zia`
- `src/season/persistence.zia`

Those currently support AI-driven daily lineups and in-game decisions. Human mode layers on top of the same structures.

## Human role profiles

The product should support more than one human control boundary.

### 1. Manager-only

The human controls:

- daily lineup
- starting pitcher
- in-game substitutions
- bullpen usage
- tactical calls

The AI controls:

- roster construction
- callups and demotions
- contracts and scouting
- trades

This is the default experience if the fantasy is "I am the field manager."

### 2. Manager plus roster control

The human controls everything in manager-only mode plus:

- IL replacements
- callups and demotions
- bullpen composition
- bench composition

This is a bridge mode between strict manager play and full franchise mode.

### 3. Full franchise control

The human controls manager and front-office responsibilities. This mode depends on `plans/19-franchise-mode.md`, but the command architecture should already leave space for it.

## Session structure

Human mode should be organized around clear, low-friction checkpoints.

### Franchise shell

Outside games, the user interacts through a text shell that can:

- show today’s schedule
- show standings
- inspect roster and availability
- inspect recent injuries and transactions
- choose "manage game", "sim day", or "delegate"

### Pregame checkpoint

Before a scheduled game, the human can:

- pick the starting pitcher
- set the batting order
- assign defensive positions
- choose bench and bullpen usage preferences
- set game-level aggression presets

This is the most important checkpoint and should always be available in human mode.

### In-game checkpoints

Do not prompt the human at every micro-event by default. That destroys pacing.

The default in-game checkpoints should be:

- before first pitch of each half inning if a lineup or defensive issue exists
- when the engine recommends a pitching change
- when a pinch-hit / pinch-run / defensive substitution opportunity is material
- before an intentional walk choice
- optional pre-pitch tactical checkpoints for bunt / steal only when the chosen human mode enables them

### Postgame checkpoint

After the game, show:

- final score
- line score
- box summary
- key manager decisions
- injuries
- next-day availability impact

Then return to the franchise shell.

## Prompt pacing profiles

The user should be able to choose how interactive the experience is.

Suggested profiles:

- **`observe_only`** — human sets pregame lineup, AI handles the game
- **`standard_manager`** — human gets high-value prompts only
- **`high_touch_manager`** — human also gets optional steal / bunt prompts
- **`commissioner_debug`** — all checkpoints exposed for debugging, not the default product experience

The key rule: *prompt density is part of game design*. Too many prompts turns the sim into a spreadsheet interrupt generator.

## Command model

Human input should map cleanly onto typed `ManagerAction` values from `plans/14`.

Suggested command forms:

- numbered menu choices for common actions
- short command aliases for advanced users
- explicit confirmation only for destructive or hard-to-reverse choices

Examples:

- `1` -> keep current pitcher
- `2` -> bring in listed reliever
- `ph 7 bench_2` -> pinch hit in slot 7 with player `bench_2`
- `iw` -> issue intentional walk
- `auto` -> delegate this checkpoint to AI

The engine should parse input into one of the legal `ManagerAction` variants and then pass it through the standard legality choke.

## Information boundary

Human mode must obey the same information boundary as AI mode.

Show:

- score, inning, outs, count, base state
- current batter and pitcher
- available bench and bullpen options
- visible scouting grades
- availability and fatigue notes
- recent pitch count / recent appearances

Hide:

- exact hidden ratings
- future random outcomes
- precomputed win probability if the product design does not want it
- engine-internal tactical recommendations unless advisor mode is explicitly enabled

## Delegation and autopilot

The human should not be forced to answer every possible prompt.

The command layer should support delegation at three scopes:

- **single checkpoint** — "AI handle this one"
- **game remainder** — "AI manage the rest of this game"
- **role slice** — "I handle lineups, AI handles in-game tactics"

This is crucial for long seasons. Even users who want to manage most games will not want identical low-leverage prompts 162 times.

## Save, replay, and undo boundaries

Human mode must remain replay-safe under `plans/15`.

### Decision logging

Every accepted human decision is recorded exactly like an AI decision:

- checkpoint id
- visible context summary
- legal action set
- chosen action
- time and source (`human`)

### Replay

Replaying a human-managed game should consume the decision log, not ask the user again.

### Undo policy

Do not promise arbitrary undo.

For v1, safe undo boundaries are:

- before first pitch of a game
- at the franchise shell before advancing the day

Once a live game checkpoint is confirmed and logged, it is authoritative. Allowing arbitrary rewinds complicates replay integrity too early.

## Text presentation requirements

Human mode is text-first. That means prompt formatting matters.

Each prompt should be:

- compact
- consistent
- scannable
- explicit about legal choices

For example, a pitching change prompt should show:

- inning / score / outs / runners
- current pitcher line
- fatigue / readiness
- top 3 to 6 bullpen options
- a legal action menu

It should *not* dump the entire roster every time.

## Accessibility and time cost

A 162-game baseball season is long. Human mode must respect that.

The planning target should be:

- a full game can be managed in a few minutes in `standard_manager` mode
- a full season is practical without requiring every game to be manually driven
- sim-day plus selective intervention is a supported play pattern

This means prompt quantity is a first-class product metric, not an afterthought.

## Interaction with future franchise mode

Human manager mode and franchise mode are related but not identical.

This plan should not assume that every user wants to negotiate contracts and make trades. Therefore:

- the human role boundary must be configurable
- front-office prompts should remain optional unless the chosen role profile requires them
- the same franchise save should support AI-front-office / human-manager play cleanly

## What to defer

For v1:

- natural-language freeform parsing
- multiplayer hot-seat management
- voice input
- animated prompt UIs
- advisor overlays with probabilistic recommendation text
- arbitrary mid-game rewind
- per-pitch interactive management as the default mode

## Failure modes to watch

### 1. Prompt fatigue

If the game stops every few pitches, the user will stop managing manually. Prompt density must be tuned.

### 2. Human cheat paths

If human mode can bypass legality while AI cannot, the entire contract is compromised.

### 3. Information leakage

If human prompts reveal hidden truth rather than visible scouting, later scouting systems become impossible to integrate cleanly.

### 4. Replay mismatch

If a human decision is not logged exactly, save/load replay diverges immediately.

### 5. Manager role creep into omniscient tactics

If the user starts choosing live-ball throw targets or omniscient send/hold calls, the mode has drifted away from baseball management.

### 6. No practical delegation

If every season requires 162 fully-managed games to feel meaningful, most users will burn out. Delegation and mixed-control play are required.

## Recommended next dependency

Once human manager mode is accepted, the next planning layer is `18-league-structure-and-postseason.md`.

Human management becomes much more meaningful once the simulation supports the actual competitive frame the user expects: divisions, full standings structure, and a real postseason path.
