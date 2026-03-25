---
status: active
audience: public
last-verified: 2026-03-25
---

# SaveData — Game Persistence
> Cross-platform key-value save/load system for game data.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Overview

`SaveData` provides simple key-value persistence for game data — high scores, settings, progress, and player preferences. Data is stored as JSON in platform-appropriate directories, with no configuration needed.

**Type:** Instance (obj)
**Constructor:** `SaveData.New(gameName)`

---

## Platform Paths

| Platform | Save Location |
|----------|---------------|
| macOS    | `~/Library/Application Support/Viper/<game>/save.json` |
| Linux    | `~/.local/share/viper/<game>/save.json` |
| Windows  | `%APPDATA%\Viper\<game>\save.json` |

The directory is created automatically on first `Save()`.

---

## Properties

| Property | Type    | Access | Description                         |
|----------|---------|--------|-------------------------------------|
| `Count`  | Integer | Read   | Number of stored key-value pairs    |
| `Path`   | String  | Read   | Full filesystem path to the save file |

---

## Methods

| Method                        | Signature                        | Description                                              |
|-------------------------------|----------------------------------|----------------------------------------------------------|
| `SetInt(key, value)`          | `Void(String, Integer)`          | Store an integer value under a key                       |
| `SetString(key, value)`       | `Void(String, String)`           | Store a string value under a key                         |
| `GetInt(key, default)`        | `Integer(String, Integer)`       | Get an integer value, or default if key missing          |
| `GetString(key, default)`     | `String(String, String)`         | Get a string value, or default if key missing            |
| `HasKey(key)`                 | `Boolean(String)`                | Check if a key exists                                    |
| `Remove(key)`                 | `Boolean(String)`                | Remove a key. Returns true if it existed                 |
| `Clear()`                     | `Void()`                         | Remove all key-value pairs                               |
| `Save()`                      | `Boolean()`                      | Write all data to disk. Returns true on success          |
| `Load()`                      | `Boolean()`                      | Read data from disk, replacing in-memory state. Returns true on success |

---

## Example

```zia
module SaveDemo;

bind Viper.IO;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var save = SaveData.New("my_platformer");

    // Load existing data (if any)
    save.Load();

    // Read with defaults
    var highScore = save.GetInt("high_score", 0);
    var playerName = save.GetString("player_name", "Player");
    Say("Welcome back, " + playerName + "! High score: " + Fmt.Int(highScore));

    // Update data
    save.SetInt("high_score", 42000);
    save.SetString("player_name", "ACE");
    save.SetInt("level_reached", 5);

    // Persist to disk
    if save.Save() {
        Say("Saved to: " + save.Path);
    }
}
```

---

## Design Notes

### Atomic Writes

`Save()` opens the file, writes the complete JSON, and closes it in one operation. There is no incremental/append mode — the entire state is written each time.

### Last-Write Wins

If the same key is set multiple times, only the last value is retained. Keys are unique — there are no duplicate entries.

### JSON Format

Data is stored as a flat JSON object. Integer values are stored as numbers; string values are stored as quoted strings:

```json
{
  "high_score": 42000,
  "player_name": "ACE",
  "level_reached": 5
}
```

---

## See Also

- [Game Loop Framework](gameloop.md) — GameBase for structuring your game
- [Files & Directories](../io/files.md) — Lower-level file I/O
