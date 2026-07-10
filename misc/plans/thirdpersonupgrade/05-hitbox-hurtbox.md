# Plan 05 — Hitbox/Hurtbox Volumes with Animation-Window Activation

## 1. Objective & scope

The melee-combat foundation: bone-attached hit volumes (attack shapes) and hurt volumes (damageable regions) with activation windows driven by animation state time, producing polled hit events. ASHFALL and Xenoscape each hand-rolled this in Zia; every action game repays that cost.

**In scope:** (a) `Viper.Game3D.Hitbox3D` (kind Hit or Hurt); (b) bone/entity attachment; (c) manual + animation-window activation; (d) world combat pass producing a `HitEvent3D` polled buffer.
**Out of scope:** health/damage bookkeeping (plan 06 — same C file, lands together), projectiles, blocking/parry logic (game-side on top of events).

**Zero external dependencies — absolute.** Reuses `Collider3D` shapes, bone sockets, and the collision-event buffer pattern.

## 2. Current state (verified anchors)

- **No hitbox/health/damage code exists** in the 3D runtime (grep across `src/runtime/graphics/3d/`: no hits).
- **Bone attachment machinery is complete:** `rt_game3d_entity_attach_to_bone(_offset)` (`rt_game3d_entity.c:886-931`) → `rt_scene_node3d_attach_to_bone` (`rt_scene3d_node.c:1016`); socket poses update inside `scene_node_sync_recursive` (`rt_scene3d_helpers.inc:871`) after animator update — so a bone-keyed volume posed *after* animations is the same read the combat pass needs.
- **Bone pose access without nodes:** `rt_anim_controller3d_get_bone_matrix` / `get_bone_pose` (`rt_animcontroller3d.h`) — hitboxes can pose directly from the palette, avoiding scene-node overhead per volume.
- **Shapes:** `Collider3D` box/sphere/capsule + `rt_collider3d_compute_world_aabb_raw` (`rt_collider3d.h:72`) and the narrowphase (`rt_physics3d_collision_narrowphase.inc`) provide overlap math; hit volumes are *not* physics bodies (no broadphase membership, no contacts) — they are combat-only.
- **Event-buffer pattern to mirror:** `rt_game3d_world_collision_event_count/collision_event` (`rt_game3d.h:1314-1316`) with phase buffers, wrapper class `Collision3DEvent`, fail-closed invalid handles (`game3d.md` §BodyDef And Collision Events).
- **Anim state time:** `rt_anim_controller3d_get_state_time` / `is_state_playing` (`rt_animcontroller3d.h`) — activation windows compare against base-layer state + time.
- **Sim step insertion point:** `game3d_world_step_simulation_impl` (`rt_game3d.c:1711`) — combat pass runs after `game3d_world_update_animations` and scene sync so palettes are current-frame.

## 3. Design

### 3.1 Class and attachment

New C `src/runtime/graphics/3d/rt_game3d_combat.c` (shared with plan 06); class IDs from the next-free `rt_graphics3d_ids.h` sequence.

```c
typedef struct rt_game3d_hitbox {
    void *vptr; void *entity;         /* owner Entity3D (weak, stale-checked) */
    void *collider;                   /* retained Collider3D shape            */
    int32_t bone_index;               /* -1 = entity-space attachment         */
    double local_offset[16];          /* shape transform in bone/entity space */
    int8_t kind;                      /* 0 = Hurt, 1 = Hit                    */
    int64_t team, channel;            /* filter: hit hits hurt iff teams differ
                                         (or friendly-fire flag) and channels overlap */
    int8_t active;                    /* manual switch                        */
    /* window bindings: activate while named state time ∈ [t0, t1] */
    struct hb_window { char state[64]; double t0, t1; } windows[4]; int32_t window_count;
    int64_t last_hit_frame_vs[8];     /* per-victim rehit suppression ring    */
} rt_game3d_hitbox;
```

`New(entity, collider)` entity-space; `NewOnBone(entity, boneName, collider)` resolves the bone via the entity's `anim` controller (unknown name traps with `Game3D.Hitbox3D.NewOnBone: unknown bone '<name>'` — bone-socket diagnostic style). Registration: the world keeps a combat list per entity (`rt_game3d_entity` gains a `hitboxes` slot array; despawn releases).

### 3.2 Activation

- Manual: `set_active(bool)` — for scripted attacks.
- Windows: `bindWindow(stateName, startTime, endTime)` (up to 4) — during the combat pass, a hitbox is live if `active` OR any window matches the owner's animator base state/time. Zero-cost when the entity has no animator.

### 3.3 Combat pass and events

`game3d_world_update_combat(world)` inserted after scene sync in `game3d_world_step_simulation_impl`:

