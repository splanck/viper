# Diagnostics

> Debugging and assertion utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Diagnostics.Assert](#viperdiagnosticsassert)

---

## Viper.Diagnostics.Assert

Runtime assertion helpers that terminate execution when conditions are not satisfied.
All assertions trap (print diagnostic to stderr and exit with status 1) on failure.

**Type:** Static utility functions

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Assert(condition, message)` | `Void(Boolean, String)` | Traps when condition is false |
| `AssertEq(expected, actual, message)` | `Void(Integer, Integer, String)` | Traps when integers are not equal |
| `AssertNeq(a, b, message)` | `Void(Integer, Integer, String)` | Traps when integers are equal |
| `AssertEqNum(expected, actual, message)` | `Void(Double, Double, String)` | Traps when numbers differ beyond epsilon |
| `AssertEqStr(expected, actual, message)` | `Void(String, String, String)` | Traps when strings are not equal |
| `AssertNull(obj, message)` | `Void(Object, String)` | Traps when object is not null |
| `AssertNotNull(obj, message)` | `Void(Object, String)` | Traps when object is null |
| `AssertFail(message)` | `Void(String)` | Always traps with the given message |
| `AssertGt(a, b, message)` | `Void(Integer, Integer, String)` | Traps when a is not greater than b |
| `AssertLt(a, b, message)` | `Void(Integer, Integer, String)` | Traps when a is not less than b |
| `AssertGte(a, b, message)` | `Void(Integer, Integer, String)` | Traps when a is not greater than or equal to b |
| `AssertLte(a, b, message)` | `Void(Integer, Integer, String)` | Traps when a is not less than or equal to b |

### Notes

- All assertions print descriptive error messages including expected vs actual values
- `AssertEqNum` uses a small epsilon for floating-point comparison (relative for large values, absolute for small)
- `AssertEqNum` treats NaN == NaN as true (unlike standard IEEE floating-point)
- Empty messages are replaced with a default description

### Example

```basic
DIM count AS INTEGER = 3
DIM name AS STRING = "Alice"
DIM total AS DOUBLE = 99.5

' Basic assertion
Viper.Diagnostics.Assert(count > 0, "count must be positive")

' Equality assertions
Viper.Diagnostics.AssertEq(count, 3, "count should be 3")
Viper.Diagnostics.AssertNeq(count, 0, "count should not be zero")
Viper.Diagnostics.AssertEqStr(name, "Alice", "name mismatch")
Viper.Diagnostics.AssertEqNum(total, 99.5, "total should be 99.5")

' Comparison assertions
Viper.Diagnostics.AssertGt(count, 0, "count > 0")
Viper.Diagnostics.AssertLt(count, 100, "count < 100")
Viper.Diagnostics.AssertGte(count, 1, "count >= 1")
Viper.Diagnostics.AssertLte(count, 10, "count <= 10")

' Null checks
DIM obj AS OBJECT = Viper.Collections.Seq.New()
Viper.Diagnostics.AssertNotNull(obj, "obj should be allocated")

' Unconditional failure (useful in unreachable code paths)
IF count < 0 THEN
    Viper.Diagnostics.AssertFail("unexpected negative count")
END IF
```

### Error Messages

When assertions fail, they print detailed diagnostics:

```
AssertEq failed: count should be 3: expected 3, got 5
AssertGt failed: value check: expected 10 > 20
AssertEqStr failed: name mismatch: expected "Alice", got "Bob"
```

---

## Viper.Diagnostics.Stopwatch

