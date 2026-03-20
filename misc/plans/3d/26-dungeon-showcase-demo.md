# Fix Zia `bind` for Runtime Classes — Then Build "Dungeon of Viper"

## BLOCKING ISSUE: `bind Viper.Graphics3D.Canvas3D` doesn't shorten class method calls

`bind Viper.Terminal` correctly makes `Say`, `SayInt` available as short names. But `bind Viper.Graphics3D.Canvas3D` does NOT make `Canvas3D.New(...)` available — you still need the full `Viper.Graphics3D.Canvas3D.New(...)`. This must be fixed first.

## Investigation needed:
1. How does `bind Viper.Terminal` resolve `Say` → `Viper.Terminal.Say`?
2. Why doesn't `bind Viper.Graphics3D.Canvas3D` resolve `Canvas3D.New` → `Viper.Graphics3D.Canvas3D.New`?
3. The fix should make `bind Viper.Graphics3D` expose `Canvas3D`, `Mesh3D`, `Camera3D` etc. as short names.

---

# Viper 3D Engine Showcase — "Dungeon of Viper"

## Context

All 18 phases of the 3D engine are complete, plus Phase A (Physics3D, Trigger3D, Vec3 helpers) and Phase B (fog, shadows, gizmos). 1335 tests pass. The user wants a **visually impressive, game-quality** showcase demo (5K+ LOC) that uses real FBX character models and looks like something you'd show at a conference. Not a tech demo with primitives — a polished game experience.

## Asset Strategy

**Quaternius CC0 Character Packs** — free, public domain, FBX format, compatible with our v7100-7700 loader.

Download from OpenGameArt.org:
1. **Ultimate Animated Character Pack** (50+ characters with idle/walk/run/attack/death anims)
   - URL: `https://opengameart.org/sites/default/files/ultimate_animated_character_pack_by_quaternius.zip`
   - 48.4 MB, CC0 license
2. **RPG Characters Pack** (6 fantasy characters: warrior, mage, archer, etc.)
   - URL: `https://opengameart.org/sites/default/files/rpg_characters_-_nov_2020.zip`
   - 12.9 MB, CC0 license
3. **Animated Knight Pack** (detailed knight with sword/shield anims)
   - URL: `https://quaternius.com/packs/knightcharacter.html`

Assets stored in `tests/runtime/assets/characters/` (gitignored or committed depending on size).

**Fallback**: If FBX loading fails for a particular model, the demo gracefully falls back to colored primitive meshes with the same animation system — ensuring the demo always runs.

## Design: "Dungeon of Viper"

A third-person dungeon exploration game where the player controls a knight character through a multi-room dungeon. Each room showcases different engine features while maintaining a cohesive fantasy aesthetic.

### Visual Polish Goals
- **Atmospheric lighting**: Warm torchlight point lights with orange/amber colors, cool ambient
- **Heavy fog**: Dark blue-gray fog creating dungeon atmosphere, visibility ~30 units
- **Post-processing**: Bloom on torches/emissive, vignette for tunnel vision, subtle color grading
- **Particle effects**: Torch flames, floating dust motes, magic sparkles, impact sparks
- **Animated NPCs**: FBX characters with idle/walk patrol animations
- **Physics objects**: Barrels, crates that tumble when player walks near
- **Procedural textures**: Checkerboard stone floors, gradient walls, noise-based surfaces
- **Day/night cycle**: Slowly shifting ambient light color over time

### File: `tests/runtime/demo_showcase.zia`

---

## Room Layout (Top-Down)

```
                    ┌─────────────┐
                    │   Room 5    │
                    │  Boss Arena │
                    │ (Particles  │
                    │  + Combat)  │
                    └──────┬──────┘
                           │ corridor
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   ┌────┴─────┐      ┌────┴─────┐      ┌────┴─────┐
   │  Room 3  │      │  Room 4  │      │  Room 6  │
   │ Treasure │──────│  Gallery │──────│  Outdoor │
   │  Vault   │      │ (Render) │      │  Vista   │
   └────┬─────┘      └────┬─────┘      └──────────┘
        │                  │
        └────────┬─────────┘
                 │ corridor
            ┌────┴─────┐
            │  Room 2  │
            │ Armory   │
            │(Physics) │
            └────┬─────┘
                 │ corridor
            ┌────┴─────┐
            │  Room 1  │
            │  Entry   │
            │  Hall    │
            └──────────┘
              Player spawns here
```

