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
- `TransProgress` — 0.0 to 1.0 during transition

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
