---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0078: Graphics3D Ragdoll Pipeline

Date: 2026-07-10

## Status

Accepted

## Context

Every primitive a ragdoll needs existed — capsule bodies, 6-DoF joints with
per-axis limits captured relative to their creation pose, bone palettes with
previous-frame snapshots — but nothing assembled them into the
death/hit-reaction pipeline a third-person adventure game requires.

## Decision

`Zanna.Graphics3D.Ragdoll3D` (`rt_ragdoll3d.c`, id `-0x603052`) plus three
internal support APIs:

- `rt_skeleton3d_get_bone_parent_raw` / `get_bone_bind_local_raw` expose the
  skeleton topology to the builder (policy-classified internal symbols).
- `rt_anim_controller3d_apply_pose_override(mask, globals)` overwrites masked
  bones' model-space globals after evaluation, propagates the delta to
  unmasked descendants (parent-first, affine-inverse delta), and refreshes the
  skinning palette — the write-back hook the ragdoll (and future systems) use
  between animation update and scene sync.

The builder fits one capsule per bone at least `MinBoneLength` long (chain
parents preserved; terminals use a cap length), mass distributed by relative
volume, and a limited 6-DoF joint per child↔parent anchored at the child bind
origin — because both bodies are posed at bind when the joint is created, the
per-axis limits are bind-relative by construction. Rig bodies share a
dedicated collision layer excluding rig-vs-rig contacts (anchors overlap by
construction); CCD stays off (its conservative bounding-sphere sweep makes
thin horizontal limbs hover above surfaces).

Activation poses bodies from the controller's **skin palette × bind global**
(not `final_globals`) so the current pose and the previous-palette pose used
for finite-difference velocity seeding share one representation — an identity
palette then means the bind pose and a static clip seeds zero velocity.
`Step(dt)` (called by Game3D between physics and scene sync) writes body poses
back as model-space globals, root-follows the node (translations shifted the
same step so world-space vertices never pop), and PD-drives powered joints
toward the animated pose. `Deactivate(blend)` removes the rig and nlerps the
captured pose to live animation. Game3D sugar: `Entity3D.EnableRagdoll()` /
`DisableRagdoll(blend)` with the rig cached on the entity.

## Consequences

`JustDied → EnableRagdoll` is a two-call corpse pipeline. v1 constraints: rig
poses assume approximately uniform entity scale; platform ride is yaw-only;
multiple simultaneous ragdolls raise solver island cost (practical cap
documented; sleeping handles settled rigs). Verified by
`test_rt_game3d_ragdoll_time`: 5-bone chain builder mass distribution,
activate → settle exactly at capsule resting height over 300 deterministic
steps, blend-out completion, and the Game3D sugar loop.