1. Collect live **Hit** volumes and all **Hurt** volumes; pose each in world space (bone palette × entity node world matrix × local offset, or node world × offset for entity-space).
2. Broad reject by world AABB (`rt_collider3d_compute_world_aabb_raw`); narrow test via the existing shape-pair narrowphase helpers.
3. Filter: skip same-entity pairs, same-team pairs (unless attacker sets `friendlyFire`), non-overlapping channels, and pairs already reported for this hitbox's current activation (rehit suppression resets when the hitbox deactivates — one hit per swing per victim).
4. Emit `HitEvent3D { attackerEntity, victimEntity, hitbox, hurtbox, point, normal }` into a frame buffer (point = deepest-overlap witness point, normal from the narrowphase axis).

Polling API on World3D: `hitEventCount()`, `hitEvent(index)`, cleared each step like collision buffers. Wrapper fails closed exactly like `Collision3DEvent` (neutral values, `+Y` normal fallback).

## 4. Implementation steps

1. `rt_game3d_combat.c` skeleton: hitbox struct, ctor pair, entity slot array, retain/release + despawn cleanup.
2. World-space posing (bone + entity paths) + debug draw hook in `Debug3D.DrawPhysics` style (wire shapes, red=hit/blue=hurt).
3. Combat pass: AABB broad phase + narrow overlap + filters + event buffer; C unit tests.
4. Window bindings against animator state/time; unit test with a scripted controller.
5. World3D polling API + `HitEvent3D` wrapper class.
6. runtime.def + audits + ADR (shared with plan 06) + docs (`game3d.md` new §Combat Volumes).
7. Zia probe `g3d_test_game3d_hitbox_probe`: two capsule fighters, one plays a swing state with a bound window, event fires exactly once per swing; deterministic replay ×2.

## 5. Public API changes (runtime.def)

```
RT_CLASS_BEGIN("Viper.Game3D.Hitbox3D", Game3DHitbox3D, "obj", Game3DHitboxNew)
    RT_PROP("Kind","i64",…) RT_PROP("Team","i64",…) RT_PROP("Channel","i64",…)
    RT_PROP("Active","i1",…) RT_PROP("FriendlyFire","i1",…)
    RT_METHOD("BindWindow","obj(obj,str,f64,f64)",…)   /* fluent */
    RT_METHOD("SetLocalOffset","obj(obj,f64,f64,f64)",…)
RT_CLASS_END()
RT_CLASS_BEGIN("Viper.Game3D.HitEvent3D", Game3DHitEvent3D, "obj", none)
    RT_PROP("Attacker","obj<Viper.Game3D.Entity3D>",get) RT_PROP("Victim","obj<Viper.Game3D.Entity3D>",get)
    RT_METHOD("Point","obj<Viper.Math.Vec3>(obj)",…)     RT_METHOD("Normal","obj<Viper.Math.Vec3>(obj)",…)
RT_CLASS_END()
```

Plus `Entity3D.attachHitbox(h)` (fluent), `World3D.hitEventCount/hitEvent/clearHitEvents`, and a `Game3D.HitboxKind` constants class (`Hurt=0`, `Hit=1`). Leaves `Hitbox3D`/`HitEvent3D`/`HitboxKind` unique. New file → source-health bump; ADR `00xx-game3d-combat-volumes.md` covers 05+06.

## 6. Tests

- **Overlap hit (C unit):** Given attacker hit-sphere on a bone posed into victim hurt-capsule — When combat pass runs — Then exactly one event with correct attacker/victim (fail-before: no API).
- **Rehit suppression:** volume stays overlapped 10 steps ⇒ still one event; deactivate + reactivate ⇒ second event.
- **Filters:** same team ⇒ none (friendlyFire ⇒ one); disjoint channels ⇒ none; self ⇒ none.
- **Window:** event fires only while `stateTime ∈ [t0,t1]` of the bound state; crossfade out of state closes the window.
- **Stale:** despawned victim entity ⇒ volume ignored, no event, no crash (`StaleEntityCalls` unchanged — the combat list is cleaned on despawn).
- **Zia probe** as §4.7, VM deterministic replay.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits + leaf checks + `check_runtime_completeness.sh`; determinism gate (combat pass is inside stepSimulation); `-L slow`.

## 8. Risks & constraints

- **Pair-count scaling:** live-hit × hurt is small in practice (few live hitboxes at once); the AABB reject keeps it O(active). If a stress case appears, bucket hurt volumes by entity AABB — do not add broadphase membership.
- **One-hit-per-activation** is a design commitment (fighting-game convention); multi-hit moves model as multiple windows.
- **Bone posing reads the palette after animation update** — never reorder the combat pass before `game3d_world_update_animations` or windows read last frame's pose.
- Hit volumes are not physics objects: raycasts/sweeps do not see them (document explicitly; plan 06's damage from projectiles goes through physics hits + `Health3D.damage`, not hurtboxes).
