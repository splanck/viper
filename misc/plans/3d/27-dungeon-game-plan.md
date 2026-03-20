# Dungeon of Viper — Steam-Quality 3D FPS Game

## Context

All 3D engine phases (A-F) are complete: Physics3D, Trigger3D, Transform3D, Path3D, Camera shake/follow, InstanceBatch3D, Terrain3D, NavMesh3D, AnimBlend3D, LOD, Decals, Sprite3D, Water3D, TextureAtlas, fog, shadows, gizmos, PostFX. 1338 tests pass. 17 demo files verified.

FBX assets verified: Knight (12 anims, 42 bones), Cleric (15 anims, 44 bones), Monk (11 anims, 44 bones) — all load correctly.

The user wants a **full game** — not a tech demo. Menu system, 3 playable levels, blood effects, death/respawn, score, the works. This is an iterative project: this plan covers the core game architecture and Level 1, with subsequent plans for Levels 2-3 and polish.

## Game Design

**Genre**: First-person dungeon crawler / arena shooter
**Camera**: First-person (WASD + mouse look)
**Core Loop**: Enter level → fight enemies → find key → reach exit → next level
**Win Condition**: Complete all 3 levels
**Lose Condition**: Health reaches 0 → death screen → retry level

### The Three Levels

| Level | Name | Theme | Enemies | Special |
|-------|------|-------|---------|---------|
| 1 | **The Crypt** | Dark stone corridors, torches | 5 Monks (melee patrol) | Introduction, learn controls |
| 2 | **The Armory** | Open rooms, physics crates | 8 Clerics (ranged spell) | Physics puzzles, key behind crates |
| 3 | **The Throne Room** | Lava arena, boss fight | 12 enemies + Knight Boss | Boss has 3 HP, arena hazards |

### Player Stats
- **Health**: 100, shown as bar on HUD
- **Ammo**: Unlimited (raycast "magic bolt" weapon)
- **Speed**: 5.0 (walk), 8.0 (sprint with Shift)
- **Damage**: 25 per shot (enemies have 50-100 HP)
- **Fire rate**: 3 shots/sec cooldown

### Enemy Behavior (Simple AI)
- **Idle**: Stand in place, play idle animation
- **Patrol**: Walk between 2-4 waypoints (Path3D), play walk animation
- **Chase**: When player within detection radius (15 units), move toward player
- **Attack**: When within attack range (2 units for melee, 10 for ranged), deal damage
- **Death**: Play death animation, spawn blood particles, fade out over 2 seconds

### Blood & Impact Effects
- **Blood spray**: Red particle burst (30 particles) at hit point on enemy, direction = shot direction
- **Blood decal**: Red splatter decal on floor/walls behind enemy (persists 10 seconds, fades)
- **Muzzle flash**: Small orange/yellow particle burst at camera + slight bloom flash
- **Camera shake**: 0.1 intensity, 0.15 duration on each shot
- **Hit marker**: Brief white flash on crosshair (0.1 sec)
- **Enemy hit flash**: Enemy material briefly goes red (0.1 sec)

---

## Project Structure

```
examples/games/dungeon/
├── main.zia                 Entry point — creates Game, calls run()
├── game.zia                 Game entity: state machine, level management
├── config.zia               All constants (colors, speeds, sizes, keybinds)
│
├── engine/
│   ├── renderer.zia         Canvas3D + PostFX + fog + lighting setup
│   ├── camera_ctrl.zia      FPS camera controller entity
│   ├── physics_mgr.zia      Physics world wrapper + static body registration
│   ├── hud.zia              HUD rendering: health bar, ammo, crosshair, score
│   └── menu.zia             Menu screens: title, pause, death, level complete
│
├── entities/
│   ├── player.zia           Player entity: health, shooting, damage, death
│   ├── enemy.zia            Enemy entity: AI states, pathfinding, animation
│   ├── enemy_spawner.zia    Spawns enemies from level data
│   ├── pickup.zia           Health/key/ammo pickup entities
│   └── door.zia             Door + trigger zone (requires key to open)
│
├── effects/
│   ├── blood.zia            Blood particles + decals system
│   ├── muzzle_flash.zia     Muzzle flash + screen flash
│   ├── torch.zia            Torch entity: mesh + light + fire particles
│   └── ambient.zia          Dust motes, fog wisps, embers
│
├── world/
│   ├── level_builder.zia    Builds walls, floors, doors from level data arrays
│   ├── level1_crypt.zia     Level 1 geometry + enemy placement data
│   ├── level2_armory.zia    Level 2 geometry + physics objects + enemies
│   ├── level3_throne.zia    Level 3 geometry + boss + lava
│   └── textures.zia         Procedural texture generators
```

