---
status: active
audience: public
last-verified: 2026-07-15
---

# Viper.Game.SceneManager

`SceneManager` is a bounded name/state timer. It does not own scene objects, invoke lifecycle
callbacks, or draw a fade; application code observes its flags and progress and performs those
actions itself.

## API behavior

- `Viper.Game.SceneManager.New()` creates an empty manager. The first successful `Add(name)` makes
  that name current and raises `JustEntered`.
- `Add(name)` silently ignores a duplicate or a 65th entry. Names are truncated to 127 bytes before
  duplicate matching, so long common prefixes alias (VDOC-243).
- `Switch(name)` immediately changes to a known, different name and cancels a pending transition.
  Unknown/current names are no-ops.
- `SwitchTransition(name, durationMs)` starts a timer for a known, different target. Non-positive
  duration becomes 1 ms. Re-requesting the current/pending target is a no-op.
- `Update(dt)` first clears `JustEntered`/`JustExited` and the one-tick completion state. Positive
  deltas advance a transition; completion changes the name and raises both edge flags.
- `Transitioning` reports an active timer. `TransProgress` is 0 at start, advances toward 1, is 1
  only on the completing update tick, then returns to 0 on the next update.
- `Current` and `Previous` return empty runtime strings when unavailable, not null. Their registry
  types are currently `Any`, so normal typed String consumption in Zia fails (VDOC-237).

## Example

```rust
module SceneManagerExample;

func start() {
    var scenes = Viper.Game.SceneManager.New();
    scenes.Add("menu");
    scenes.Add("playing");
    scenes.SwitchTransition("playing", 500);
    scenes.Update(500);
    Viper.Terminal.SayBool(scenes.IsScene("playing"));
    Viper.Terminal.SayBool(scenes.get_JustEntered());
}
```
