# ADR 0077: Game3D Combat Volumes And Health

Date: 2026-07-10

## Status

Accepted

## Context

Melee combat had no runtime foundation: ASHFALL and Xenoscape each hand-rolled
hit volumes, damage bookkeeping, and i-frames in Zia. Every action game repays
that cost. The runtime already had all the primitives — collider shapes, bone
palettes, the narrow-phase, and the buffered polling-event pattern — but
nothing assembled them.

## Decision

One C file (`rt_game3d_combat.c`) adds the paired combat layer:

- `Viper.Game3D.Hitbox3D` (id `-0x60304E`) — combat-only volumes (never
  physics bodies; raycasts and sweeps do not see them) attached entity-space
  or to animator bones, with team/channel filters, friendly fire, manual
  activation, and up to four animation windows (`BindWindow(state, t0, t1)`
  against the animator base state/time). The world combat pass runs after
  animation + scene sync inside `stepSimulation`: AABB broad reject, then the
  shared `rt_collider3d_overlap_at_raw` narrow-phase primitive (scratch bodies
  through `test_collision`, added for this purpose and reusable by future
  volume queries). One hit per activation per victim; the suppression ring
  resets when the volume goes inactive. Events land in a polled
  `HitEvent3D` buffer cleared each step (`World3D.hitEventCount/hitEvent`),
  fail-closed like `Collision3DEvent`.
- `Viper.Game3D.Health3D` (id `-0x603050`) — per-entity hp with
  damage/heal/revive, i-frames granted per applied hit and ticked by the
  combat pass, latched death, one-shot `JustDamaged`/`JustDied` flags cleared
  at step start (so same-step damage survives to the post-step poll), a polled
  `DamageEvent3D` buffer (`wasLethal` distinguishes death), and an explicit
  knockback helper that impulses dynamic bodies and returns false otherwise.
  No auto-damage coupling: damage amounts are game data; docs show the
  canonical hit-event → Damage loop.

Ownership is cycle-free: entities retain their hitboxes/health; components
hold plain backrefs NULLed at entity teardown so surviving handles fail
closed.

## Consequences

Games go from "volumes overlapped" to "enemy staggered, took 25, died" with
no hand-rolled state. Hit volumes not being physics objects is a documented
contract — projectile damage goes through physics hits + `Health3D.Damage`,
not hurtboxes. One-hit-per-activation is the fighting-game convention;
multi-hit moves are modeled as multiple windows. Covered by
`test_rt_game3d_combat` (7 scenarios including window gating with a scripted
controller) and the `g3d_test_game3d_combat_probe` Zia probe.
