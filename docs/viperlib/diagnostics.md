---
status: active
audience: public
last-verified: 2026-04-09
---

# Diagnostics

> Debugging and assertion utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Diagnostics](#viperdiagnostics)
- [Viper.Diagnostics.TrapInfo](#viperdiagnosticstrapinfo)
- [Viper.Core.Diagnostics](#vipercorediagnostics)
- [Viper.Debug.Protocol](#viperdebugprotocol)
- [Viper.Time.Stopwatch](time.md#vipertimestopwatch)

---

## Viper.Diagnostics

Read-only runtime diagnostic helpers.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `CurrentTrap()` | `Option<TrapInfo>()` | Return a snapshot of the current thread's latest trap metadata, or `None` when no trap was recorded |

`CurrentTrap()` is the modern, read-only diagnostics surface for trap metadata.
It preserves the existing low-level `Viper.Error.*` compatibility hooks while
giving applications and tools an explicit `Option` result instead of requiring
them to poll individual mutable trap fields. New code should not mutate trap
state directly; compiler/runtime interop hooks live under
`Viper.Runtime.Unsafe`.

---

## Viper.Diagnostics.TrapInfo

Immutable snapshot of trap metadata captured by `Viper.Diagnostics.CurrentTrap()`.

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

```rust
module TrapInfoDemo;

bind Viper.Diagnostics as Diagnostics;
bind Viper.Option as Option;
bind Viper.Terminal;

func start() {
    var trap = Diagnostics.CurrentTrap();
    if trap.IsNone {
        Say("No trap has been recorded on this thread.");
    }
}
```

---

## Viper.Core.Diagnostics

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
- Traps print a diagnostic to stderr and terminate the process with exit code 1.
- `AssertEqNum` uses a relative epsilon for floating-point comparison.

### Zia Example

```rust
module DiagDemo;

bind Viper.Terminal;

func start() {
    // Diagnostics uses fully-qualified calls
    Viper.Core.Diagnostics.Assert(true, "this should pass");
    Viper.Core.Diagnostics.AssertEq(42, 42, "integers match");
    Viper.Core.Diagnostics.AssertEqStr("hello", "hello", "strings match");
    Say("All assertions passed!");
}
```

### BASIC Example

```basic
DIM value AS INTEGER
value = 5

Viper.Core.Diagnostics.Assert(value > 0, "value must be positive")
Viper.Core.Diagnostics.AssertEq(value, 5, "value mismatch")

' Force a trap
' Viper.Core.Diagnostics.AssertFail("not implemented")
```

---

## Viper.Debug.Protocol

Headless debug-session model for IDE debugger integration. The current runtime surface provides a deterministic protocol boundary for launch, breakpoints, stepping, locals, stack frames, events, termination, and crash reporting. It is intentionally separate from the host IDE process so a target crash is represented as an event instead of crashing the IDE.

**Type:** Static utility class plus `Viper.Debug.Protocol.Session` handles

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Session()` | Create a debug protocol session |
| `SetBreakpoint(session, path, line)` | `Void(Object, String, Integer)` | Add a source breakpoint |
| `ClearBreakpoints(session, path)` | `Void(Object, String)` | Remove breakpoints for one source path |
| `Launch(session, path, source)` | `Map(Object, String, String)` | Start a headless source session and return the first event |
| `Continue(session)` | `Map(Object)` | Run until the next breakpoint, exit, or crash |
| `StepOver(session)` | `Map(Object)` | Advance one source line |
| `Pause(session)` | `Map(Object)` | Emit a pause event at the current line |
| `Terminate(session)` | `Map(Object)` | End the session |
| `StackFrames(session)` | `Seq(Object)` | Return current stack-frame maps |
| `Locals(session)` | `Seq(Object)` | Return current local-variable maps |
| `Events(session)` | `Seq(Object)` | Return session event history |
| `IsRunning(session)` | `Boolean(Object)` | True while the target is running |

Event maps include `type`, `reason`, `path`, and `line` where relevant. Locals currently report parsed `var`/`let` assignments from the deterministic headless model; future VM-hosted adapters can keep the same command/event shape.

### Zia Example

```rust
module DebugProtocolDemo;

bind Viper.Terminal;

func start() {
    var session = Viper.Debug.Protocol.New();
    Viper.Debug.Protocol.SetBreakpoint(session, "main.zia", 2);
    var event = Viper.Debug.Protocol.Launch(
        session, "main.zia", "var a = 1;\nvar b = 2;\n");
    Say(event.GetStr("type"));
    Say(event.GetStr("reason"));
}
```

---

## See Also

- [Time & Timing](time.md) - `Stopwatch` for performance measurement
- [Utilities](utilities.md) - `Log` for runtime diagnostics
