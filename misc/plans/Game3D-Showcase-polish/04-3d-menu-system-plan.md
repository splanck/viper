# Ridgebound 3D Menu System Plan

Goal: add a polished, game-like title/menu front end to Ridgebound without
introducing external assets or dependencies.

The first screen should feel like the game itself, not a landing page or a
runtime showcase. It should use the existing world, terrain, beacons, lighting,
particles, and post-FX to create a 3D-looking interactive menu.

## Design Pillars

1. The menu is in-world.
   - The camera looks across the ridge basin toward a lit beacon site.
   - Menu choices are represented by 3D stone/metal pylons or beacon fragments.
   - The selected item emits light, particles, and a subtle audio cue.

2. The title is a first-viewport signal.
   - `RIDGEBOUND` appears large and immediately.
   - `THE FIVE BEACONS` appears as a smaller subtitle.
   - The background shows terrain/beacon content, not abstract gradients.

3. Controls are discoverable but not tutorial text-heavy.
   - Use clear menu labels.
   - Keep detailed controls in Options/Controls.
   - Do not fill the title screen with runtime feature copy.

4. The menu must be deterministic enough for smoke tests.
   - Use fixed seeds for any procedural particles/menu layout.
   - Avoid relying on external assets beyond the existing optional tree model.

## Proposed Screens

### Title Screen

Visible elements:

- 3D scene:
  - A ridge overlook.
  - One inactive beacon in the middle distance.
  - Four stone/metal menu pylons arranged in an arc.
  - Slow moving clouds/skybox and subtle water reflections if visible.
- Overlay:
  - Large title: `RIDGEBOUND`.
  - Subtitle: `THE FIVE BEACONS`.
  - Small version/backend text only in a corner or debug mode.

Menu options:

- `Start`
- `Options`
- `Controls`
- `Quit`

Optional later:

- `Continue` only if save-state exists. Do not add fake Continue.

### Pause Menu

Visible elements:

- Current gameplay world blurred/dimmed only through existing post-FX/overlay
  tools.
- A 3D beacon glyph or rotating artifact beside the options.

Options:

- `Resume`
- `Restart`
- `Options`
- `Controls`
- `Quit`

### Options Screen

Settings:

- Mouse invert Y.
- Mouse sensitivity.
- FOV preset:
  - `Focused`
  - `Standard`
  - `Wide`
- Graphics quality:
  - `Performance`
  - `Balanced`
  - `Cinematic`
- Fullscreen toggle.
- Post-FX toggle or Vignette toggle if needed for accessibility.

Use existing runtime APIs:

- `input.LookSensitivity`.
- `Canvas3D.SetQuality`.
- `Canvas3D.ToggleFullscreen`.
- Config-backed fields for FOV/invert Y.

Do not add persistent disk settings in the first pass unless a save/config API
already exists and is simple to use.

### Controls Screen

Show:

- Move: WASD / arrows.
- Look: mouse.
- Jump: Space.
- Sprint: Shift.
- Scan/link: E.
- View: V.
- Fullscreen: F11.
- Debug: Ctrl.
- Quit/pause: Esc.

Keep this as a menu page, not as default HUD clutter.

## Architecture

### New Modules

Add after rename:

- `menu3d.zia`
  - Owns menu state, selection, animations, input handling, and menu-specific
    draw calls.
- Optional `settings.zia`
  - Owns mutable runtime settings if options become more than a few fields.

Avoid adding a broad UI framework. This menu is bespoke and should stay close to
the game.

### Game State

Add a small state enum/int constants in `game.zia` or `config.zia`:

- `STATE_TITLE`
- `STATE_PLAYING`
- `STATE_PAUSED`
- `STATE_OPTIONS`
- `STATE_CONTROLS`
- `STATE_QUIT_CONFIRM` if needed.

Startup flow:

1. `setup` builds the world, terrain, sky, lights, assets, and menu.
2. Initial state is `STATE_TITLE`.
3. Player entity may exist but is hidden/frozen until Start.
4. `Start` transitions to gameplay:
   - Reset player if needed.
   - Switch camera from title-menu camera pose to gameplay follow camera.
   - Enable normal input/camera capture after the transition.

### Input Handling

Title/menu input:

- Up/Down or W/S changes selected option.
- Enter/Space confirms.
- Escape backs out or quits from title.
- Mouse hover/click optional later, not required in first pass.

Important:

- Do not capture mouse on the title menu.
- Capture mouse only when entering gameplay or when the player clicks to engage
  gameplay mode.
- Release mouse when pausing.

### Rendering Flow

Title render:

1. `world.BeginFrame()`.
2. Draw terrain/water/sky/forest/menu objects.
3. Draw menu-specific 3D objects:
   - Pylons.
   - Beacon title artifact.
   - Selection light/particles.
4. `world.DrawEffects()`.
5. `world.EndScene()`.
6. `Canvas3D.BeginOverlay(canvas)`.
7. Draw title and labels.
8. `Canvas3D.EndOverlay(canvas)`.

Use the same final-frame path as gameplay so post-FX and screenshots remain
consistent.

### 3D-Looking UI Without Text Meshes