Each room is ~30x30 units. Rooms at Y=0 ground level. Corridors are 4-unit-wide passages.

---

## Structure (~5,500 LOC)

### 1. Utility Functions (~500 LOC)

```
func create_stone_texture(size: Integer) -> Ptr
  - Procedural checkerboard with noise variation
  - Dark gray stones with slight color variation per cell

func create_wall_texture(size: Integer) -> Ptr
  - Vertical brick pattern with mortar lines

func create_wood_texture(size: Integer) -> Ptr
  - Horizontal grain pattern in brown tones

func create_lava_texture(size: Integer) -> Ptr
  - Orange/red swirling pattern for boss room

func create_skybox_faces(size: Integer) -> 6 Pixels
  - Top: dark blue-black
  - Sides: gradient from dark blue to black
  - Bottom: dark gray

func build_room_floor(cx, cz, size) -> mesh, xform, material
func build_room_walls(cx, cz, size, height, openings[]) -> array of wall segments
func build_corridor(from_room, to_room) -> floor + walls
func build_torch(x, y, z) -> mesh + point light + particle system
func build_barrel(x, y, z, physics_world) -> mesh + physics body
func build_crate(x, y, z, physics_world) -> mesh + physics body
func build_column(x, y, z, height) -> mesh + xform

func lerp(a, b, t), clamp(v, lo, hi), smoothstep(a, b, t)
func random_range(lo, hi) -> pseudo-random (deterministic seed from position)
```

### 2. Room 1: Entry Hall (~600 LOC)

**Purpose**: First impression — dramatic lighting, atmosphere, introduce controls.

```
setup_entry_hall():
  - Large stone floor (textured checkerboard)
  - 4 walls with 2 arched openings (to Room 2 and corridor)
  - 8 decorative stone columns with capitals
  - 6 wall-mounted torches (point lights + fire particles)
  - Central raised platform with rotating crystal
    - Crystal: sphere with emissive material, pulsing glow
    - Emissive color cycles through hue wheel
  - Fog: near=5, far=35, dark blue-gray
  - Skybox: dark gradient
  - PostFX: bloom(0.6, 0.4, 3), vignette(0.5, 0.3), colorgrade(1.1, 1.0, 1.05)
  - Welcome text on HUD: "Dungeon of Viper — WASD to move, Mouse to look"

update_entry_hall(time):
  - Rotate crystal pedestal
  - Pulse emissive intensity (sin wave)
  - Flicker torch lights (subtle intensity variation)
  - Update fire particles

draw_entry_hall(canvas, cam):
  - Draw floor, walls, columns, crystal
  - Draw torch meshes
  - Particles3D.Draw for each torch fire
```

### 3. Room 2: Armory — Physics Playground (~900 LOC)

**Purpose**: Physics3D showcase — interactive objects with collision.

```
setup_armory(physics_world):
  - Stone floor + walls (3 openings: to Room 1, Room 3, Room 4)
  - 12 wooden barrels (cylinder meshes + sphere physics bodies)
    - Arranged in groups: triangle stack of 6, row of 4, scattered 2
    - Brown wood material with shininess
  - 8 wooden crates (box meshes + AABB physics bodies)
    - Stacked pyramid: 4-3-1 formation
    - Different sizes (0.8-1.2 units)
  - 3 ramps (angled box meshes + static AABB bodies)
  - Collision layer demo:
    - Red barrels on layer 1, blue barrels on layer 2
    - Red only collides with red, blue only with blue
    - They pass through each other!
  - 4 trigger zones at room corners:
    - Enter trigger: burst of sparkle particles
    - Exit trigger: reset nearby objects
  - Weapon rack decoration (cylinders arranged as swords)
  - 4 torches on walls
  - Debug gizmos (toggled with F3):
    - Wireframe AABBs around all physics bodies (green)
    - Wireframe spheres around triggers (yellow)
    - Axis gizmo at room center

update_armory(dt, physics_world, triggers):
  - physics_world.Step(dt)
  - Sync mesh transforms from body positions
  - trigger.Update(physics_world) for each trigger
  - Check enter/exit counts, spawn particle bursts
  - Respawn bodies that fall below Y=-10

draw_armory(canvas, cam):
  - Draw all barrel/crate meshes at physics positions
  - Draw static ramps, weapon racks, torches
  - If gizmos_on: DrawAABBWire, DrawSphereWire for each body/trigger
  - DrawAxis at (0, 0, armory_z)
  - Particles for torches + trigger bursts
```

