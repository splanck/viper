# Viper.Game.Config

Typed config loader wrapping JSON with dotted path access and default values.

## API

### Config.Load(path) -> Config?
Load and parse a JSON config file. Returns `null` when the path is missing, unreadable, or empty, so callers can keep defaults without a JSON parser trap.

### Config.FromString(jsonStr) -> Config?
Parse an inline JSON string.

### GetInt(path, default) -> Integer
### GetStr(path, default) -> String
### GetBool(path, default) -> Boolean
### Has(path) -> Boolean

## Example
```zia
var cfg = Config.Load("game.json")
var gravity = cfg.GetInt("physics.gravity", 78)
var title = cfg.GetStr("title", "My Game")
var debug = cfg.GetBool("debug", false)
```

## JSON File
```json
{
    "physics": { "gravity": 78, "maxFall": 1350 },
    "title": "XENOSCAPE",
    "debug": false
}
```
