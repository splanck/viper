# Viper.Game.SceneManager

Multi-scene management with named scenes, switching, and timed transitions.

## API

### SceneManager.New() -> SceneManager
### Add(name) — Register a named scene
### Switch(name) — Instant switch to scene
### SwitchTransition(name, durationMs) — Timed transition
### Update(dt) — Call once per frame
### IsScene(name) -> Boolean — Check if current scene matches
### Properties
- `Current` — Current scene name
- `Previous` — Previous scene name
- `JustEntered` — True on first frame of new scene
- `JustExited` — True on first frame after leaving scene
- `Transitioning` — True during timed transition
- `TransProgress` — 0.0 to 1.0 during transition, and `1.0` on the update tick that completes the switch

`Add(name)` ignores duplicate scene names. `SwitchTransition(name, durationMs)` is a no-op when `name` is already the current scene or is already the pending transition target. It does not retrigger `JustEntered` or `JustExited` in those cases.

## Example
```zia
var scenes = SceneManager.New()
scenes.Add("menu")
scenes.Add("playing")
scenes.Switch("menu")

// Game loop:
scenes.Update(dt)
if scenes.IsScene("menu") { drawMenu(canvas) }
if scenes.IsScene("playing") { drawGame(canvas) }
scenes.SwitchTransition("playing", 500)
```
