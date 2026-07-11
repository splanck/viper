# ADR 0099: Viper.Game.Quests — Objective Tracker with SaveData Integration

Date: 2026-07-11

## Status

Accepted

## Context

Quest state is the backbone every adventure game rebuilds: quests → stages
→ objectives, activation, progress toasts, persistence. Nothing existed in
the runtime; `AchievementTracker` provides the structural template
(process-owned registry, polled notifications, bounded shapes).

## Decision

- **Model** (`src/runtime/game/rt_quests.c`, 2D/3D-agnostic pure state
  machine — no clock, no randomness): quests (≤32) → stages (≤16) →
  objectives (≤8, Flag or Counter with target). Registration is fluent and
  **idempotent on string ids** (`[A-Za-z0-9._-]`, ≤64 — validated so save
  serialization needs no escaping; budgets trap loud at registration).
- **Lifecycle:** `Activate` (Hidden→Active), `Fail`; `SetFlag`/`Progress`
  act on the **active stage** only; stages auto-advance when all their
  objectives complete; the final stage completes the quest. Mutations on
  Hidden/Complete/Failed quests are safe no-op `false` — HUD and gameplay
  code polls freely, unknown ids never trap.
- **Events:** a 64-entry ordered buffer of `{questId, kind}` (Activated /
  ObjectiveProgress / ObjectiveComplete / StageComplete / QuestComplete /
  QuestFailed) polled via `EventCount/EventQuestId/EventKind` and cleared
  explicitly (`ClearEvents`); `JustCompleted(id)` is the read-reset
  one-shot. `QuestState` / `QuestEventKind` static constants classes mirror
  the enum-accessor pattern (`Game3D.Layers`).
- **Persistence:** `Save(saveData)`/`Load(saveData)` store one versioned
  value under `"viper.quests.v1"` — a compact `q=id;s=state;g=stage;
  o=stageId.objId:progress` record string (ids need no escaping by the
  registration contract; SaveData's JSON layer handles the rest). Titles /
  texts / targets are **not** saved: games re-register at boot, then `Load`
  applies id-matched state; unknown saved ids are tolerated and `Load`
  before any registration returns false.
- Native link: `rt_quests_` added to the Game component prefix table in
  `RuntimeComponents.hpp` (the archive classifier is prefix-driven — a new
  `rt_*` family always needs an entry or native links fail).

## Consequences

- Deferred (recorded): `autoBind` dirty-tracking sugar (explicit
  `Save`-before-`SaveData.Save()` covers the contract without hidden IO)
  and the journal-UI docs recipe page.
- Multiple trackers may share a SaveData; key collisions between two
  trackers are the game's responsibility (documented).
- Test: `zia_test_game_quests_probe` — hidden-state no-ops, flag+counter
  stage gating, auto-advance resets the objective view, full ordered event
  trail ending in QuestComplete, one-shot semantics, and a
  register→mutate→save→fresh-tracker→re-register→load round trip.
  VM == native.

## Links

- misc/plans/thirdpersonupgrade/29-quest-tracker.md
- src/runtime/game/rt_quests.c, src/runtime/game/rt_achievement.c (template)
- docs/viperlib/game/persistence.md, ADR 0097
