# Diagnostics

> Debugging and assertion utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Diagnostics.Assert](#viperdiagnosticsassert)

---

## Viper.Diagnostics.Assert

Runtime assertion helper that terminates execution when a required condition is
not satisfied.

**Type:** Static function

### Signature

| Function                   | Signature               | Description                                                       |
|----------------------------|-------------------------|-------------------------------------------------------------------|
| `Viper.Diagnostics.Assert` | `Void(Boolean, String)` | Traps when @p condition is false; uses default message when empty |

### Notes

- When `condition` is zero/false, the runtime traps (prints to stderr and exits with status 1).
- An empty string or `""` message is replaced with `"Assertion failed"` to keep diagnostics informative.

### Example

```basic
DIM count AS INTEGER
count = 3

' Passes: execution continues
Viper.Diagnostics.Assert(count > 0, "count must be positive")

' Fails: terminates with the provided message
Viper.Diagnostics.Assert(count < 0, "boom")
```

---

## See Also

- [Time & Timing](time.md) - `Stopwatch` for performance measurement
- [Utilities](utilities.md) - `Log` for runtime diagnostics