### 4. Room 3: Treasure Vault — Animation Showcase (~900 LOC)

**Purpose**: Skeletal animation, morph targets, scene graph, particles.

```
setup_treasure_vault():
  ─── FBX Characters (or fallback primitives) ───
  - Load "Knight.fbx" → extract mesh, skeleton, animations
  - NPC 1: Knight with idle animation (standing guard at door)
  - NPC 2: Knight with walk animation (patrolling back and forth)
  - NPC 3: Mage/Archer with idle (if available from pack)
  - Each NPC has AnimPlayer3D playing looped animation

  ─── Scene Graph Solar System ───
  - Floating "orrery" above treasure pile
  - Root node at room center, Y=4
  - Sun: emissive gold sphere (0.5 radius)
  - Planet 1: blue sphere (0.2), orbiting at 2.0 radius
    - Moon: tiny white sphere (0.08), orbiting planet at 0.5 radius
  - Planet 2: red sphere (0.15), orbiting at 3.5 radius
    - Ring: flat cylinder (0.01 height, 0.4 radius), tilted 45°
  - Planet 3: green sphere (0.25), orbiting at 5.0 radius
    - 2 moons
  - All nodes use Scene3D for automatic transform hierarchy

  ─── Morph Target Gem ───
  - Diamond shape (custom mesh, 8 vertices)
  - 3 morph shapes: "pulse" (expand), "spike" (elongate top), "twist"
  - Weights animated with sine waves at different frequencies
  - Emissive purple material

  ─── Particle Effects ───
  - Treasure sparkles: gold particles rising from floor
  - Magic circle: ring of blue particles around orrery
  - Torch flames: 4 wall torches

  ─── Treasure Pile ───
  - Multiple small golden spheres and boxes (treasure coins/ingots)
  - Emissive gold material

update_treasure_vault(time, dt):
  - AnimPlayer3D.Update for each NPC
  - NPC patrol: lerp position back and forth
  - Rotate scene graph nodes (planet orbits)
  - Cycle morph target weights
  - Update all particle systems

draw_treasure_vault(canvas, cam):
  - DrawMeshSkinned for each NPC
  - Scene3D.Draw for orrery
  - DrawMeshMorphed for gem
  - Draw treasure pile meshes
  - Particles3D.Draw for sparkles, magic, torches
```

### 5. Room 4: Gallery — Rendering Lab (~800 LOC)

**Purpose**: RTT, transparency, materials, custom meshes.

