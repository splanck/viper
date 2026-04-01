# Viper.Game.Behavior

Composable AI presets for 2D game entities. Combines common patterns (patrol, chase, gravity, edge reverse, shoot, animation) into a single `Update()` call.

## API

### Behavior.New() -> Behavior
### AddPatrol(speed) — Walk back and forth at speed
### AddChase(speed, range) — Move toward target if within range (pixels)
### AddGravity(gravity, maxFall) — Apply gravity each frame
### AddEdgeReverse() — Reverse at platform edges
### AddWallReverse() — Reverse on wall collision
### AddShoot(cooldownMs) — Fire on cooldown; check ShootReady
### AddSineFloat(amplitude, speed) — Sine-wave vertical movement
### AddAnimLoop(frameCount, msPerFrame) — Auto-advance animation frames
### Update(entity, tilemap, targetX, targetY, dt) — Apply all behaviors
### Properties
- `ShootReady` — True when shoot cooldown expired (auto-clears)
- `AnimFrame` — Current animation frame index

## Example
```zia
var bhv = Behavior.New()
bhv.AddPatrol(100)
bhv.AddGravity(78, 1350)
bhv.AddEdgeReverse()
bhv.AddWallReverse()
bhv.AddAnimLoop(4, 120)

// Per frame (replaces 60+ lines of manual AI code):
bhv.Update(entity, tilemap, playerX, playerY, dt)
var frame = bhv.get_AnimFrame()
```