**~25 files, ~8,000+ LOC total across all files**

---

## File Details

### main.zia (~20 LOC)
```zia
module main;
bind "./game";
func start() {
    var g = new Game();
    g.run();
}
```

### config.zia (~200 LOC)
All game constants in one place:
```
- STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_GAMEOVER, STATE_LEVELCOMPLETE
- PLAYER_SPEED, PLAYER_SPRINT, PLAYER_HEALTH, PLAYER_DAMAGE
- ENEMY_HEALTH_MONK, ENEMY_HEALTH_CLERIC, BOSS_HEALTH
- ENEMY_DETECT_RADIUS, ENEMY_ATTACK_RANGE, ENEMY_DAMAGE
- FIRE_COOLDOWN, CAMERA_SENSITIVITY
- Colors: COLOR_BLOOD, COLOR_MUZZLE, COLOR_HEALTH_GREEN, COLOR_HEALTH_RED
- HUD layout constants
- Level dimensions
```

### game.zia (~600 LOC)
Main orchestrator entity with state machine:
```
entity Game {
    hide state (menu/playing/paused/gameover/levelcomplete)
    hide currentLevel (1-3)
    hide score, kills, levelTime
    hide all subsystem references

    expose func run():
        Create canvas (1280x720, "Dungeon of Viper")
        Init all subsystems
        Main loop:
            Poll input
            Compute dt
            Switch on state:
                MENU → menu.update(), menu.draw()
                PLAYING → updateGame(dt), drawGame()
                PAUSED → drawGame() frozen, menu.drawPause()
                GAMEOVER → drawGame() frozen, menu.drawGameOver()
                LEVELCOMPLETE → drawGame() frozen, menu.drawLevelComplete()
            Flip

    hide func updateGame(dt):
        camera.update(dt)
        player.update(dt)
        enemies.updateAll(dt, player)
        physics.step(dt)
        blood.update(dt)
        ambient.update(dt)
        checkPlayerDeath()
        checkLevelComplete()

    hide func drawGame():
        renderer.beginFrame(cam)
        level.draw(canvas)
        enemies.drawAll(canvas, cam)
        blood.draw(canvas, cam)     // particles + decals
        torches.drawAll(canvas, cam)
        ambient.draw(canvas, cam)
        pickups.drawAll(canvas)
        doors.drawAll(canvas)
        renderer.endFrame()
        hud.draw(canvas, player, score, currentLevel)

    hide func handleShooting():
        If mouse left + cooldown elapsed:
            camera.shake(0.08, 0.12, 6.0)
            muzzleFlash.fire()
            Raycast from camera → find nearest enemy hit
            If hit:
                enemy.takeDamage(PLAYER_DAMAGE)
                blood.spawnAtHit(hitPoint, hitDirection)
                blood.spawnDecal(hitPoint, hitNormal)
                hud.flashHitMarker()

    hide func loadLevel(levelNum):
        Clear old level geometry/enemies/pickups
        Switch levelNum:
            1 → level1_crypt.build(...)
            2 → level2_armory.build(...)
            3 → level3_throne.build(...)
        Reset player health/position

    hide func checkPlayerDeath():
        If player.health <= 0:
            state = STATE_GAMEOVER

    hide func checkLevelComplete():
        If player inside exit trigger + has key:
            If currentLevel < 3:
                state = STATE_LEVELCOMPLETE
            Else:
                state = STATE_VICTORY
}
```

