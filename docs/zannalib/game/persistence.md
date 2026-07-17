---
status: active
audience: public
last-verified: 2026-07-15
---

# SaveData — Game Persistence
> Cross-platform key-value save/load system for game data.

**Part of [Zanna Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Overview

`SaveData` provides simple key-value persistence for game data — high scores, settings, progress, and player preferences. Data is stored as JSON in platform-appropriate directories, with no configuration needed.

**Type:** Instance (obj)
**Constructor:** `SaveData.New(gameName)`

---

## Platform Paths

| Platform | Save Location |
|----------|---------------|
| macOS    | `~/Library/Application Support/Zanna/<game>/save.json` |
| Linux    | `~/.local/share/zanna/<game>/save.json` |
| Windows  | `%APPDATA%\Zanna\<game>\save.json` |

The directory is created automatically on first `Save()`.
`Path` is always reported as an absolute path, including when platform environment variables contain relative paths.
`gameName` must be 1-64 bytes and contain only ASCII letters, digits, `_`, or `-`. Embedded NUL bytes, path separators, traversal syntax, and other characters trap before a save path is built.
Save keys must be non-empty valid UTF-8 strings without embedded NUL bytes. `SetInt` and `SetString` trap on invalid keys or invalid handles instead of silently ignoring writes. `SetString` also requires the stored value to be valid UTF-8, while JSON escaping preserves valid control characters such as embedded `NUL`. `Load()` applies the same decoded key and string-value validation to data read from disk.

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
| `GetInt(key, default)`        | `Integer(String, Integer)`       | Get an integer value, or the default if the key is missing or stores a string |
| `GetString(key, default)`     | `String(String, String)`         | Get a string value, or the default if the key is missing or stores an integer |
| `HasKey(key)`                 | `Boolean(String)`                | Check if a key exists                                    |
| `Remove(key)`                 | `Boolean(String)`                | Remove a key. Returns true if it existed                 |
| `Clear()`                     | `Void()`                         | Remove all key-value pairs                               |
| `Save()`                      | `Boolean()`                      | Write all data to disk. Returns true on success          |
| `Load()`                      | `Boolean()`                      | Read data from disk, replacing in-memory state. Returns true on success |

---

## Example

```rust
module SaveDemo;

bind Zanna.IO;
bind Zanna.Terminal;
bind Zanna.Text.Fmt as Fmt;

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

`Save()` creates the parent directory, writes the complete JSON payload to an exclusively created
temporary file in the destination directory, flushes that file, and then replaces the live save
file. The file and parent directory are synced where the platform supports it. There is no
incremental or append mode; each save rewrites the full state.

Write, flush, replacement, and allocation failures return `false` — and so now do parent-directory
creation failures (permission, a non-directory path component) and failure to obtain secure
randomness for the temporary name: the directory trap is caught and converted to a Boolean failure,
and the entropy path returns `false` rather than trapping (VDOC-246). `Save()` is therefore a
consistent Boolean error boundary for ordinary operational failures; only invalid programming inputs
(a null/invalid handle) still trap.

### Missing Save Files

`Load()` treats a missing save file as a successful empty load. It clears any current in-memory entries and returns true. Malformed JSON or other read errors still return false and leave the current in-memory state unchanged.

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

String values are emitted with standard JSON escaping for control characters. `Load()` rejects malformed or trailing garbage and leaves the in-memory state unchanged on failure.
Numeric values loaded from JSON must be plain decimal integers within the signed 64-bit range. They are parsed from the original JSON token text rather than through floating point, so the full `Integer` range round-trips exactly. Fractional, exponent-form, or out-of-range numbers make `Load()` return false and leave the existing in-memory state unchanged.

---

## See Also

- [Game Loop Framework](gameloop.md) — GameBase for structuring your game
- [Files & Directories](../io/files.md) — Lower-level file I/O
