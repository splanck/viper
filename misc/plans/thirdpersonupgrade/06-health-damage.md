# Plan 06 — `Health3D` + Damage/Death Event Layer

## 1. Objective & scope

The bookkeeping half of combat: per-entity health with damage application, invulnerability frames, knockback, and polled damage/death events. Pairs with plan 05 (same C file, same ADR); together they let a game go from "hit volume overlapped" to "enemy staggered, took 25 damage, died" without hand-rolled state.

**In scope:** (a) `Viper.Game3D.Health3D` component; (b) `Entity3D.attachHealth`; (c) damage/heal/i-frames/death lifecycle; (d) knockback helper; (e) polled `DamageEvent3D` buffer + one-shot flags.
**Out of scope:** damage formulas/resistances (game math), UI bars (GameUI `Bar` exists), respawn logic.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No health/damage code in the 3D runtime** (verified grep; the only "health" hits are 2D `rt_entity.c` fields, which are 2D-canvas-tier and not reachable from Entity3D).
- **2D precedent for component style:** `Viper.Game.*` components are small C structs with `New` + property accessors + polled flags (e.g. `rt_achievement.c`, `rt_statemachine.c`) — `Health3D` follows this shape in `rt_game3d_combat.c` (plan 05's file).
- **Entity slots:** `rt_game3d_entity` carries typed slots (`anim`, `body`, plan-05 `hitboxes`); adding `health` follows `attachBody`/`attachAnimator` retention patterns (`game3d.md` §Entities: fluent attach, world-despawn releases, stale-entity fail-closed getters incrementing `Game3D.Diagnostics.StaleEntityCalls`).
- **Knockback primitive:** `rt_body3d_apply_impulse_at_point` (`rt_physics3d.h:290`) — off-center hits also spin the body; characters (kinematic) instead receive a velocity nudge through gameplay, so the helper targets the entity's dynamic body when present and no-ops otherwise (documented).
- **Event buffers:** mirror `collisionEvent`/plan-05 `hitEvent` polled-buffer pattern (`rt_game3d.h:1314-1316` shape).
- **Damage source typing:** plan 20 adds `Physics3DBody` user data; until then the event carries source `Entity3D` (nullable) + an `i64 tag` the caller supplies.

## 3. Design

### 3.1 Component

```c
typedef struct rt_game3d_health {
    void *vptr; void *entity;      /* weak owner backref                    */
    double max_hp, hp;
    double invuln_seconds;         /* i-frame duration granted per damage   */
    double invuln_remaining;       /* ticked by the world combat pass       */
    int8_t dead;                   /* latched at hp<=0                      */
    int8_t just_died, just_damaged;/* one-shot flags, cleared next step     */
    double last_damage;            /* most recent applied amount            */
    int64_t last_tag;              /* caller-supplied damage tag            */
} rt_game3d_health;
```

`New(maxHp)`; `Entity3D.attachHealth(h)` fluent, one per entity (reattach replaces). The world combat pass (plan 05's `game3d_world_update_combat`) ticks `invuln_remaining` and clears one-shot flags at step start — the same lifecycle collision transition buffers use.

### 3.2 Damage lifecycle

`damage(amount, sourceEntity, tag) -> f64` (applied amount): returns 0 and does nothing while `invuln_remaining > 0` or `dead`; clamps `hp`; grants `invuln_seconds` of i-frames; sets `just_damaged`; on crossing to `hp <= 0` latches `dead` + `just_died` and emits a death event. `heal(amount)`; `revive(hp)` clears `dead`. All mutations emit `DamageEvent3D { victim, source, amount, tag, wasLethal }` into the world buffer — one buffer for damage and death (`wasLethal` distinguishes), keeping the polling surface small.

### 3.3 Knockback helper

`applyKnockback(direction, strength, point)`: if the entity has a dynamic body ⇒ `rt_body3d_apply_impulse_at_point(strength × dir, point)` + wake; if kinematic/static/none ⇒ no-op returning false so gameplay can route to its character controller instead. Not automatic on damage — an explicit call, typically from the same code that polls hit events.

### 3.4 Convenience wiring with plan 05

`World3D.autoDamageFromHits(enabled, amount... )` is **not** added — damage amounts are game data. Instead docs show the canonical loop:

```zia
var n = world.hitEventCount();
for i in 0..n {
    var hit = world.hitEvent(i);
    var victimHealth = Game3D.Entity3D.get_health(Game3D.HitEvent3D.get_Victim(hit));
    if (victimHealth != null) { Game3D.Health3D.damage(victimHealth, 25.0, attacker, DMG_SLASH); }
}
```

## 4. Implementation steps

1. Struct + `New` + props + attach slot + despawn/stale plumbing.
2. Damage/heal/revive lifecycle + i-frame ticking in the combat pass; C unit tests.
3. `DamageEvent3D` buffer + World3D polling API + wrapper class (fail-closed).
4. Knockback helper + unit test (dynamic sphere gains velocity; kinematic returns false).
5. runtime.def + audits + shared 05/06 ADR + docs (`game3d.md` §Combat Volumes gains §Health).
6. Zia probe `g3d_test_game3d_health_probe`: swing → hit event → damage → i-frames block second swing → third swing after expiry kills → `just_died` + lethal event; deterministic replay ×2.

## 5. Public API changes (runtime.def)

```
RT_FUNC(Game3DHealthNew, rt_game3d_health_new, "Viper.Game3D.Health3D.New", "obj(f64)")
RT_CLASS_BEGIN("Viper.Game3D.Health3D", Game3DHealth3D, "obj", Game3DHealthNew)
    RT_PROP("Current","f64",get) RT_PROP("Max","f64",get/set) RT_PROP("IsDead","i1",get)
    RT_PROP("InvulnSeconds","f64",get/set) RT_PROP("Invulnerable","i1",get)
    RT_METHOD("Damage","f64(obj,f64,obj,i64)",…) RT_METHOD("Heal","void(obj,f64)",…)
    RT_METHOD("Revive","void(obj,f64)",…)
    RT_METHOD("JustDied","i1(obj)",…) RT_METHOD("JustDamaged","i1(obj)",…)
    RT_METHOD("LastDamage","f64(obj)",…) RT_METHOD("LastTag","i64(obj)",…)
    RT_METHOD("ApplyKnockback","i1(obj,obj<Viper.Math.Vec3>,f64,obj<Viper.Math.Vec3>)",…)
RT_CLASS_END()
RT_CLASS_BEGIN("Viper.Game3D.DamageEvent3D", Game3DDamageEvent3D, "obj", none)
    RT_PROP("Victim","obj<Viper.Game3D.Entity3D>",get) RT_PROP("Source","obj<Viper.Game3D.Entity3D>",get)
    RT_PROP("Amount","f64",get) RT_PROP("Tag","i64",get) RT_PROP("WasLethal","i1",get)
RT_CLASS_END()
```

Plus `Entity3D`: `RT_METHOD("attachHealth","obj(obj,obj<Viper.Game3D.Health3D>)")` + `RT_PROP("health","obj<Viper.Game3D.Health3D>",get)`; `World3D`: `damageEventCount/damageEvent/clearDamageEvents`. Leaves `Health3D`/`DamageEvent3D` unique.

## 6. Tests

- **Lifecycle (C unit):** 100 hp − 40 ⇒ 60, `just_damaged`; −70 ⇒ 0, `dead`+`just_died`, lethal event; further damage returns 0 (fail-before: no API).
- **I-frames:** `invulnSeconds 0.5`, two damages 0.1 s apart ⇒ second returns 0; after 0.6 s ⇒ applies.
- **Flags one-shot:** `just_damaged` true for exactly one step.
- **Events:** buffer holds victim/source/amount/tag; cleared next step; invalid wrapper handle fails closed.
- **Knockback:** dynamic body velocity change ≈ impulse/mass; off-center point produces angular velocity; kinematic ⇒ false.
- **Stale:** health getter on despawned entity ⇒ null + `StaleEntityCalls` increment.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; determinism gate (state mutates inside stepSimulation); `-L slow`.

## 8. Risks & constraints

- **No auto-damage coupling** to plan 05 — keeps damage as game data; revisit only with real demand.
- **One health per entity** (slot semantics); multi-part bosses attach health to child entities.
- **Flag clearing at step start** must be ordered before the combat pass emits new events (same-step damage must survive to the poll point after the step).
- Death does not despawn — games own corpse/ragdoll handoff (plan 07's `enableRagdoll` is the natural pairing; cross-reference in docs).
