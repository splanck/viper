# Plan 29 — `Viper.Game.Quests`: Objective Tracker with SaveData Integration

## 1. Objective & scope

Quest/objective state is the backbone every adventure game rebuilds: quests → stages → objectives (flags and counters), activation, progress events for HUD toasts, and persistence. Ship it as a 2D/3D-agnostic `Viper.Game` component beside `AchievementTracker` (its structural template), with automatic SaveData round-trip.

**In scope:** (a) quest/stage/objective registry + lifecycle; (b) polled update events + one-shot flags; (c) SaveData JSON persistence; (d) HUD-consumable state getters (active quest, objective lines, progress fractions).
**Out of scope:** quest *authoring* formats (games register from their own data), journal UI (GameUI recipe in docs), rewards/inventory.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No quest system exists** (`src/runtime/game/` listing: achievements, dialogue, config, timers, … — no quests).
- **Structural template:** `rt_achievement.c` (12 KB) — process-owned registry (`AchievementTracker.New`), stat counters, `Unlock/IsUnlocked`, `Update/Draw` hooks (`docs/viperlib/game/` docs) — the registry + polled-notification shape to mirror.
- **Persistence:** `Viper.Game.SaveData` JSON key-value with atomic writes and platform paths (`docs/viperlib/game/persistence.md`; §Design Notes: atomic, missing-file-safe, last-write-wins) — quests serialize into a reserved key namespace (`"quests.*"` or one JSON blob key).
- **Event conventions:** buffered counts + one-shot flags (collision/anim-event patterns).
- **HUD:** GameUI `HudLabel/Bar/Panel` (`docs/viperlib/game/ui.md`) — docs recipe territory, no runtime coupling.
- **Class naming:** 2D-tier `Viper.Game.*` namespace; leaf uniqueness rule applies (`Quests` unique).

## 3. Design

### 3.1 Model

New C `src/runtime/game/rt_quests.c/h` (2D/3D-agnostic — no canvas/scene deps):

```c
objective := { id, text, kind: Flag|Counter, target (counter), progress, complete }
stage     := { id, text, objectives[≤8], complete (all objectives complete) }
quest     := { id, title, stages[≤16], state: Hidden|Active|Complete|Failed, current_stage }
tracker   := { quests[], event ring, dirty flag }
```

`Quests.New()` (instance object like `AchievementTracker` — supports multiple profiles; games typically hold one):

- **Registration (fluent, at load):** `addQuest(id, title)`, `addStage(questId, stageId, text)`, `addFlag(questId, stageId, objId, text)`, `addCounter(questId, stageId, objId, text, target)`. Registration is idempotent on ids (re-register updates text/target — supports hot data reload); ids are strings (stable across sessions, the plan-17 lesson).
- **Lifecycle:** `activate(questId)` (Hidden→Active), `fail(questId)`; objectives: `setFlag(questId, objId)` (searches active stage), `progress(questId, objId, amount)`; stage auto-advances when its objectives complete; quest completes after the last stage. Completing an inactive/complete quest is a safe no-op returning false.
- **Queries for HUD:** `activeCount/activeQuest(i)`, `questTitle/questState(id)`, `currentStageText(id)`, `objectiveCount(id)`, `objectiveText/objectiveProgress/objectiveTarget/objectiveComplete(id, i)` — everything a journal or tracker widget needs, string/number-typed.

### 3.2 Events

Ring buffer of `{questId, kind}` with kinds `Activated/ObjectiveProgress/ObjectiveComplete/StageComplete/QuestComplete/QuestFailed`; polled `eventCount()/eventQuestId(i)/eventKind(i)`, cleared by `clearEvents()` (explicit — the tracker has no world tick; games poll each frame). One-shot convenience `justCompleted(questId)`.

### 3.3 Persistence

- `save(saveData)` / `load(saveData)` against a `Viper.Game.SaveData` handle: one JSON object under key `"viper.quests.v1"` — `{questId: {state, stage, objectives: {objId: progress}}}`. Registration data (titles/texts/targets) is *not* saved — games re-register at boot, then `load` applies state (id-matched; unknown saved ids ignored with a diagnostic count — data patches stay safe).
- `autoBind(saveData)`: after binding, any state mutation marks dirty and `save` happens on the SaveData handle's own `Save()` call (no hidden IO; SaveData owns write timing per its atomic-write contract).

## 4. Implementation steps