### engine/menu.zia (~500 LOC)
Full menu system with animated backgrounds:
```
entity MenuSystem {
    ─── Title Screen ───
    - "DUNGEON OF VIPER" large text centered
    - Animated torch particles in background
    - Slowly orbiting camera showing Level 1 preview
    - Menu options: [New Game] [Level Select] [Controls] [Quit]
    - Selection with W/S keys, confirm with Enter
    - Selected item highlighted (brighter color, slight pulse)

    ─── Pause Screen ───
    - Game frozen in background (still visible, darkened overlay)
    - "PAUSED" text
    - [Resume] [Restart Level] [Quit to Menu]

    ─── Death Screen ───
    - Screen tints red briefly, then fades
    - "YOU DIED" text with camera shake
    - Shows: kills, time survived
    - [Retry] [Quit to Menu]
    - Auto-retry after 3 seconds

    ─── Level Complete ───
    - "LEVEL COMPLETE" text with golden particles
    - Shows: time, kills, score breakdown
    - Time bonus: faster = more points
    - [Next Level] [Replay] [Quit to Menu]
    - Transition: brief fade to black

    ─── Level Select ───
    - 3 level cards showing name + preview
    - Locked levels show padlock until previous completed
    - Best time/score shown for completed levels

    ─── Controls Screen ───
    - WASD: Move  |  Mouse: Look  |  Click: Shoot
    - Shift: Sprint  |  E: Interact  |  ESC: Pause
    - F1: Debug overlay  |  [Back]
}
```

### engine/hud.zia (~300 LOC)
```
entity HUD {
    ─── Always visible (during gameplay) ───
    - Health bar: bottom-left, red bg + green fill (proportional to HP)
      - Flashes red when hit, pulses when low (<25%)
    - Crosshair: center, thin white cross
      - Briefly turns red on hit ("hit marker")
      - Expands slightly when moving (accuracy feedback)
    - Ammo: "∞" text bottom-right (unlimited ammo)
    - Level name: top-center, fades after 3 seconds

    ─── Boss fight (Level 3) ───
    - Boss health bar: top-center, large red bar with name

    ─── Score/stats ───
    - Kill counter: top-right
    - Timer: top-right below kills

    ─── Damage flash ───
    - When player takes damage: red vignette flash (DrawRect2D semi-transparent)
    - Fade out over 0.3 seconds
}
```

### entities/player.zia (~400 LOC)
```
entity Player {
    hide health, maxHealth
    hide invulnTimer     // brief invulnerability after hit
    hide hasKey          // for door unlocking
    hide shootCooldown
    hide killCount

    expose func update(dt, cam):
        Handle WASD via Camera3D.FPSUpdate
        Handle sprint (Shift)
        Handle shooting (left click + cooldown)
        Update invulnerability timer
        Clamp to level bounds

    expose func takeDamage(amount):
        If not invulnerable:
            health -= amount
            invulnTimer = 0.5
            camera.shake(0.15, 0.2, 5.0)  // hit feedback
            hud.flashDamage()

    expose func heal(amount):
        health = min(health + amount, maxHealth)

    expose func giveKey():
        hasKey = true
}
```

### entities/enemy.zia (~600 LOC)
```
entity Enemy {
    hide fbxAsset        // loaded FBX
    hide mesh, skeleton, animPlayer
    hide idleAnim, walkAnim, attackAnim, deathAnim
    hide health, maxHealth
    hide state           // IDLE, PATROL, CHASE, ATTACK, DYING, DEAD
    hide patrolPath      // Path3D with waypoints
    hide patrolT         // current position on path [0,1]
    hide position        // Vec3
    hide physBody        // Physics3D body
    hide hitFlashTimer   // flash red when hit
    hide deathTimer      // fade out after death
    hide attackCooldown

    expose func init(fbx, spawnPos, patrolPoints):
        Load mesh/skeleton/animations from FBX
        Create physics body (capsule)
        Build patrol path from waypoints

    expose func update(dt, playerPos):
        Switch state:
            IDLE:
                Play idle animation
                If playerDist < DETECT_RADIUS → state = CHASE
            PATROL:
                Move along patrolPath
                Play walk animation
                If playerDist < DETECT_RADIUS → state = CHASE
            CHASE:
                Move toward player (Vec3.MoveTowards)
                Play run animation (or walk at 2x speed)
                If playerDist < ATTACK_RANGE → state = ATTACK
                If playerDist > DETECT_RADIUS * 1.5 → state = PATROL
            ATTACK:
                Face player
                Play attack animation
                If attackCooldown <= 0:
                    Deal damage to player
                    attackCooldown = 1.0
            DYING:
                Play death animation (once)
                deathTimer -= dt
                Fade alpha
                If deathTimer <= 0 → state = DEAD

    expose func takeDamage(amount, hitDir):
        health -= amount
        hitFlashTimer = 0.1
        If health <= 0:
            state = DYING
            deathTimer = 2.0

    expose func draw(canvas, cam):
        If state != DEAD:
            If hitFlashTimer > 0:
                // Use red material temporarily
            DrawMeshSkinned(canvas, mesh, transform, material, animPlayer)
}

entity EnemyManager {
    hide enemies: List[Enemy]

    expose func spawnEnemy(fbx, pos, patrol):
        Create enemy, add to list

    expose func updateAll(dt, playerPos):
        For each alive enemy: update

    expose func drawAll(canvas, cam):
        For each alive enemy: draw

    expose func raycastCheck(origin, dir) -> hitEnemy, hitDist, hitPoint:
        Check ray against each enemy's physics body
        Return nearest hit
}
```

