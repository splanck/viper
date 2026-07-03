# Runtime Fixes and API Additions Plan

This file separates required runtime fixes from optional runtime ergonomics.
Do the correctness fixes first. Treat public API additions as separate commits
with ADRs where required by repo policy.

## Runtime Workstream R1: Fix CharacterController3D Gravity Sign

### Problem

`CharacterController3D.Update` currently applies gravity upward:

- Current airborne path adds `gravity * dt` to vertical velocity.
- `Character3D.Move` interprets positive Y velocity as upward movement.
- A controller that starts just above the floor accelerates away from it.

### Desired Semantics

Use one consistent convention:

- Positive Y velocity means upward movement.
- `JumpSpeed` is a positive upward launch velocity.
- `Gravity` is a positive magnitude that accelerates downward.
- Ground stickiness is a small negative velocity while grounded.

### Implementation Shape

In `src/runtime/graphics/3d/rt_game3d_controllers.c`:

- Keep `Gravity` as a positive magnitude on the public property.
- In the airborne branch, change velocity integration to subtract gravity:
  `vertical_velocity -= gravity * dt`.
- Preserve clamping to `[-100.0, 100.0]`.
- Keep jump assignment positive: `vertical_velocity = jump_speed`.
- Keep or revise grounded stickiness as a small negative value, because
  negative Y is downward.
- After `rt_character3d_move`, if the character is grounded and vertical velocity
  is negative, keep it at the ground-stickiness value.

Potential pseudocode:

```c
if (grounded) {
    if (controller->vertical_velocity < 0.0)
        controller->vertical_velocity = -0.5;
    if (jump_pressed)
        controller->vertical_velocity = jump_speed;
} else {
    controller->vertical_velocity -= gravity * dt;
    controller->vertical_velocity = game3d_clamp(controller->vertical_velocity, -100.0, 100.0);
}
```

### Tests

Add C++ unit coverage in the Game3D/physics test area:

- A character starts slightly above a static floor.
- With no input and a positive gravity value, after several updates:
  - Y decreases from the initial airborne value.
  - The character eventually becomes grounded.
  - It does not rise above its initial Y.

Add a Zia runtime probe if practical:

- Use `Game3D.World3D.New`.
- Spawn a static AABB floor.
- Spawn a `CharacterController3D`.
- Run several frames with no input.
- Assert `afterY <= beforeY` until grounded.

### Compatibility

This is a bug fix. It changes behavior only where behavior was physically
incorrect. Existing content depending on upward gravity in `CharacterController3D`
would be relying on a bug.

### Acceptance Criteria

- Controller with no input settles downward onto a floor.
- Jump still moves upward.
- Existing first-person controller movement probe still passes.
- Ridgebound no longer launches at startup.

## Runtime Workstream R2: Separate Character Planar Movement From Free-Fly Movement

### Problem

`Input3D.MoveAxis` is a general 3D axis:

- X: strafe.
- Z: forward/back.
- Y: Space positive, Shift/Ctrl negative.

That is useful for free-fly cameras. It is wrong for a walking character
controller where:

- Space is jump, not continuous vertical motion.
- Shift is sprint, not descent.
- Normalizing X/Y/Z together reduces horizontal movement while sprinting.

### Minimal Internal Fix

Add an internal helper in the Game3D runtime:

- `game3d_input_planar_move_axis_components(input, &x, &z)`, or
- `game3d_input_move_axis_components_planar(input, &x, &z)`.

It should:

- Read W/A/S/D and arrow keys only.
- Normalize only the X/Z vector.
- Ignore Space, Shift, and Ctrl.

Change `rt_game3d_character_controller_update` to use the planar helper for
horizontal velocity.

Keep jump detection separate:

- Continue using `rt_game3d_input_pressed(input_obj, rt_game3d_key_space())`.
- Do not use `move_y > 0.5` for character jump after the planar helper lands,
  unless compatibility demands it. If compatibility is retained, compute it
  from raw key state, not normalized `MoveAxis`.

### Optional Public API Addition

Consider exposing a public planar axis:

- `Viper.Game3D.Input3D.PlanarMoveAxis() -> Vec3` with Y set to 0, or
- `Viper.Game3D.Input3D.HorizontalMoveAxis() -> Vec2`.

Recommendation: expose `PlanarMoveAxis() -> Vec3`.

Reasons:

- Same mental shape as `MoveAxis`.
- Lets gameplay code keep using Vec3 X/Z math.
- Avoids introducing Vec2 handling into every walking-controller script.

### ADR Requirement

The internal helper does not need an ADR. A new public method in `runtime.def`
does affect the runtime surface and should get an ADR or be folded into an ADR
covering all Game3D runtime polish APIs.

### Tests

- Unit test W + Shift:
  - Existing `MoveAxis` can continue returning normalized 3D axis.
  - New planar helper/API returns full forward magnitude.
- Character controller W + Shift:
  - Horizontal speed uses sprint multiplier without a hidden `sqrt(1/2)`
    reduction.

## Runtime Workstream R3: Add Symmetric Horizontal FOV Accessors

### Problem

The runtime already has:

- `Camera3D.NewHorizontalFov`.
- `Camera3D.SetHorizontalFov`.

But `Camera3D.Fov` remains vertical, and there is no symmetric getter or
property. Demo authors can easily lose track of which FOV they are tuning.
3DScene demonstrates the failure mode by passing `60` to the vertical-FOV
constructor and getting a much wider horizontal view.

