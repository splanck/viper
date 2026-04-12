# Manager Command Layer

## Goal

Turn the in-game decision model from `plans/09 § Recommended decision checkpoints` into a real backend contract that supports both AI policies and human-driven manager input through the same interface.

The implemented Phase 6 `DefaultManagerPolicy` (`src/sim/manager_policy.zia`) was an early sketch — a single class with `shouldIntentionalWalk`, `choosePitchIntent`, and `shouldChangePitcher` methods. This plan formalizes that into a typed, checkpoint-driven contract that an AI policy, a scripted policy, or a human-driven CLI all satisfy in the same way.

## Core principle

AI and human managers make the same kind of decisions through the same typed interface. The engine never knows which one it's talking to.

This is the formalized version of the rule from `plans/09 § Human and AI parity`:

> If the game later supports human management, human input should flow through the same legality and action pipeline as AI policy decisions. That avoids an entire class of "human mode can do things AI mode cannot" bugs. It also prevents human mode from receiving non-manager omniscient live-ball prompts.

The contract is what makes this rule actually enforceable: if there is one interface and three implementations (default AI, scripted AI, human prompt), then "human can do something AI can't" becomes a type error.

## Why this layer matters

Without a formal command layer:

- the engine hardcodes a single policy (the current `DefaultManagerPolicy`)
- swapping in a different AI requires touching every call site
- there is no path to a human-driven mode without a rewrite
- replay determinism breaks the moment any decision can vary
- the legality layer from `plans/09` is enforced ad-hoc instead of at one chokepoint

The implemented code already has the *shape* of this layer — `manager_policy.zia` has a `DefaultManagerPolicy` class with decision methods. The work this plan describes is mostly about hardening that shape: typed contexts, typed actions, a single legality choke, and a contract that supports more than one implementation.

## Decision context types

Each checkpoint from `plans/09 § Recommended decision checkpoints` gets a dedicated context type. The context is the *only* thing the policy reads — it cannot reach into `GameState` directly, which keeps the information boundary from `plans/09 § Information boundary` honest.

### Context types

- **`PregameLineupContext`** — passed at lineup card submission. Carries available active roster (per `plans/11`), opponent starter handedness, ballpark, ruleset, recent results.
- **`BetweenPAContext`** — passed before each plate appearance. Carries inning, score, outs, base state, current batter, current pitcher, on-deck and in-hole batters, available bench, available bullpen.
- **`PrePitchContext`** — passed before each pitch when tactical decisions matter (rare; placeholder for `DeclareSteal`, `DeclareBunt`).
- **`PitchingChangeContext`** — passed when the engine asks "should we make a pitching change here?". Carries current pitcher's stint stats (pitches, runs allowed), available bullpen, leverage estimate.
- **`BetweenInningsContext`** — passed at the end of each half-inning. Carries the change in score, the upcoming half-inning's batters, defensive substitution opportunity.

Each context is a read-only struct or class containing exactly the information the manager is *allowed* to know per `plans/09 § Information boundary`. It explicitly does NOT carry:

- future random outcomes
- hidden true ratings of opposing players
- the engine's pre-rolled outcome of the next plate appearance

## Manager-visible player view

Decision contexts should expose manager-visible player cards, not raw `Player`
objects from the simulation core.

Suggested `ManagerVisiblePlayerCard` contents:

- identity and handedness
- listed positions and role tags
- manager-visible scouting grades or coarse ratings
- current availability snapshot from `plans/12`
- recent workload summary

For v1, with scouting depth deferred, the visible grades can be deterministic coarse
transformations of true ratings. The key design rule is still important: the manager
contract should depend on the visible view, not on hidden exact talent values, so
future scouting systems do not require a rewrite.

### Context construction

The engine builds a fresh context at every checkpoint. Contexts are immutable. The policy receives the context, returns an action, and never sees the context again.

## Action types

Manager decisions return typed `ManagerAction` values, not strings or booleans:

- `NoAction` — proceed with engine defaults
- `IssueIntentionalWalk` — for `BetweenPAContext`
- `MakeSubstitution(outPlayer, inPlayer, slot)` — for `BetweenPAContext` or `BetweenInningsContext`
- `ChangePitcher(newPitcher)` — for `PitchingChangeContext` or `BetweenPAContext`
- `DeclareBunt` — for `PrePitchContext`
- `DeclareSteal(runnerBase)` — for `PrePitchContext`
- `SetRunningAggressionProfile(profile)` — for `PregameLineupContext`
- `SubmitLineupCard(card)` — for `PregameLineupContext`

The implemented `DefaultManagerPolicy` returns `Boolean` and `String` instead of typed actions. The migration to typed actions is part of this plan's implementation work, not an existing-code modification right now.

### Why typed not strings

Typed actions:

- catch invalid actions at compile time (can't return `"FlyToTheMoon"`)
- make legality checks total (every action variant has a legality rule)
- make replay logs structured (the log records `ChangePitcher` directly, not a parsed string)
- match `plans/09 § Typed decision model` exactly

## Legality layer

Every manager action passes through a single legality choke before mutating game or season state. The choke runs the three-tier check from `plans/11 § Legality checks`:

1. **Compartment legality** — substitutions only from active roster, no IL or minors
2. **Single-game legality** — no double-occupied positions, valid DH state
3. **Availability legality** — every involved player has `availableForGame == true` per `plans/12 § Availability snapshot`

If any tier fails, the action is rejected and the policy is asked to choose again (or the engine falls back to `NoAction`).

The legality choke should return `Result.OkI64(0)` on success or
`Result.ErrStr("...")` with a specific reason on failure. The current
`LineupCard.validateStart()` implementation still returns `Boolean`; migrating that
path to `Result` is part of this plan's implementation work.

## Default AI policy

The Stage-1 default AI policy from `plans/09 § Stage 1: Static default policy` is the baseline implementation:

- never bunt
- never steal
- no pinch hitting
- no pinch running
- no intentional walks
- leave lineup fixed
- use a single starter unless a scripted pitching-change rule triggers
- pitching change rule: pull the starter at >105 pitches, in the 7th+ if they're tiring, or after a 4+ run inning

The implemented `DefaultManagerPolicy` already does this. The contract specification here is mostly documenting what the existing class does and the migration path to the typed `ManagerPolicy` interface.

A `Stage 2` policy with conservative bullpen management (per `plans/09 § Stage 2`) is the next step beyond the default; it does not need its own plan, just an implementation guided by this contract.

## Human-manager contract

The human-manager mode lets a real person make decisions at every checkpoint. For v1, this is text-only (no GUI):

- the engine reaches a checkpoint and calls the policy
- if the policy is a `HumanManagerPolicy`, it prints the context summary and a numbered menu of legal actions
- the engine blocks (synchronously) waiting for input
- the user enters a number; that becomes the typed action
- the action is validated through the legality choke; if rejected, the user is reprompted

### Synchronous blocking

For v1, the human manager mode runs synchronously in the simulation thread. The engine pauses entirely while waiting for input. This is acceptable because the v1 demo is text-only and single-player; async / multiplayer is deferred.

### Default for unanswered prompts

If the user enters nothing or aborts, the engine falls back to `NoAction`. This guarantees the simulation can always make forward progress.

### Display formatting

Human-mode prompts must show enough context for an informed decision but no more. The
display rule mirrors `plans/09 § Information boundary`:

- show: count, outs, base state, score, batter and pitcher names with manager-visible scouting grades
- show: bench / bullpen options with their manager-visible scouting grades and availability snapshots
- show: pitcher's current pitch count and recent appearances
- hide: opposing manager's intent
- hide: pre-rolled outcomes
- hide: hidden true talent ratings of opposing players

## Replay determinism with manager input

A saved game must replay byte-identically whether or not a human is in the loop. This is harder than it looks because human input is non-deterministic.

The solution is a **manager decision log**: every action chosen at every checkpoint is recorded along with the checkpoint id. On replay, the engine consults the log instead of calling the policy. If the log says "at checkpoint #42, the action was `ChangePitcher(Smith)`", that action is replayed exactly.

This log:

- is part of the season-level persistent state from `plans/15`
- captures both AI and human decisions identically
- makes the AI policy implementation entirely cacheable
- means replaying a saved game is the same operation regardless of whether the original game was AI or human

## Logging and traceability

Every manager decision is logged with:

- checkpoint type (`PregameLineupContext`, `BetweenPAContext`, etc.)
- a context summary (inning, score, outs, batter, pitcher, base state)
- the legal options the policy was offered (a list of action variants that passed legality)
- the action chosen
- the policy's rationale code or string (e.g., `"pitch_count_threshold"`, `"manual"`)
- the source policy class (e.g., `"DefaultManagerPolicy"`, `"HumanManagerPolicy"`)

This log feeds:

- replay determinism (the replay log above)
- post-game analysis (which decisions did the manager make?)
- AI policy debugging (why did the AI choose this?)
- human-vs-AI comparison runs

## Per-team policy

The two teams in a game have separate policies. The home team and the away team can each be AI, scripted, or human. The four combinations are:

- AI vs AI (default; what the current `DefaultManagerPolicy` produces)
- AI vs Human (the v1 demo target)
- Human vs AI (same, with sides swapped)
- Human vs Human (deferred — needs UI alternation)

Per-team policy is set when the game's `GameState` is constructed. The engine consults the right policy at each checkpoint based on which team is acting.

## What to defer

For v1:

- real interactive UI (text prompts only)
- scouting depth modeling (the manager always sees manager-visible scouting grades)
- manager personality traits (aggressive vs conservative as a profile, separate from individual decisions)
- multi-player human mode (only one human at a time)
- manager AI tuning UI
- recording and replaying human decisions across saves (the log captures them; replay is deferred polish)
- "advisor mode" (AI suggestions overlaid on human decisions)
- async / event-driven manager input
- bullpen warmup decisions (more granular than just `ChangePitcher`)

## Failure modes to watch

### 1. AI/human contract divergence

If the human prompt offers options the AI can't return, or vice versa, the contract has diverged. Test: enumerate the actions the human menu can produce; verify each is a legal `ManagerAction` variant.

### 2. Decision context cheats with hidden info

If a context accidentally includes hidden state (e.g., the pre-rolled outcome of the next pitch), the AI can game it and the human menu reveals secrets. Test: every context type's fields must be reviewable against `plans/09 § Information boundary`.

### 3. Replay non-determinism after human play

If the engine doesn't log a human decision and tries to replay, it falls back to AI and produces a different game. Test: play a human game, save, replay from save, verify identical output.

### 4. Legality bypass

If any action ever mutates state without going through the legality choke, an illegal lineup state can leak in. Test: search code for any action handler that modifies game/season state without first calling the legality function.

### 5. Synchronous blocking hangs the engine

If the human prompt blocks indefinitely with no timeout / no abort, the engine hangs. Test: launch with no input, abort cleanly after a timeout.

### 6. Per-team policy crossover

If the engine accidentally consults the home policy on an away-team checkpoint, the wrong manager makes the call. Test: log the source-policy class on every decision, verify it matches the acting team.

### 7. The `DefaultManagerPolicy` diverges from the contract during refactor

The implemented class predates this plan. The migration to the typed interface must keep behavior identical (the calibration probe should pass before and after). Otherwise the migration silently changes outcome rates.

## Recommended next dependency

Once the manager command layer is accepted, the next planning layer is `15-save-format-and-persistence.md`. Replay determinism, the manager decision log, and per-team policy state all need to survive save/load cleanly — that's exactly what plan 15 owns.