```
setup_gallery():
  ─── Security Camera Monitor ───
  - RenderTarget3D (256x256)
  - Overhead camera looking down at Entry Hall
  - Display: flat plane (2x2) on wall with RT texture as material
  - Updates each frame: renders Entry Hall from above

  ─── Transparency Showcase ───
  - 5 stained glass panels (thin box meshes)
    - Colors: red(0.3α), blue(0.25α), green(0.35α), amber(0.2α), purple(0.4α)
    - Behind each: a differently colored sphere (visible through glass)
  - SetBackfaceCull(false) for glass, true after

  ─── Material Showcase Wall ───
  - 8 spheres in a row (X spacing 1.5)
  - Shininess: 2, 4, 8, 16, 32, 64, 128, 256
  - All same color but different specular response
  - Below: 8 spheres with gradient colors (red→violet rainbow)
  - 1 unlit sphere (comparison, shows raw color without lighting)

  ─── Emissive Objects ───
  - 3 crystal pillars (cylinders with emissive materials)
  - Colors: cyan, magenta, amber
  - Pulsing emissive intensity (staggered sine waves)
  - Emissive floor strip: thin plane glowing white, guiding path

  ─── Custom Mesh: Procedural Torus ───
  - Built vertex-by-vertex using AddVertex/AddTriangle
  - Major radius 1.0, minor radius 0.3
  - ~200 vertices, ~400 triangles
  - Slowly rotating, gold material

  ─── Custom Mesh: Star Shape ───
  - 10-pointed star extruded into 3D
  - Built procedurally
  - Metallic silver material

update_gallery(time):
  - Rotate overhead camera (slow orbit)
  - Pulse emissive intensities
  - Rotate torus and star

draw_gallery(canvas, cam):
  - Security camera pass: SetRenderTarget → draw entry hall → ResetRenderTarget → ApplyAsTexture
  - Draw monitor quad with texture
  - BackfaceCull off → draw glass panels → BackfaceCull on
  - Draw material spheres, emissive crystals
  - Draw custom meshes
```

### 6. Room 5: Boss Arena — Combat & Particles (~700 LOC)

**Purpose**: Raycasting, combat, heavy particles, triggers, HUD.

```
setup_boss_arena():
  - Large circular room (octagonal walls)
  - Lava floor (custom red/orange emissive material, animated UV offset effect)
  - Stone walkway (ring of floor segments above lava)
  - Boss: large FBX character (2x scale) with idle animation
    - If no FBX: large red sphere with spikes (morph target)
  - 6 target orbs floating in circle (move up/down on sine waves)
  - Heavy particle effects:
    - Lava bubbles (orange, rising from floor)
    - Smoke (dark, rising along walls)
    - Magic sparks around boss (purple, orbiting)
    - Impact sparks (burst on hit)
  - Trigger zone: entering room activates "combat mode"
    - Changes HUD to show health bar + score
    - Increases ambient to show room clearly
  - Score: +10 per orb hit, displayed on HUD

update_boss_arena(time, dt, cam):
  - Move target orbs (sine wave oscillation)
  - Check mouse click: raycast against each orb
  - On hit: burst impact particles, respawn orb at new position
  - Update boss idle animation
  - Update all particle systems
  - Trigger zone enter/exit

draw_boss_arena(canvas, cam):
  - Draw walkway, walls
  - Draw lava floor (emissive)
  - Draw boss character (skinned or morph)
  - Draw target orbs
  - All particles
  - DrawDebugRay showing last shot direction (if gizmos on)
  - HUD: health bar, score, crosshair
```

### 7. Room 6: Outdoor Vista (~500 LOC)

**Purpose**: Large-scale rendering, distance fog, skybox, nature.

```
setup_outdoor():
  - Large ground plane (50x50, green-brown texture)
  - Mountain range: 5 large pyramids (custom mesh, gray material)
  - Forest: 20 trees (cylinder trunk + sphere canopy, various sizes)
  - Lake: transparent blue plane (alpha 0.3) with slight Y offset
  - Ruins: scattered broken columns and walls
  - Heavy fog: near=10, far=45, matching sky color (light blue-gray)
  - Bright directional light (sun, warm white)
  - Particle effects:
    - Campfire near ruins (fire + smoke)
    - Fireflies around lake (green sparkle, slow, large spread)
    - Falling leaves (brown, slow gravity)
  - Day/night cycle: slowly rotate sun direction, shift fog/ambient color

update_outdoor(time, dt):
  - Rotate sun direction (1 full cycle per ~120 seconds)
  - Update ambient based on sun height
  - Update fog color to match sky
  - Update all particles
  - Sway trees slightly (rotate canopy nodes)

draw_outdoor(canvas, cam):
  - Draw terrain, mountains, trees, lake, ruins
  - All particles
  - Sky gradient via fog blending
```

### 8. HUD System (~300 LOC)

