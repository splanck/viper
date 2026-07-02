# ADR 0050: Game Stateful Result Objects

## Status

Accepted

## Context

Several game runtime APIs exposed operation output through mutable receiver
state:

- `Pathfinder.LastFound` and `LastSteps`
- `Quadtree.ResultCount`, `GetResult`, `GetPairs`, `PairFirst`, and `PairSecond`
- `AnimStateMachine.EventsFiredCount` and `EventFiredId`
- `AnimTimeline.EventsFiredCount` and `EventFiredId`

That style is compact for demos, but it is fragile in production code because a
later update, query, or pair collection can overwrite the result before another
system consumes it.

## Decision

Add composable snapshot objects while preserving every existing API:

- `Pathfinder.FindPathResult(...) -> Viper.Game.PathResult`
- `Pathfinder.FindNearestResult(...) -> Viper.Game.PathResult`
- `Quadtree.QueryRectResult(...) -> Viper.Game.QueryResult`
- `Quadtree.QueryPointResult(...) -> Viper.Game.QueryResult`
- `Quadtree.QueryPairs() -> Viper.Game.QuadtreePairResult`
- `AnimStateMachine.PollEvents() -> Viper.Game.AnimationEventBatch`
- `AnimTimeline.PollEvents() -> Viper.Game.AnimationEventBatch`

The result objects copy producer state at the time they are created. Existing
last-state and indexed APIs remain available for compatibility and diagnostics,
with API dump migration targets pointing to the snapshot APIs.

`PathResult.StepCount` is the canonical name for the cell-to-cell path step
count. `PathResult.Length` remains available as a compatibility alias; see
ADR 0062.

## Consequences

- Game code can store, pass, and inspect query/path/event results without
  relying on mutable last-state.
- Examples and docs should teach result objects first.
- Compatibility APIs remain source-compatible and useful for low-level probes.
- Runtime API metadata can classify the old rows as legacy and direct tooling to
  the new composable APIs.
