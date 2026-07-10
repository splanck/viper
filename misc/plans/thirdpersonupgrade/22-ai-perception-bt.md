# Plan 22 — `Perception3D` (Sight/Hearing) + `BehaviorTree3D` Runtime

## 1. Objective & scope

Give NPC AI its two missing layers: **perception** (sight cones with budget-capped LoS raycasts, hearing from reported sound events) and **decision** (a data-built behavior-tree runtime whose leaves drive the systems that already exist — NavAgent3D movement, Animator3D states, entity facing). ASHFALL (`ai/` directory) and Xenoscape (`ai.zia`) each hand-rolled both layers; `Behavior3D` covers only ambient motion presets.

**In scope:** (a) `Perception3D` component with polled seen/lost/heard events; (b) `World3D.reportSound` stimulus; (c) `BehaviorTree3D` node set + tick integration; (d) built-in action leaves + polled custom-leaf requests.
**Out of scope:** squad coordination, utility-AI scoring, nav link auto-traversal animations (games react to `OnOffMeshLink`), scripting BT leaves via VM callbacks (polled request pattern instead).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No perception/BT code:** `Behavior3D` is fixed motion presets (spin/orbit/chase/path…, `game3d.md` §Behavior3D); `rt_behaviortree`/perception greps return nothing in the 3D runtime. 2D has `rt_statemachine.c` (generic FSM) and `rt_behavior.c` — the component conventions, not the algorithms.
- **Movement layer complete:** `NavAgent3D` (targets, RVO avoidance, `BindCharacter/BindNode`, `OnOffMeshLink/LinkKind`, `rendering3d.md` §NavAgent3D).
- **Animation layer complete:** `Animator3D.play/crossfade/isPlaying` (`game3d.md` §Animator3D).
- **LoS primitive:** `rt_world3d_raycast` with masks (`rt_physics3d.h:165`); budget-capping precedent: per-frame budgets exist across the runtime (occlusion triangle budget, commit budgets).
- **Polled-event convention:** buffered counts/index accessors (collision/hit/interaction events); one-shot flags.
- **Tick insertion:** `game3d_world_step_simulation_impl` (`rt_game3d.c:1711`) — AI (perception then trees) ticks before controllers so decisions feed the same step's movement.

## 3. Design

### 3.1 `Perception3D`

New C `src/runtime/graphics/3d/rt_game3d_ai.c`. Component on an entity (`Entity3D.attachPerception`):

