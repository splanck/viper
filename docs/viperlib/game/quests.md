---
status: active
audience: public
last-verified: 2026-07-15
---

# Quests
> A bounded quest, stage, and objective state machine with polled events and SaveData persistence.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

`Viper.Game.Quests` tracks quests made of ordered stages and flag or counter objectives. It has no
clock, random source, renderer, or automatic file I/O. Game code registers definitions, applies
mutations, renders query results, polls events, and explicitly saves or loads state.

## Registration

```zia
module QuestDemo;

bind Viper.Terminal;

func start() {
    var quests = Viper.Game.Quests.New();
    quests.AddQuest("hunt", "The Wolf Hunt");
    quests.AddStage("hunt", "track", "Track the wolves");
    quests.AddFlag("hunt", "track", "find_den", "Find the den");
    quests.AddCounter("hunt", "track", "kill_scouts", "Kill scouts", 3);
    quests.AddStage("hunt", "showdown", "Slay the alpha");
    quests.AddFlag("hunt", "showdown", "kill_alpha", "Slay the alpha wolf");

    quests.Activate("hunt");
    quests.SetFlag("hunt", "find_den");
    quests.Progress("hunt", "kill_scouts", 1);
    Say(quests.CurrentStageText("hunt"));
}
```

Quest, stage, and objective IDs are documented as 1–64 ASCII characters from
`[A-Za-z0-9._-]`. Registration traps when an ID is invalid, when a stage is added before its quest,
when an objective is added before its stage, or when a fixed budget is exceeded:

| Budget | Limit |
|---|---:|
| Quests per tracker | 32 |
| Stages per quest | 16 |
| Objectives per stage | 8 |

`AddQuest`, `AddStage`, `AddFlag`, and `AddCounter` return the tracker for fluent calls. Registering
an existing ID updates its title/text and, for objectives, its kind and target without resetting
state or progress. A counter target at or below zero is normalized to `1`. Because progress is
retained, changing an objective's kind or target after play has begun can leave state inconsistent;
use re-registration for text-only refreshes unless game code also resets or migrates state.

## Lifecycle

`Activate(id)` changes a hidden quest to active and selects stage zero. A quest with no stages, or
a stage with no objectives, completes automatically when advancement is checked. `Fail(id)` changes
only an active quest to failed.

`SetFlag(quest, objective)` and `Progress(quest, objective, amount)` search only the active stage.
They return `false` for unknown IDs, inactive quests, completed objectives, and nonpositive progress
amounts. After an objective completes, the tracker advances across completed stages and marks the
quest complete after its final stage.

The current implementation does not enforce objective kind: `SetFlag` can complete a counter and
`Progress` can advance a flag. Treat that as a known defect and call the matching method. Also avoid
progress increments that could overflow a signed 64-bit integer before the target clamp.

Quest states are the integer constants `Viper.Game.QuestState.Hidden`, `Active`, `Complete`, and
`Failed`. `QuestState(id)` reports `Hidden` for an unknown quest.

## HUD Queries

| Query | Result |
|---|---|
| `ActiveCount` | Number of active quests. |
| `ActiveQuest(index)` | ID of the indexed active quest, or `""` out of range. |
| `QuestTitle(id)` | Registered title, or `""` for an unknown ID. |
| `CurrentStageText(id)` | Current stage text, or `""` when unavailable. |
| `ObjectiveCount(id)` | Objective count in the active stage, otherwise `0`. |
| `ObjectiveText(id, index)` | Objective text, or `""` out of range. |
| `ObjectiveProgress(id, index)` | Current progress, or `0` out of range. |
| `ObjectiveTarget(id, index)` | Target, or `0` out of range. |
| `ObjectiveComplete(id, index)` | Whether progress has reached the target. |

String-ID lookup is linear within the fixed budgets. Indexes are presentation indexes, not stable
persistence identifiers.

## Events

Mutations append ordered events whose kinds are exposed through
`Viper.Game.QuestEventKind`: `Activated`, `ObjectiveProgress`, `ObjectiveComplete`,
`StageComplete`, `QuestComplete`, and `QuestFailed`.

Use `EventCount()`, `EventQuestId(index)`, and `EventKind(index)` to poll them, then call
`ClearEvents()`. The buffer holds 64 entries; adding another silently drops the oldest. Out-of-range
event queries return `""` and `-1`. `JustCompleted(id)` is a separate read-and-reset flag that
returns true once after normal quest completion.

## Persistence

Registration data is not saved. Re-register definitions at startup, load the `SaveData` file, and
then apply quest state:

```zia
var save = Viper.IO.SaveData.New("mygame");
save.Load();

// Register quests before this call.
quests.Load(save);

// Later, copy tracker state into SaveData and persist that store.
if quests.Save(save) {
    save.Save();
}
```

`Save` stores state, current-stage indexes, and objective progress as one string under
`viper.quests.v1`; it does not call `SaveData.Save()`. `Load` returns `false` when the tracker has no
registrations or the key is absent/empty. Saved quest, stage, and objective IDs not present in the
current registration are ignored.

Use one tracker per SaveData file: the key is fixed and two trackers overwrite one another. The
current serializer also has a 512-byte per-quest record limit even though a legal maximum-size
quest can exceed it. The loader is not transactional and weakly parses numeric fields, so validate
or version externally supplied save data; malformed nonempty records can return `true` after
partially mutating registered quests. Save/load also currently leak temporary runtime strings.

## See Also

- [Achievement Tracking](../game.md#vipergameachievementtracker) — a smaller registry and
  notification tracker
- [Persistence](persistence.md) — SaveData paths, formats, and atomic writes
- [Game3D](../graphics/game3d.md) — interaction events can drive flag objectives