### Recommended API

Add:

- `Camera3D.get_HorizontalFov -> f64`
- `Camera3D.set_HorizontalFov -> void(f64)`

Bind as:

- `RT_PROP("HorizontalFov", "f64", Camera3DGetHorizontalFov, Camera3DSetHorizontalFov)`

Behavior:

- Getter computes `2 * atan(tan(vertical/2) * aspect)`.
- Setter delegates to the existing horizontal-to-vertical conversion.
- Orthographic cameras return `0.0` and ignore set, matching `Fov`.
- The value is based on the camera's current stored aspect.

Optional but useful:

- `Camera3D.GetHorizontalFovForAspect(aspect: f64) -> f64`
- `Camera3D.GetVerticalFovForAspect(horizontalFov: f64, aspect: f64) -> f64`

Recommendation: start with the property only. Add aspect-specific methods only
if tests or docs show a real need.

### ADR Requirement

Required. This changes the public runtime surface and should be documented as a
small additive Camera3D ergonomics ADR.

### Docs

Update:

- `docs/graphics3d-guide.md`
- `docs/viperlib/graphics/rendering3d.md`
- `docs/viperlib/graphics/game3d.md` if the Game3D world constructors are
  discussed there.

Docs must explicitly state:

- `Fov` is vertical.
- `HorizontalFov` is horizontal.
- `NewHorizontalFov` and `WithHorizontalCamera` are preferred for player-facing
  game cameras on widescreen windows.

### Tests

Add numeric tests:

- 72 horizontal at 16:9 gives about 44.4577 vertical.
- Round-trip set/get horizontal returns the original value within tolerance.
- 60 vertical at 16:9 gives about 91.4928 horizontal.

## Runtime Workstream R4: Improve Vignette Ergonomics

### Problem

`PostFX3D.AddVignette(radius, softness)` is powerful but easy to overtune.
The 3DScene demo uses `0.84, 0.10`, which turns corners fully black. The API is
doing what it says, but the parameter names do not communicate how quickly hard
corners can appear.

### Option A: Documentation-Only Fix

Update docs to include safe ranges:

- Subtle gameplay vignette: radius `0.96`, softness `0.24` or wider.
- Dramatic cutscene vignette: radius `0.85`, softness `0.20`.
- Avoid radius + softness below `1.0` for gameplay if black corners are not
  desired.

This option requires no ADR.

### Option B: Add a Preset Helper

Add:

- `PostFX3D.AddSubtleVignette()`

Implementation:

- Appends `AddVignette(0.96, 0.28)`.

Pros:

- Extremely simple.
- Matches existing `Game3D.PostFX.Cinematic` intent.
- Reduces repeated demo mistakes.

Cons:

- Adds a narrow public API.
- Still requires an ADR for runtime surface expansion.

### Option C: Add Strength-Based Vignette

Add:

- `PostFX3D.AddVignetteStrength(strength: f64, softness: f64)`

Potential mapping:

- `strength = 0.0` means no effect.
- `strength = 1.0` means strong but not full-black gameplay effect.
- Internally map to radius/softness with a floor that avoids full black unless
  strength exceeds a deliberate threshold.

Pros:

- Better user-facing control.

Cons:

- More API design surface.
- Harder to explain and test.
- Could conflict with existing radius/softness mental model.

Recommendation: do Option A immediately, and Option B only if the ADR/API pass
is already happening for HorizontalFov.

## Runtime Workstream R5: Strengthen PostFX Presets

Existing `Game3D.PostFX.Cinematic` already tries to keep vignette subtle. Make
sure demo code uses presets or matches preset values unless it has a specific
reason not to.

Plan:

- Review `src/runtime/graphics/3d/rt_game3d_presets.c` vignette values.
- Keep cinematic vignette subtle enough for playable demos.
- Add/keep a test similar to the existing "subtle vignette" test in
  `test_rt_game3d`.
- Avoid changing existing `AddVignette` behavior directly. Existing callers may
  intentionally use hard vignette for horror/cutscene effects.

## Runtime Workstream R6: Optional 3D Menu Helpers

The Ridgebound menu can be built with existing APIs, but runtime additions could
make future 3D menus cleaner.

Possible additions:

- `Camera3D.WorldToScreen(point, width, height) -> Vec2/Option`
- `Canvas3D.DrawBillboardText(text, position, size, color)`
- `Canvas3D.DrawBillboardImage(pixels, position, width, height)`

Recommendation: do not block Ridgebound on these. Use existing 3D meshes,
lights, particles, and final overlay text first. Revisit these helpers only if
the menu implementation becomes awkward or duplicated.

ADR required for any public addition.

## Runtime Commit Staging

Commit/runtime batch recommendation:

1. `fix(runtime): correct Game3D character gravity`
   - R1 only.
   - No ADR needed if no public surface changes.
2. `fix(runtime): use planar movement for character controllers`
   - Internal helper only.
   - No ADR needed if no public surface changes.
3. `feat(runtime): add Camera3D horizontal FOV property`
   - ADR required.
   - Runtime docs and tests included.
4. Optional `feat(runtime): add subtle vignette preset`
   - ADR required if public.
   - Docs and tests included.

Keep runtime bug fixes separate from demo polish so regressions are easier to
bisect.

