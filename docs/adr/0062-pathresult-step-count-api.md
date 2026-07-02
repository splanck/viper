# ADR 0062: PathResult StepCount API

## Status

Accepted

## Context

`Viper.Game.PathResult.Length` was added as part of the game snapshot result
work to expose the number of cell-to-cell steps in a path. The value is useful,
but the name is ambiguous beside `PathResult.Path.Length`, weighted path `Cost`,
and collection/string `Length` conventions.

## Decision

Add `Viper.Game.PathResult.StepCount` as the canonical property for the
cell-to-cell step count. Keep `PathResult.Length` registered as a compatibility
alias, with runtime API metadata pointing callers to `PathResult.StepCount`.

## Consequences

- New pathfinding code can read `StepCount` without confusing it for waypoint
  list length or weighted path cost.
- Existing `Length` users remain source-compatible.
- Runtime docs, examples, and API metadata should teach `StepCount` first and
  describe `Length` as a legacy alias.
