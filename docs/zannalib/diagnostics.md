---
status: active
audience: public
last-verified: 2026-07-14
---

# Diagnostics

> Debugging and assertion utilities.

**Part of the [Zanna Runtime Library](README.md)**

## Contents

- [Zanna.Diagnostics](#zannadiagnostics)
- [Zanna.Diagnostics.TrapInfo](#zannadiagnosticstrapinfo)
- [Zanna.Core.Diagnostics](#zannacorediagnostics)
- [Zanna.Time.Stopwatch](time.md#zannatimestopwatch)

---

## Zanna.Diagnostics

Read-only runtime diagnostic helpers.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `CurrentTrap()` | `Option<TrapInfo>()` | Return a snapshot of the current thread's latest trap metadata, or `None` when no trap was recorded |

`CurrentTrap()` snapshots the calling thread's most recently recorded trap. It
returns `None` until that thread has recorded one and does not clear the stored
metadata after a read. In a normal standalone process, the default trap handler
terminates before application code can inspect the snapshot; it is chiefly
useful from a language catch/recovery path or an embedding host with a recovery
hook.

This is the modern, read-only diagnostics surface. It preserves the existing
low-level `Zanna.Runtime.Unsafe` trap hooks while giving applications and
tools an explicit `Option` result instead of requiring them to poll individual
mutable trap fields. New code should not mutate trap state directly;
compiler/runtime interop hooks live under `Zanna.Runtime.Unsafe`.

---

## Zanna.Diagnostics.TrapInfo

Immutable snapshot of trap metadata captured by `Zanna.Diagnostics.CurrentTrap()`.

**Type:** Runtime object

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Kind` | `Integer` | Canonical trap-kind integer |
| `Code` | `Integer` | Runtime `Err_*` code, or `0` when none was recorded |
| `Ip` | `Integer` | Native instruction pointer, or `0` when unavailable |
| `Line` | `Integer` | Source line number, or `-1` when unavailable |
| `KindName` | `String` | Stable trap kind name such as `Overflow` or `RuntimeError` |
| `Message` | `String` | Trap message or default message for the trap kind |
| `Location` | `String` | Formatted location such as `line 12`, or empty when unavailable |

### Zia Example

```zia
module TrapInfoDemo;

bind Zanna.Diagnostics as Diagnostics;
bind Zanna.Option as Option;
bind Zanna.Terminal;

func start() {
    var trap = Diagnostics.CurrentTrap();
    if trap.IsNone {
        Say("No trap has been recorded on this thread.");
    }
}
```

---

## Zanna.Core.Diagnostics

Runtime assertion helpers that terminate execution when a condition fails.

**Type:** Static utility class

### Methods

| Method          | Signature                          | Description                                                   |
|-----------------|------------------------------------|---------------------------------------------------------------|
| `Assert`        | `Void(Boolean, String)`            | Trap when condition is false                                  |
| `AssertEq`      | `Void(Integer, Integer, String)`   | Trap when integers are not equal                              |
| `AssertEqNum`   | `Void(Double, Double, String)`     | Trap when numbers are not equal (relative epsilon)            |
| `AssertEqStr`   | `Void(String, String, String)`     | Trap when strings are not equal                               |
| `AssertFail`    | `Void(String)`                     | Unconditionally trap with message                             |
| `AssertGt`      | `Void(Integer, Integer, String)`   | Trap when `a` is not greater than `b`                         |
| `AssertGte`     | `Void(Integer, Integer, String)`   | Trap when `a` is not greater than or equal to `b`             |
| `AssertLt`      | `Void(Integer, Integer, String)`   | Trap when `a` is not less than `b`                            |
| `AssertLte`     | `Void(Integer, Integer, String)`   | Trap when `a` is not less than or equal to `b`                |
| `AssertNeq`     | `Void(Integer, Integer, String)`   | Trap when integers are equal                                  |
| `AssertNotNull` | `Void(Object, String)`             | Trap when object is null                                      |
| `AssertNull`    | `Void(Object, String)`             | Trap when object is not null                                  |
| `Trap`          | `Void(String)`                     | Unconditionally trap with message                             |

### Notes

- An empty message uses a default (e.g., "Assertion failed").
- With the default trap hook, an unrecovered trap prints its message to stderr
  and terminates immediately with exit code 1. Language recovery frames and
  embedder/test hooks may intercept traps instead.
- `AssertEqNum` accepts exact equality (including equal infinities) and paired
  NaNs. Otherwise it uses an absolute tolerance below magnitude `1.0` and a
  relative tolerance at or above it; both tolerances are `1e-9` and comparisons
  are strict (`<`, not `<=`).

### Zia Example

```zia
module DiagDemo;

bind Zanna.Terminal;

func start() {
    // Diagnostics uses fully-qualified calls
    Zanna.Core.Diagnostics.Assert(true, "this should pass");
    Zanna.Core.Diagnostics.AssertEq(42, 42, "integers match");
    Zanna.Core.Diagnostics.AssertEqStr("hello", "hello", "strings match");
    Say("All assertions passed!");
}
```

### BASIC Example

```basic
DIM value AS INTEGER
value = 5

Zanna.Core.Diagnostics.Assert(value > 0, "value must be positive")
Zanna.Core.Diagnostics.AssertEq(value, 5, "value mismatch")

' Force a trap
' Zanna.Core.Diagnostics.AssertFail("not implemented")
```

## See Also

- [Time & Timing](time.md) - `Stopwatch` for performance measurement
- [Utilities](utilities.md) - `Log` for runtime diagnostics
- [Debugging Guide](../tools/debugging.md) - debugger adapter protocol and source-level debugging
