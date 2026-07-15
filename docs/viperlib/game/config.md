---
status: active
audience: public
last-verified: 2026-07-15
---

# Viper.Game.Config

`Config` wraps a parsed JSON value and resolves dotted/JSONPath-style paths. The current registry
exposes it as a function namespace; construct it with the fully qualified name before using
instance-style calls.

## Construction

- `Viper.Game.Config.Load(path)` returns null for a null or missing path, an empty file, invalid
  JSON, or allocation failure. Other open/read/stat failures can still trap.
- `Viper.Game.Config.FromString(json)` returns null for null or invalid JSON.

## Queries

| Member | Return | Current behavior |
|---|---|---|
| `GetInt(path, default)` | `Integer` | Uses `default` only when the path is absent; numbers, booleans, and numeric strings are coerced. A present conversion failure returns `0` (VDOC-245). |
| `GetStr(path, default)` | Registry type `Any` | Converts strings, numbers, and booleans to text. Missing paths return the supplied value; unsupported present values return an empty string. The erroneous `Any` signature prevents normal typed String use in Zia (VDOC-237). |
| `GetBool(path, default)` | `Boolean` | Uses integer coercion and returns whether it is nonzero. A present conversion failure returns false rather than `default`. |
| `Has(path)` | `Boolean` | True when the path resolves to a non-null runtime value. |

Successful queries currently leak a retained JSON value (VDOC-236); avoid treating repeated lookup
as allocation-free until that runtime defect is repaired.

## Example

```rust
module ConfigExample;

func start() {
    var config = Viper.Game.Config.FromString(
        "{\"physics\":{\"gravity\":78},\"debug\":true}"
    );
    var gravity = config.GetInt("physics.gravity", 100);
    var debug = config.GetBool("debug", false);
    Viper.Terminal.SayInt(gravity);
    Viper.Terminal.SayBool(debug);
}
```