```
draw_hud(canvas, zone_name, fps, score, health, show_controls):
  - Top-left: zone name text (DrawText2D)
  - Top-right: FPS counter
  - Center: crosshair (DrawCrosshair or DrawRect2D cross)
  - Bottom-left: controls help (toggled)
  - If in boss room:
    - Health bar (DrawRect2D: red bg + green fill)
    - Score display
  - Bottom-right: minimap concept (small DrawRect2D squares for room layout)
```

### 9. Player Controller (~400 LOC)

```
- FPS camera with mouse capture
- WASD movement via Camera3D.FPSUpdate
- Character3D physics body for collision with walls
- World3D.Step() syncs player position
- Gravity + ground detection
- Height lock (maintain eye_height above ground)
- Speed: walk=5.0, sprint=8.0 (hold Shift)
- Mouse sensitivity: 0.15
- Clamp pitch to ±85°
```

### 10. Main Game Loop (~400 LOC)

```
func start():
  1. Create canvas (1280x720, "Dungeon of Viper")
  2. Create camera (70 FOV)
  3. Download/verify FBX assets (graceful fallback)
  4. Create physics world
  5. Call all setup_*() functions
  6. Init FPS camera, capture mouse
  7. Main loop:
     a. Poll input
     b. Compute dt (clamped to 50ms)
     c. Handle toggles: F1=wireframe, F2=fog, F3=gizmos, F4=postfx, F5=cycle room
     d. FPS camera update (WASD + mouse)
     e. Determine current room from player XZ position
     f. Update current room + adjacent rooms
     g. Clear canvas (dark)
     h. Begin(cam)
     i. Draw all visible rooms (nearby ones)
     j. End
     k. Draw HUD overlay
     l. Flip
  8. On exit: release mouse
```

### Keyboard Controls

| Key | Action |
|-----|--------|
| WASD | Move |
| Mouse | Look |
| Left Click | Shoot (boss room) |
| F1 | Toggle wireframe |
| F2 | Toggle fog on/off |
| F3 | Toggle debug gizmos |
| F4 | Toggle post-processing |
| F5 | Teleport to next room |
| R | Reset physics objects |
| ESC | Quit |

---

## Asset Download Steps (Pre-Implementation)

1. Create `tests/runtime/assets/characters/` directory
2. Download Quaternius Ultimate Character Pack from OpenGameArt
3. Extract FBX files for: Knight, Mage, Archer (or similar)
4. Verify each loads with our FBX loader (check version, bone count, mesh extraction)
5. If any fail: document issue, use primitive fallback for that character

## Implementation Order

1. Download and verify FBX assets
2. Utility functions (textures, room builders)
3. Main loop + player controller + HUD
4. Room 1 (Entry Hall) — first visual test
5. Room 2 (Armory/Physics)
6. Room 3 (Treasure/Animation)
7. Room 4 (Gallery/Rendering)
8. Room 5 (Boss Arena)
9. Room 6 (Outdoor Vista)
10. Polish pass: lighting tweaks, particle tuning, fog distances

## Verification

1. `./build/viper run tests/runtime/demo_showcase.zia` — launches game
2. Walk through all 6 rooms checking each feature
3. F-key toggles all work
4. Physics objects respond to world step
5. FBX characters animate correctly
6. Particles render properly
7. Render-to-texture shows security camera view
8. Glass panels are transparent
9. No crashes on room transitions
10. Runs at reasonable framerate (~30+ FPS in software renderer)

## Feature Coverage Checklist (Every 3D API Used)

Canvas3D, Mesh3D (all primitives + custom + FBX), Camera3D (orbit + FPS), Material3D (color, texture, alpha, emissive, shininess, unlit), Light3D (directional, point, ambient), Vec3 (all ops including new Phase A helpers), Mat4, Quat, Physics3DWorld, Physics3DBody (AABB, sphere), Character3D, Trigger3D, Scene3D + SceneNode3D, Skeleton3D + Animation3D + AnimPlayer3D, MorphTarget3D, Particles3D, PostFX3D, RenderTarget3D, CubeMap3D/Skybox, Ray3D, Fog, Shadows, Debug Gizmos, HUD overlay, Input (keyboard + mouse), Pixels (procedural textures), BackfaceCull toggle, Wireframe toggle, DrawLine3D, DrawPoint3D