### effects/blood.zia (~300 LOC)
```
entity BloodSystem {
    hide splashParticles   // Particles3D for blood spray
    hide decals: List[Ptr] // active decals

    expose func init():
        Create particle system:
            Red/dark red color (0xCC0000 → 0x440000)
            Gravity: (0, -6, 0) (drips fall)
            Speed: 3-8
            Lifetime: 0.3-0.8
            Size: 0.06-0.02
            Alpha: 1.0 → 0.0

    expose func spawnAtHit(hitPoint, hitDir):
        Move particles emitter to hitPoint
        Set direction = hitDir with spread 0.8
        Burst(25)

    expose func spawnDecal(hitPoint, surfaceNormal):
        Create Decal3D at hitPoint
        Size: 0.3-0.6 (randomized)
        Texture: procedural blood splat (red circle with irregular edges)
        Lifetime: 10.0 seconds
        Add to decals list

    expose func update(dt):
        Update particles
        Update all decals, remove expired

    expose func draw(canvas, cam):
        Draw particles (between Begin/End)
        Draw all active decals
}
```

### effects/muzzle_flash.zia (~150 LOC)
```
entity MuzzleFlash {
    hide flashParticles    // orange/yellow burst
    hide screenFlashTimer  // brief white screen edge flash

    expose func fire(camPos, camForward):
        Position particles slightly in front of camera
        Burst(8) — bright orange particles, very short lifetime (0.05s)
        screenFlashTimer = 0.05

    expose func draw(canvas):
        If screenFlashTimer > 0:
            Draw thin white rectangles at screen edges (simulates flash)
}
```

### effects/torch.zia (~200 LOC)
```
entity Torch {
    hide meshCylinder      // torch body
    hide meshFlame         // small sphere at top (emissive)
    hide pointLight        // warm orange point light
    hide fireParticles     // fire particle system
    hide flickerSeed       // for deterministic flicker

    expose func init(x, y, z):
        Build torch geometry
        Create point light (orange, range 8.0)
        Create fire particles (orange→red, upward)

    expose func update(dt):
        Flicker light intensity (0.8-1.2 range, xorshift random)
        Update fire particles
}

entity TorchManager {
    hide torches: List[Torch]
    // Factory + batch update/draw
}
```

### world/level_builder.zia (~500 LOC)
```
entity LevelBuilder {
    // Builds level geometry from a grid-based map definition
    // Each level provides a 2D array of tile types:
    //   0=empty, 1=wall, 2=floor, 3=door, 4=spawn, 5=exit, 6=torch, 7=pickup

    expose func buildFromGrid(grid, physics_world):
        For each cell in grid:
            WALL → build wall box (1x3x1), add static physics body
            FLOOR → build floor plane
            DOOR → build door mesh + trigger zone
            TORCH → create Torch entity
            etc.

        Generate floor texture (procedural stone)
        Generate wall texture (procedural bricks)
}
```

### world/level1_crypt.zia (~300 LOC)
```
// Level 1: "The Crypt" — Dark corridors, 5 monks, find key, reach exit
// Grid: 20x20 cells, each cell = 3x3 world units

var grid = [
    "11111111111111111111",
    "14222222211111111111",  // 4=player spawn
    "11111221111111111111",
    "11111221112222222211",
    "11111221112111111211",
    "12222222112116112211",  // 6=torch positions
    "12111121112111112211",
    "12111121112111112211",
    "12111121112222222211",
    "12222221112111111111",
    "11111221112111111111",
    "11111221112222222211",
    "11111221111111111211",
    "12222222222222271211",  // 7=key pickup
    "12111111111111111211",
    "12111111111111111211",
    "12222222222222222211",
    "11111111111111132211",  // 3=door, 5=exit (after door)
    "11111111111111152211",
    "11111111111111111111",
]

enemy_spawns = [
    {type: "Monk", pos: (15, 0, 9), patrol: [(15,0,9), (15,0,15)]},
    {type: "Monk", pos: (6, 0, 6), patrol: [(6,0,6), (6,0,12)]},
    // ... 3 more
]
```