1. Model + registration + lifecycle (activate/flag/counter/auto-advance); C unit tests over a 2-stage quest matrix.
2. Query surface + event ring + one-shots.
3. SaveData serialization round-trip + unknown-id tolerance + version key.
4. runtime.def + audits + ADR + docs (`docs/viperlib/game/quests.md` new page following the achievements page structure; journal-HUD recipe).
5. Zia probe `game_quests_probe` (2D-tier test binary per the `viper_add_test` pattern) + a Game3D docs snippet (fetch-quest wired to plan-21 interaction events).

## 5. Public API changes (runtime.def)

```
RT_FUNC(GameQuestsNew, rt_quests_new, "Viper.Game.Quests.New", "obj()")
RT_CLASS_BEGIN("Viper.Game.Quests", GameQuests, "obj", GameQuestsNew)
    RT_METHOD("addQuest","obj(obj,str,str)",…) RT_METHOD("addStage","obj(obj,str,str,str)",…)
    RT_METHOD("addFlag","obj(obj,str,str,str,str)",…) RT_METHOD("addCounter","obj(obj,str,str,str,str,i64)",…)
    RT_METHOD("activate","i1(obj,str)",…) RT_METHOD("fail","i1(obj,str)",…)
    RT_METHOD("setFlag","i1(obj,str,str)",…) RT_METHOD("progress","i1(obj,str,str,i64)",…)
    RT_PROP("activeCount","i64",get) RT_METHOD("activeQuest","str(obj,i64)",…)
    RT_METHOD("questTitle","str(obj,str)",…) RT_METHOD("questState","i64(obj,str)",…)
    RT_METHOD("currentStageText","str(obj,str)",…)
    RT_METHOD("objectiveCount","i64(obj,str)",…) RT_METHOD("objectiveText","str(obj,str,i64)",…)
    RT_METHOD("objectiveProgress","i64(obj,str,i64)",…) RT_METHOD("objectiveTarget","i64(obj,str,i64)",…)
    RT_METHOD("objectiveComplete","i1(obj,str,i64)",…)
    RT_METHOD("eventCount","i64(obj)",…) RT_METHOD("eventQuestId","str(obj,i64)",…)
    RT_METHOD("eventKind","i64(obj,i64)",…) RT_METHOD("clearEvents","void(obj)",…)
    RT_METHOD("justCompleted","i1(obj,str)",…)
    RT_METHOD("save","i1(obj,obj)",…) RT_METHOD("load","i1(obj,obj)",…) RT_METHOD("autoBind","void(obj,obj)",…)
RT_CLASS_END()
```

Plus a `Viper.Game.QuestState` constants class (Hidden/Active/Complete/Failed) and `QuestEventKind`. Leaves unique. New files → source-health; ADR `00xx-game-quests.md`.

## 6. Tests

- **Lifecycle (C unit):** flag+counter stage completes only when the counter hits target AND the flag is set; stage auto-advance; final stage completes the quest with the full event sequence in order (fail-before: no API).
- **Safe no-ops:** progress on Hidden/Complete quests returns false, no events; unknown ids return false/"" (no traps — HUD code polls freely).
- **Events:** ring holds ordered kinds; `clearEvents` empties; `justCompleted` one-shot semantics.
- **Persistence:** register → mutate → save → fresh tracker + re-register → load ⇒ state identical (query-surface comparison); saved-unknown-id tolerated; double save/load idempotent; JSON key stable (`"viper.quests.v1"`).
- **Determinism:** no time/randomness anywhere — pure state machine (assert no clock reads by construction/review).

## 7. Verification gates

Full build + ctest; `check_runtime_completeness.sh` + surface audits + leaf checks; the SaveData suite stays green; `-L slow`. No graphics/simulation coupling — no determinism-gate rerun needed.

## 8. Risks & constraints

- **Registration-not-persisted** is the load-order contract: games must re-register before `load` — the docs page leads with it and `load` before any registration returns false with a diagnostic.
- **Bounded shapes** (16 stages/8 objectives) trap at registration when exceeded — early, loud, and generous for the genre.
- **String ids** cost hash lookups at query time — HUD polling a handful of quests per frame is trivial; noted so nobody "optimizes" to indices and breaks save stability.
- Multiple trackers are allowed but share nothing — save-key collisions across two auto-bound trackers on one SaveData are the game's responsibility (documented).