The runtime does not currently provide text mesh or billboard text. Build the
3D feel with:

- Actual 3D pylon meshes under each menu option.
- 2D overlay text aligned with the menu layout.
- Drop shadows, duplicated offset text, and glow colors for a beveled look.
- Selection rings/frames drawn as 3D geometry.
- Dynamic lights and particles on the selected pylon.

Do not block on runtime additions like `DrawBillboardText`.

## Menu Art Direction

### Scene Composition

Camera:

- Low orbit looking slightly upward at a beacon ridge.
- Slow idle drift:
  - Yaw oscillation: small.
  - Height oscillation: very small.
  - No aggressive roll.

Foreground:

- One dark stone slab or broken arch framing the title.
- Menu pylons in a shallow arc.

Midground:

- A beacon tower or light column.
- Forest silhouettes.

Background:

- Mountains, skybox, fog.
- Day/night phase near golden hour or early twilight for strong rim light.

### Title Treatment

Use overlay text, but style it like a 3D game title:

- Draw title shadow 2 to 3 times with small offsets.
- Draw a darker extruded back layer.
- Draw main text in a bright warm/cyan tone.
- Add a subtle glow by drawing larger low-alpha text behind it if text alpha is
  supported, or draw translucent rectangles/lines behind the title.

Avoid huge paragraphs. The title screen should not explain the runtime.

### Selection Treatment

For selected menu item:

- Pylon material brightens.
- A point light appears above/inside the pylon.
- A thin vertical beam or ring pulses.
- Particles drift upward.
- The label brightens and moves a few pixels or scales if supported.

For unselected items:

- Dim, readable labels.
- Low static emissive strip or tiny beacon dot.

### Audio

Use existing audio system where possible:

- Soft tick on selection movement.
- Low beacon hum on title screen.
- Confirm stinger when starting.
- Back/cancel sound for Options/Controls return.

If a `menu_back` clip does not exist, synthesize one in the same style as the
existing generated audio.

## Implementation Batches

### Batch M0: State Skeleton

- Add state constants.
- Add title state as initial state.
- Ensure title screen renders world but does not update player movement.
- Ensure Esc quits from title and pauses from gameplay.

Validation:

- `viper check`.
- Title appears.
- Gameplay can still start through a temporary key if menu actions are not done.

### Batch M1: Menu3D Module

- Add `menu3d.zia`.
- Implement selection state and option list.
- Add update/draw methods:
  - `setup(world, canvas, assets, land, waterSky...)`
  - `update(dt, input) -> action`
  - `draw3D(canvas, camera, worldTime)`
  - `drawOverlay(canvas)`
- Keep action values simple integers:
  - `ACTION_NONE`
  - `ACTION_START`
  - `ACTION_OPTIONS`
  - `ACTION_CONTROLS`
  - `ACTION_QUIT`
  - `ACTION_BACK`

Validation:

- Keyboard navigation changes selection.
- Confirm returns correct action.
- No mouse capture on menu.

### Batch M2: Title Scene Art

- Add pylon meshes using existing box/cylinder/sphere meshes or generated mesh
  helpers.
- Add selected pylon light.
- Add title camera pose and idle drift.
- Add particles/hum if cheap.

Validation:

- Screenshot shows visible 3D menu geometry.
- No severe corner darkness.
- No UI overlap at 1280x720 and 960x540.

### Batch M3: Gameplay Transition

- On Start:
  - Fade/crossfade if available through existing post-FX/overlay path.
  - Reset player/camera.
  - Capture mouse only as gameplay begins or after click.
- On Pause:
  - Release mouse.
  - Draw pause menu over paused world.

Validation:

- Start enters normal gameplay.
- Pause/resume works.
- Quit exits cleanly.

### Batch M4: Options and Controls

- Add Options page.
- Add Controls page.
- Wire FOV preset and invert Y.
- Wire quality and fullscreen.

Validation:

- FOV preset changes horizontal FOV.
- Invert Y changes camera pitch direction.
- Fullscreen toggle still works.
- Quality toggle still works.

### Batch M5: Smoke Probe Coverage

- Add menu smoke path:
  - Render title.
  - Capture final frame.
  - Assert title/menu colors are present.
  - Assert nonblank 3D menu region.
- Add action smoke:
  - Synthetic key to Start.
  - Verify gameplay state after transition.

If synthetic menu input is awkward, add deterministic helper methods on the demo
class rather than depending on OS input.

## Layout and Responsiveness

Targets:

- 1280x720 default.
- 960x540 smoke.
- 640x360 low-resolution smoke if practical.

Rules:

- Title must not overlap menu options.
- Menu labels must fit within screen width.
- Minimap should not draw on the title screen unless intentionally designed.
- HUD controls text should not show on title.

## Acceptance Criteria

- First screen says `RIDGEBOUND`.
- It shows a real 3D scene and 3D menu objects.
- It has Start, Options, Controls, Quit.
- Start reaches gameplay without player flyaway.
- Pause/resume works.
- Mouse is not captured before gameplay.
- Smoke probe can validate title frame and gameplay frame.
- No external assets are required.

