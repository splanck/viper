---
status: active
audience: public
last-verified: 2026-04-09
---

# Game Utilities
> Game development helpers: physics, animation, timers, and state machines.

**Part of [Viper Runtime Library](../README.md)**

---

## Contents

| File | Contents |
|------|----------|
| [Game Loop Framework](gameloop.md) | GameBase, IScene — eliminates game loop boilerplate |
| [Core Utilities](core.md) | Timer (frame + ms modes), StateMachine, SmoothValue, ObjectPool |
| [Physics & Collision](physics.md) | Grid2D, CollisionRect, Collision, Physics2D, Quadtree |
| [Animation & Movement](animation.md) | Tween, SpriteAnimation, AnimStateMachine, SpriteSheet, PathFollower, ButtonGroup |
| [Visual Effects](effects.md) | ParticleEmitter, ScreenFX, Lighting2D |
| [Platformer](../game.md#vipergameplatformercontroller) | PlatformerController — jump buffer, coyote time, acceleration curves |
| [Achievement Tracking](../game.md#vipergameachievementtracker) | AchievementTracker — bitmask unlocks, stat counters, notification popup |
| [Text Reveal](../game.md#vipergametypewriter) | Typewriter — character-by-character text animation |
| [Entity System](entity.md) | Entity — 2D game object with position, velocity, HP, tilemap collision |
| [Behavior AI](behavior.md) | Behavior — composable AI presets (patrol, chase, gravity, shoot) |
| [Raycast](raycast.md) | Raycast2D — line-of-sight and line collision queries |
| [Level Data](leveldata.md) | LevelData — JSON level loader with tilemap and object spawns |
| [Scene Manager](scenemanager.md) | SceneManager — multi-scene transitions and lifecycle |
| [Config](config.md) | Config — JSON configuration file loader |
| [UI Menus](ui-menu.md) | MenuList, GameButton — game menu widgets |
| [Debug Overlay](debug.md) | DebugOverlay — FPS, dt, custom watch variables |
| [Persistence](persistence.md) | SaveData — cross-platform key-value save/load |
| [UI Widgets](ui.md) | Label, Bar, Panel, NineSlice — in-game HUD widgets |
| [Pathfinding](pathfinding.md) | A* grid pathfinding for AI navigation |

## See Also

- [Time & Timing](../time.md) - `Timer`, `Countdown` for frame-based and wall-clock timing
- [Graphics](../graphics/README.md) - `Canvas`, `Sprite` for rendering
- [Input](../input.md) - `Keyboard`, `Mouse`, `Pad` for input handling
- [GUI](../gui/README.md) - `Button`, `RadioButton` for GUI widgets

