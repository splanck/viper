# Plan 21 — `Interactable3D` / `Interactor3D` Focus-and-Use System

## 1. Objective & scope

Every adventure verb — doors, chests, levers, NPC talk, item pickup — sits on the same loop: find the best interactable in front of the player, show a prompt, fire on use. Provide that loop once: `Interactable3D` components on entities, one `Interactor3D` on the player, focus scanning with view-cone + distance + line-of-sight, polled events, and a highlight hook.

**In scope:** (a) `Interactable3D` component; (b) `Interactor3D` scanner; (c) polled interaction/focus events; (d) prompt string plumbing; (e) emissive highlight sugar.
**Out of scope:** prompt UI rendering (GameUI widgets exist; docs show the recipe), hold-to-interact timers (game-side on top of `interactHeld`), inventory (out of engine scope).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No interaction system exists** in the runtime (grep `interact` under `src/runtime/graphics/3d/` — none).
- **Trigger primitives are adjacent but insufficient:** `Trigger3D` standalone AABB zones with enter/exit (`rt_physics3d.h:353-366`) and trigger bodies via `BodyDef.asTrigger()` — zone tests, not "best candidate in view with LoS" selection.
- **Queries:** `rt_world3d_overlap_sphere` + `rt_world3d_raycast` (`rt_physics3d.h:165-176`) — the scanner's primitives.
- **Component/slot pattern:** `Entity3D` typed slots + fluent attach + stale fail-closed semantics (`game3d.md` §Entities); plans 05/06 extend the same shape (`hitboxes`, `health`).
- **Highlight primitive:** `Material3D.MakeInstance` + `SetEmissiveColor`/`set_EmissiveIntensity` (runtime.def Material3D block) — per-entity emissive nudge without touching shared materials (plan 01 uses the same instance trick for fades).
- **Event polling convention:** buffered counts + index accessors + one-shot flags (collision events, `game3d.md` §BodyDef And Collision Events).
- **Prompt text rendering:** overlay HUD path + GameUI widgets (`game/ui.md`) — game-side.

## 3. Design

### 3.1 `Interactable3D`

New C `src/runtime/graphics/3d/rt_game3d_interact.c`:

```c
typedef struct rt_game3d_interactable {
    void *vptr; void *entity;    /* owner (weak, stale-checked)             */
    double radius;               /* interaction range override (default 2.0)*/
    rt_string prompt;            /* "Open", "Talk", … (game/localized text) */
    int64_t kind;                /* free game tag (door/npc/chest…)         */
    int8_t enabled;
    double focus_priority;       /* tie-breaker bonus (default 0)           */
} rt_game3d_interactable;
```

`New(entity)` + fluent `withPrompt(str)/withKind(i64)/withRadius(f64)`; `Entity3D.attachInteractable` slot (one per entity). Registered in a world interactable list on spawn (mirrors plan-05 combat list bookkeeping; despawn removes).

### 3.2 `Interactor3D`

One per player (world-registered, ticked in `stepSimulation` after controllers, before combat):

- Scan: gather enabled interactables within `max(interactable.radius)` via the world list (list is small; no physics query needed — distance check against entity world positions), filter by: distance ≤ per-interactable radius, angle from the interactor's facing ≤ `coneDegrees` (default 70; facing = owner entity forward or camera forward via `set_useCameraFacing(true)` — third-person default), and LoS raycast (origin = owner pivot, static-world mask, `losMask` prop) unless `requireLineOfSight=false`.
- Best = max(score) with `score = (1 − dist/radius) + 0.5×(1 − angle/cone) + focus_priority`; hysteresis: current focus keeps a 10% bonus (no flicker between two chests).
- State: `focused` (Interactable3D|null), `focusChanged` one-shot, `interact()` → emits an interaction event `{interactor entity, interactable, kind}` into a world polled buffer if focused (returns true); `interactHeld(dt)` accumulation left game-side.

