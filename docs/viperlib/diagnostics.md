# Diagnostics

> Debugging and assertion utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Diagnostics](#viperdiagnostics)
- [Viper.Time.Stopwatch](time.md#vipertimestopwatch)

---

## Viper.Diagnostics

Runtime assertion helpers that terminate execution when a condition fails.

**Type:** Static utility class

### Methods

| Method        | Signature                          | Description                                                   |
|---------------|------------------------------------|---------------------------------------------------------------|
| `Assert`      | `Void(Boolean, String)`            | Trap when condition is false                                  |
| `AssertEq`    | `Void(Integer, Integer, String)`   | Trap when integers are not equal                              |
| `AssertNeq`   | `Void(Integer, Integer, String)`   | Trap when integers are equal                                  |
| `AssertEqNum` | `Void(Double, Double, String)`     | Trap when numbers are not equal (relative epsilon)            |
| `AssertEqStr` | `Void(String, String, String)`     | Trap when strings are not equal                               |
| `AssertNull`  | `Void(Object, String)`             | Trap when object is not null                                  |
| `AssertNotNull` | `Void(Object, String)`           | Trap when object is null                                      |
| `AssertFail`  | `Void(String)`                     | Unconditionally trap with message                             |
| `AssertGt`    | `Void(Integer, Integer, String)`   | Trap when `a` is not greater than `b`                         |
| `AssertLt`    | `Void(Integer, Integer, String)`   | Trap when `a` is not less than `b`                            |
| `AssertGte`   | `Void(Integer, Integer, String)`   | Trap when `a` is not greater than or equal to `b`             |
| `AssertLte`   | `Void(Integer, Integer, String)`   | Trap when `a` is not less than or equal to `b`                |
| `Trap`        | `Void(String)`                     | Unconditionally trap with message                             |

### Notes

- An empty message uses a default (e.g., "Assertion failed").
- Traps print a diagnostic to stderr and terminate the process with exit code 1.
- `AssertEqNum` uses a relative epsilon for floating-point comparison.

### Zia Example

```zia
module DiagDemo;

bind Viper.Terminal;

func start() {
    // Diagnostics uses fully-qualified calls
    Viper.Diagnostics.Assert(true, "this should pass");
    Viper.Diagnostics.AssertEq(42, 42, "integers match");
    Viper.Diagnostics.AssertEqStr("hello", "hello", "strings match");
    Say("All assertions passed!");
}
```

### BASIC Example

```basic
DIM value AS INTEGER
value = 5

Viper.Diagnostics.Assert(value > 0, "value must be positive")
Viper.Diagnostics.AssertEq(value, 5, "value mismatch")

' Force a trap
' Viper.Diagnostics.AssertFail("not implemented")
```

---

## See Also

- [Time & Timing](time.md) - `Stopwatch` for performance measurement
- [Utilities](utilities.md) - `Log` for runtime diagnostics