- **Sight:** `setSight(rangeM, fovDegrees, eyeHeight)`; candidate set = entities on `targetMask` layers within range (world entity registry scan — same bookkeeping as plan 21's list); per-candidate visibility = cone test then LoS raycast (eye → target pivot, `losMask`). **Budget:** world-level `setPerceptionBudget(raysPerStep)` (default 16) round-robins raycasts across all perceivers/candidates; between updates, last-known visibility persists (stale-by-budget is invisible at 16 rays/step for typical scenes).
- **State + events:** per-target seen-state with enter/exit hysteresis (`timeToSee` 0.3 s accumulating while visible, `timeToLose` 2.0 s); transitions emit polled events `{perceiver, target, kind: Seen|Lost}` in a world AI-event buffer; `seenCount()/seenTarget(i)` accessors for current state; `lastKnownPosition(target)`.
- **Hearing:** `World3D.reportSound(position, loudness, i64 tag)` (gameplay calls it on gunshots/footsteps — plan 23 can call it automatically for player footsteps); perceivers with `setHearing(rangeAtLoudness1)` receive `{Heard, position, tag}` events when `distance ≤ range × loudness` — no occlusion math v1 (documented).

### 3.2 `BehaviorTree3D`

Data-built immutable tree + per-entity instance state (node cursor, timers), ticked after perception:

- **Composites:** `Sequence`, `Selector`, `Parallel(successPolicy)`; **decorators:** `Inverter`, `Succeeder`, `Repeat(n|forever)`, `Cooldown(seconds)`, `WhileSeen(perception)` (condition-gated subtree).
- **Conditions:** `CanSee(targetRef)`, `HeardRecently(seconds)`, `HealthBelow(fraction)` (plan 06), `DistanceTo(targetRef, lessThan)`, `Custom(id)` (polled, below).
- **Actions:** `MoveTo(targetRef | lastKnown | point)` (drives the bound NavAgent3D; Running until `RemainingDistance ≤ stopping`), `Face(targetRef)`, `PlayAnim(state, crossfade)` (Running until state completes or loops once), `Wait(seconds)`, `SetAnimBool`-style knobs deferred, `Custom(id)`.
- **Custom leaves (no VM callbacks):** ticking a `Custom(id)` node parks the tree in Running and pushes `{entity, id}` into a polled request buffer; the game fulfills it and calls `bt.resolve(entity, id, Success|Failure)` — the tree resumes next tick. This keeps script logic in Zia without trampolines.
- Building is fluent from Zia: `BT.sequence() / BT.moveTo(...)` static builders returning node handles, `BehaviorTree3D.New(rootNode)`; `Entity3D.attachBehaviorTree(tree)` — trees are shared/immutable, instance state lives on the entity slot; `targetRef` = a named blackboard slot (`setTarget(entity)`, `setTargetPoint(vec3)` per entity instance — a 4-slot mini-blackboard, not a general store).

## 4. Implementation steps

1. Perception component: sight cone + budgeted LoS + hysteresis + events; C unit tests (visibility timeline with a wall).
2. Hearing + `reportSound`.
3. BT core: node arena, tick semantics (Running/Success/Failure), composites/decorators; pure-C tests on scripted condition stubs (no world).
4. Built-in conditions/actions bound to NavAgent3D/Animator3D/blackboard; world tick integration + instance state on entities.
5. Custom-leaf request buffer + `resolve`.
6. runtime.def + audits + ADR + docs (`game3d.md` new §AI: perception + a patrol/chase/attack tree example replacing the ASHFALL-style hand-rolled loop).
7. Zia probe `g3d_test_game3d_ai_probe`: guard patrols (MoveTo loop), sees player (event), chases (WhileSeen + MoveTo lastKnown), loses, returns — deterministic replay ×2.

## 5. Public API changes (runtime.def)

```
"Viper.Game3D.Perception3D": New(entity); setSight(f64,f64,f64), setHearing(f64),
    setTargetMask(i64), setLosMask(i64); seenCount()->i64, seenTarget(i64)->obj<Entity3D>,
    lastKnownPosition(obj<Entity3D>)->obj<Vec3>
"Viper.Game3D.BT" (static, "none"): sequence()/selector()/parallel(i64) -> obj<BTNode3D>,
    inverter(node), repeat(node,i64), cooldown(node,f64), whileSeen(node),
    canSee()/heardRecently(f64)/healthBelow(f64)/distanceTo(i64,f64),
    moveTo(i64)/face(i64)/playAnim(str,f64)/wait(f64)/custom(i64)  /* i64 = blackboard slot / id */
"Viper.Game3D.BTNode3D": add(child) fluent (composite children)
"Viper.Game3D.BehaviorTree3D": New(root); resolve(obj<Entity3D>,i64,i1)
Entity3D: attachPerception, attachBehaviorTree, setTarget(i64,obj<Entity3D>), setTargetPoint(i64,obj<Vec3>)
World3D: reportSound(obj<Vec3>,f64,i64), setPerceptionBudget(i64),
         aiEventCount()/aiEvent(i64) -> AIEvent3D {perceiver,target,kind,position,tag},
         btRequestCount()/btRequest(i64) -> {entity,id}
```

Leaves `Perception3D/BT/BTNode3D/BehaviorTree3D/AIEvent3D` unique. New file → source-health; ADR `00xx-game3d-ai.md`.

## 6. Tests

- **Sight timeline (C unit):** target crosses the cone behind a wall gap — seen event fires after `timeToSee` inside the gap, lost after `timeToLose` past it (fail-before: no API).
- **Budget:** 32 perceivers × 4 candidates at budget 16 ⇒ ≤16 raycasts/step (counter), full visibility convergence within N steps; results identical for budget 16 vs 1024 after convergence (order-independence).
- **BT semantics:** Sequence short-circuits on Failure, resumes Running child; Parallel policies; Cooldown gates.
- **Action leaves:** MoveTo reaches within stopping distance then Success; PlayAnim Running while state active.
- **Custom leaf:** request appears in buffer; `resolve(Success)` advances the sequence next tick.
- **Determinism:** guard-patrol probe replay ×2 identical; worker-count parity (AI is main-thread).

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; determinism gate; `-L slow`.

## 8. Risks & constraints

- **Scope discipline:** this is a *small* BT (one arena, ~12 node types) — resist blackboard generalization; 4 typed slots cover patrol/chase/attack.
- **Perception fairness:** round-robin must be deterministic (fixed iteration order over the registry) or replays diverge — index-ordered, tested.
- **Tick order commitment:** perception → trees → controllers → physics; document in the frame-order table (semantic freeze).
- Custom-leaf latency is one step by design (poll cadence); note it — reaction-time-critical logic belongs in built-in leaves.
