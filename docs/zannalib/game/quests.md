---
status: active
audience: public
last-verified: 2026-07-15
---

# Quests
> A bounded quest, stage, and objective state machine with polled events and SaveData persistence.

**Part of [Zanna Runtime Library](../README.md) › [Game Utilities](README.md)**

`Zanna.Game.Quests` tracks quests made of ordered stages and flag or counter objectives. It has no
clock, random source, renderer, or automatic file I/O. Game code registers definitions, applies
mutations, renders query results, polls events, and explicitly saves or loads state.

## Registration

```zia
module QuestDemo;

bind Zanna.Terminal;

func start() {
    var quests = Zanna.Game.Quests.New();
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

Quest, stage, and objective IDs are 1–64 ASCII characters from `[A-Za-z0-9._-]`. Validation checks
the runtime string's full byte length and rejects any embedded NUL, so a NUL-bearing ID cannot pass
on only its pre-NUL prefix and then alias another ID under `strcmp`/serialization; lookups likewise
treat a NUL-bearing key as not-found rather than matching by prefix (VDOC-247). Registration traps
when an ID is invalid, when a stage is added before its quest, when an objective is added before its
stage, or when a fixed budget is exceeded:

| Budget | Limit |
|---|---:|
| Quests per tracker | 32 |
| Stages per quest | 16 |
| Objectives per stage | 8 |

`AddQuest`, `AddStage`, `AddFlag`, and `AddCounter` return the tracker for fluent calls. Registering
an existing ID updates its title/text and, for objectives, its kind and target while retaining
progress. A counter target at or below zero is normalized to `1`. Re-registering an objective clamps
retained progress to the (possibly lower) new target and re-runs stage/quest advancement, so a hot
re-registration whose new target is already met completes the stage and quest rather than stranding
an active quest on an all-complete stage (VDOC-253). Advancement is only recomputed for an active
quest, so registering objectives during hidden-quest setup behaves as before.

## Lifecycle

`Activate(id)` changes a hidden quest to active and selects stage zero. A quest with no stages, or
a stage with no objectives, completes automatically when advancement is checked. `Fail(id)` changes
only an active quest to failed.

`SetFlag(quest, objective)` and `Progress(quest, objective, amount)` search only the active stage.
They return `false` for unknown IDs, inactive quests, completed objectives, and nonpositive progress
amounts. After an objective completes, the tracker advances across completed stages and marks the
quest complete after its final stage.

Each method enforces the objective kind: `SetFlag` returns `false` (a no-op) on a counter objective
and `Progress` returns `false` on a flag objective, so a mis-wired call cannot silently change quest
state (VDOC-248). A large `Progress` increment saturates to the objective's target rather than
overflowing signed 64-bit arithmetic — the addition is only performed when it fits the remaining
headroom, otherwise the progress clamps straight to the target (VDOC-249).

Quest states are the integer constants `Zanna.Game.QuestState.Hidden`, `Active`, `Complete`, and
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
`Zanna.Game.QuestEventKind`: `Activated`, `ObjectiveProgress`, `ObjectiveComplete`,
`StageComplete`, `QuestComplete`, and `QuestFailed`.

Use `EventCount()`, `EventQuestId(index)`, and `EventKind(index)` to poll them, then call
`ClearEvents()`. The buffer holds 64 entries; adding another silently drops the oldest. Out-of-range
event queries return `""` and `-1`. `JustCompleted(id)` is a separate read-and-reset flag that
returns true once after normal quest completion.

## Persistence

Registration data is not saved. Re-register definitions at startup, load the `SaveData` file, and
then apply quest state:

```zia
var save = Zanna.IO.SaveData.New("mygame");
save.Load();

// Register quests before this call.
quests.Load(save);

// Later, copy tracker state into SaveData and persist that store.
if quests.Save(save) {
    save.Save();
}
```

`Save` stores state, current-stage indexes, and objective progress as one string under
`zanna.quests.v1`; it does not call `SaveData.Save()`. `Load` returns `false` when the tracker has no
registrations or the key is absent/empty. Saved quest, stage, and objective IDs not present in the
current registration are ignored.

Use one tracker per SaveData file: the key is fixed and two trackers overwrite one another. The
serializer appends directly to a dynamically grown buffer, so any legal quest — including a
full-budget one with maximum-length IDs and progress on every objective — serializes without a
fixed per-quest limit (VDOC-250). The loader validates the entire blob before committing: it parses
records strictly (each must lead with `q=`, every field must use a recognized prefix, and every
integer is parsed with full-token/overflow checks), returns `false` for any malformed blob, and
applies nothing until validation of the whole payload succeeds — so a corrupt or externally edited
save cannot leave a hybrid of decoded and stale state. Unknown quest/stage/objective IDs are still
tolerated so forward-compatible data patches stay safe (VDOC-252). Save and load balance their
temporary runtime-string
references, so repeated save/load cycles do not leak (VDOC-251).

## See Also

- [Achievement Tracking](../game.md#zannagameachievementtracker) — a smaller registry and
  notification tracker
- [Persistence](persistence.md) — SaveData paths, formats, and atomic writes
- [Game3D](../graphics/game3d.md) — interaction events can drive flag objectives
