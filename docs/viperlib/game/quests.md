# Quests

`Viper.Game.Quests` is a quest/objective tracker: quests → stages →
objectives (flags and counters), activation, polled progress events, and
SaveData persistence. It is a pure state machine — no clock, no randomness,
no graphics coupling — usable from 2D and 3D games alike.

**Load-order contract:** registration data (titles, texts, targets) is
*not* saved. Re-register your quests at boot, then `Load` applies saved
state; `Load` before any registration returns `false`.

## Registration

Fluent, idempotent on string ids (`[A-Za-z0-9._-]`, ≤64 chars — re-adding
an id updates its text, which makes hot data reloads safe). Budgets trap at
registration: 32 quests, 16 stages per quest, 8 objectives per stage.

```zia
var quests = Game.Quests.New();
quests.AddQuest("hunt", "The Wolf Hunt");
quests.AddStage("hunt", "track", "Track the wolves");
quests.AddFlag("hunt", "track", "find_den", "Find the den");
quests.AddCounter("hunt", "track", "kill_scouts", "Kill scouts", 3);
quests.AddStage("hunt", "showdown", "Slay the alpha");
quests.AddFlag("hunt", "showdown", "kill_alpha", "Slay the alpha wolf");
```

## Lifecycle

`Activate(id)` moves Hidden → Active. `SetFlag(quest, obj)` and
`Progress(quest, obj, amount)` act on the **active stage** only; a stage
auto-advances when all of its objectives complete, and the final stage
completes the quest. `Fail(id)` marks an active quest failed. Every
mutation on a Hidden/Complete/Failed quest is a safe no-op returning
`false`, and unknown ids never trap — HUD code polls freely.

States are `Game.QuestState.Hidden/Active/Complete/Failed`
(`QuestState(id)` reports Hidden for unknown ids).

## HUD queries

`ActiveCount` / `ActiveQuest(i)` enumerate active quests;
`QuestTitle(id)`, `CurrentStageText(id)`, `ObjectiveCount(id)`, and
`ObjectiveText/Progress/Target/Complete(id, i)` provide everything a
journal or tracker widget renders. String ids cost a linear lookup —
polling a handful of quests per frame is trivial; do not "optimize" to
indices, they are not save-stable.

## Events

Transitions append to an ordered 64-entry buffer of
`{questId, Game.QuestEventKind.*}`: Activated, ObjectiveProgress,
ObjectiveComplete, StageComplete, QuestComplete, QuestFailed. Poll
`EventCount()/EventQuestId(i)/EventKind(i)` each frame for toasts, then
`ClearEvents()`. `JustCompleted(id)` is a read-reset one-shot for
completion fanfare.

## Persistence

```zia
var save = Viper.IO.SaveData.New("mygame");
quests.Save(save);   // one value under "viper.quests.v1"
save.Save();
// ... next session, after re-registering:
quests.Load(save);
```

`Save` stores quest state (state/stage/objective progress) under a single
versioned key; SaveData's atomic-write JSON layer owns file IO. Saved ids
unknown to the current registration are tolerated, so shipping a data
patch never corrupts old saves. Two trackers auto-sharing one SaveData
must not collide on the key — one tracker per save file is the intended
shape.

## See Also

- [Achievement Tracking](../game.md#vipergameachievementtracker) — the
  structural sibling (registry + polled notifications)
- [Persistence](persistence.md) — SaveData paths and atomic writes
- Game3D interaction events (`Interactor3D.LastInteracted`) pair naturally
  with `SetFlag` for fetch/talk objectives.