### world/level2_armory.zia (~300 LOC)
```
// Level 2: "The Armory" — More open, physics crates blocking paths
// Larger grid (25x25), more enemies, physics puzzle elements
// Player must knock crates out of the way to reach key
```

### world/level3_throne.zia (~400 LOC)
```
// Level 3: "The Throne Room" — Boss arena + minions
// Large open room with lava hazards around edges
// Boss (Knight) at center: 3x health, attack animation
// 8 minion enemies that respawn on timer
// Water3D lava planes with orange glow
// Heavy particles: embers, smoke
```

### world/textures.zia (~200 LOC)
```
Procedural texture generators:
- Stone floor: 64x64, gray checkerboard with noise variation
- Brick wall: 64x64, brown bricks with dark mortar lines
- Wood: 32x32, horizontal grain pattern
- Metal: 32x32, dark gray with subtle reflection dots
- Lava: 32x32, animated orange/red swirl
- Blood splat: 16x16, red circle with ragged edges
```

---

## Controls

| Key | Action |
|-----|--------|
| WASD | Move |
| Mouse | Look |
| Left Click | Shoot |
| Shift | Sprint |
| E | Interact (open doors, pick up items) |
| ESC | Pause menu |
| F1 | Toggle debug info (FPS, enemy count, physics bodies) |

---

## Implementation Phases

### Phase 1: Core Architecture (~3,000 LOC)
**Goal**: Playable Level 1 with basic enemies
- `main.zia`, `config.zia`, `game.zia` (state machine)
- `engine/renderer.zia` (canvas + postfx + fog)
- `engine/camera_ctrl.zia` (FPS controller)
- `engine/hud.zia` (health bar, crosshair, kill count)
- `entities/player.zia` (movement, shooting, health)
- `entities/enemy.zia` (patrol + chase + death, FBX animated)
- `effects/blood.zia` (particle spray + decals)
- `effects/muzzle_flash.zia`
- `world/level_builder.zia` + `world/level1_crypt.zia`
- `world/textures.zia`
**Deliverable**: Walk through Level 1, shoot monks, blood effects, die and retry

### Phase 2: Menu System + Level 2 (~2,500 LOC)
**Goal**: Full menu flow + second level
- `engine/menu.zia` (title, pause, death, level complete screens)
- `world/level2_armory.zia`
- `entities/pickup.zia` (health, key)
- `entities/door.zia` (locked door + trigger)
- `effects/torch.zia` + `effects/ambient.zia`
- Level transitions, score tracking
**Deliverable**: Title → Level 1 → Level Complete → Level 2

### Phase 3: Level 3 Boss + Polish (~2,500 LOC)
**Goal**: Complete game
- `world/level3_throne.zia` (lava arena + boss)
- Boss enemy behavior (special AI, 3 HP phases)
- Water3D lava planes
- Victory screen
- Level select (replay completed levels)
- Sound effects placeholder hooks
- Performance tuning
**Deliverable**: Full 3-level game start to finish

---

## Verification

1. `./build/viper run examples/games/dungeon/main.zia` — launches game
2. Title screen appears with menu options
3. New Game → Level 1 loads
4. WASD movement works, mouse look works
5. Click to shoot → muzzle flash + camera shake
6. Hit enemy → blood particles + blood decal + enemy flash red
7. Enemy dies → death animation + fade out
8. Kill all enemies, find key, reach exit → Level Complete screen
9. Level 2 loads with different layout and more enemies
10. Level 3 boss fight works
11. Death → retry works
12. Pause (ESC) → resume works
13. FPS stays above 20 in software renderer
14. All existing 1338 tests still pass

## Current Assets Available

| Asset | File | Animations |
|-------|------|------------|
| Knight (Boss) | `Knight.fbx` | Idle, Walk, Run, Jump, Roll, Death, 5 sword attacks |
| Cleric (Ranged) | `Cleric.fbx` | Idle, Walk, Run, Spell1/2, Staff attack, Death, Roll |
| Monk (Melee) | `Monk.fbx` | Idle, Walk, Run, Attack1/2, Death, Roll |
| Sword prop | `Sword.fbx` | Static mesh |