### 3.3 Highlight sugar

`Interactable3D.setFocusHighlight(enabled)` (default off): while focused, the interactor swaps the entity subtree's materials for emissive-nudged instances (`+0.35` emissive, warm white), restoring on blur — same bookkeeping pattern as plan 01's occluder fades (shared helper `g3d_material_override_begin/end` extracted once, used by both).

## 4. Implementation steps

1. Component + slot + world list + despawn/stale plumbing.
2. Interactor scan (distance/cone/priority + hysteresis) + focus flags; C unit tests (three candidates, expected winner; hysteresis hold).
3. LoS gating + camera-facing mode.
4. `interact()` + polled event buffer + wrapper class `InteractionEvent3D`.
5. Shared material-override helper + focus highlight.
6. runtime.def + audits + ADR + docs (`game3d.md` new §Interaction with a door + prompt HUD recipe).
7. Zia probe `g3d_test_game3d_interact_probe` (walk to door, focus gained, interact event, deterministic replay).

## 5. Public API changes (runtime.def)

```
RT_CLASS_BEGIN("Viper.Game3D.Interactable3D", Game3DInteractable3D, "obj", Game3DInteractableNew)  /* New(entity) */
    RT_PROP("Prompt","str",…) RT_PROP("Kind","i64",…) RT_PROP("Radius","f64",…)
    RT_PROP("Enabled","i1",…) RT_PROP("FocusPriority","f64",…)
    RT_METHOD("withPrompt","obj(obj,str)",…) RT_METHOD("withKind","obj(obj,i64)",…) RT_METHOD("withRadius","obj(obj,f64)",…)
    RT_METHOD("setFocusHighlight","void(obj,i1)",…)
RT_CLASS_END()
RT_CLASS_BEGIN("Viper.Game3D.Interactor3D", Game3DInteractor3D, "obj", Game3DInteractorNew)        /* New(world, ownerEntity) */
    RT_PROP("ConeDegrees","f64",…) RT_PROP("RequireLineOfSight","i1",…) RT_PROP("LosMask","i64",…)
    RT_PROP("UseCameraFacing","i1",…) RT_PROP("Focused","obj<Viper.Game3D.Interactable3D>",get)
    RT_METHOD("FocusChanged","i1(obj)",…) RT_METHOD("Interact","i1(obj)",…)
RT_CLASS_END()
```

Plus `Entity3D.attachInteractable`, `World3D.interactionEventCount/interactionEvent(i)` returning `InteractionEvent3D` (entity/interactable/kind getters). Leaves unique. New file → source-health; ADR `00xx-game3d-interaction.md`.

## 6. Tests

- **Selection (C unit):** near-center chest beats far chest and off-cone lever; hysteresis holds focus across a marginal score swap (fail-before: no API).
- **LoS:** interactable behind a wall is skipped; flag off ⇒ selected.
- **Events:** `interact()` with focus emits exactly one buffered event with correct kind; without focus returns false and emits none; buffer clears next step.
- **Highlight:** focus installs emissive instance on all subtree materials; blur restores original handles (pointer asserts); despawn while focused restores safely.
- **Stale:** despawned interactable drops from the list; `Focused` never returns a stale handle.
- **Camera-facing mode:** interactor with camera facing selects the chest the camera looks at even when the entity faces away.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits + leaf checks; determinism gate (ticks in stepSimulation); `-L slow`.

## 8. Risks & constraints

- **List scan is O(interactables)** per step — fine to hundreds; if a stress case appears, bucket by the existing scene spatial structure rather than physics broadphase.
- **Material-override helper** must be single-owner per entity (focus highlight + occluder fade colliding on one entity: last-writer wins with proper restore chaining — the shared helper enforces a small override stack).
- **Prompt strings are game/localization data** — the runtime stores what it's given; plan 25's localization keys pattern applies game-side.
- One interactor per world in v1 (co-op later); constructor traps on a second registration with a clear diagnostic.
