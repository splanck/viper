# Pac-Man Zia Clone - Bug Documentation

This document tracks all bugs encountered during the development of the Pac-Man clone in Zia.

## Bug Index

| ID | Description | Status | Workaround |
|----|-------------|--------|------------|
| PAC-001 | Canvas.Show() method doesn't exist | Fixed | Use Canvas.Flip() instead |
| PAC-002 | Canvas.Close() method causes null pointer error | Fixed | Don't call Close(), program exits automatically |
| PAC-003 | Value types not properly initialized | Fixed | Added emitValueTypeAlloc() for proper stack allocation |
| PAC-004 | Field syntax differs from parameter syntax | Documentation | Fields: `Type name;`, Params: `name: Type` |
| PAC-005 | List/String methods are lowercase | Documentation | Use `.get()`, `.set()`, `.length()`, `.add()` |
| PAC-006 | Module system requires all transitive deps at entry | Fixed | Transitive bindings now propagated automatically |
| PAC-007 | Keyboard events leak to terminal causing beeps | Fixed | Don't forward key events to NSApp |

---

## Detailed Bug Reports

### PAC-001: Canvas.Show() Method Missing

**Status:** Fixed
**Severity:** Critical
**Discovered:** During initial testing

**Description:**
When attempting to display the rendered frame using `canvas.Show()`, the runtime threw "Null indirect callee" error.

**Error Message:**
```
Null indirect callee
```

**Expected Behavior:**
`canvas.Show()` should present the rendered frame to the display.

**Actual Behavior:**
The method doesn't exist; the runtime crashes with a null pointer error.

**Workaround:**
Use `canvas.Flip()` instead. This is the correct method name for presenting the frame buffer.

**Code Change:**
```rust
// Before (broken)
canvas.Show();

// After (working)
canvas.Flip();
```

---

### PAC-002: Canvas.Close() Method Causes Crash

**Status:** Fixed
**Severity:** Medium
**Discovered:** During initial testing

**Description:**
Calling `canvas.Close()` to close the graphics window causes a null pointer error.

**Workaround:**
Don't call Close(). The window closes automatically when the program terminates.

**Code Change:**
```rust
// Before (broken)
canvas.Close();

// After (working)
// Simply don't call Close() - window closes on program exit
```

---

### PAC-003: Value Types Not Properly Initialized

**Status:** Fixed
**Severity:** Critical
**Discovered:** During Vec2 implementation

**Description:**
Value types (declared with `value` keyword) weren't properly allocated in memory. When creating a value type variable without an initializer, the compiler only allocated a null pointer instead of actual stack space.

**Root Cause:**
In `Lowerer_Stmt.cpp`, the `lowerVarStmt()` function mapped value types to `Type::Kind::Ptr` but only initialized them with `Value::null()`, not actual allocated memory.

**Fix:**
1. Added `emitValueTypeAlloc()` function in `Lowerer_Emit.cpp` that:
   - Allocates proper stack space using `alloca` with the value type's actual size
   - Zero-initializes all fields

2. Modified `lowerVarStmt()` to detect value types and call `emitValueTypeAlloc()` instead of using `Value::null()`

**Files Changed:**
- `src/frontends/zia/Lowerer.hpp` - Added `emitValueTypeAlloc()` declaration
- `src/frontends/zia/Lowerer_Emit.cpp` - Added `emitValueTypeAlloc()` implementation
- `src/frontends/zia/Lowerer_Stmt.cpp` - Modified default initialization to handle value types

**Code Now Works:**
```rust
value Vec2 {
    Integer x;
    Integer y;
}

func test() {
    var v: Vec2;  // Now properly allocates stack space
    v.x = 10;     // Works correctly
    v.y = 20;
}
```

---

### PAC-004: Field Declaration Syntax vs Parameter Syntax

**Status:** Documentation
**Severity:** Informational

**Description:**
Zia uses different syntax for field declarations vs function parameters, which can be confusing.

**Correct Syntax:**

Entity/Value fields:
```rust
entity Example {
    Integer fieldName;           // Type first, then name
    hide Integer privateField;   // With visibility modifier
    expose String publicField;
}
```

Function parameters:
```rust
func example(paramName: Integer, anotherParam: String) -> Boolean {
    // ...
}
```

---

### PAC-005: Collection Methods Use Lowercase Names

**Status:** Documentation
**Severity:** Informational

**Description:**
List and other collection methods use lowercase method names, not PascalCase.

**Correct Methods:**
- `list.add(item)` - Add item to list
- `list.get(index)` - Get item at index
- `list.set(index, value)` - Set item at index
- `list.length()` - Get list length

**String Methods (via Viper.String):**
- `Viper.String.Length(str)` - Get string length
- `Viper.String.Substring(str, start, length)` - Get substring

**Incorrect (will fail):**
```rust
list.Add(item);    // Wrong - capital A
list.Get(index);   // Wrong - capital G
list.Length();     // Wrong - capital L
```

---

### PAC-006: Module System Requires All Transitive Dependencies at Entry Point

**Status:** Fixed
**Severity:** Medium
**Discovered:** During modular refactoring

**Description:**
The Zia module system wasn't propagating transitive bindings. If module A imports module B, and B imports module C, references to C inside B would fail because C's module symbol wasn't registered.

**Root Cause:**
In `ImportResolver.cpp`, the `processModule()` function collected declarations from nested modules but failed to propagate the `binds` vector. When semantic analysis ran, it only registered module symbols for explicitly listed binds, missing transitive ones.

**Fix:**
Modified `ImportResolver::processModule()` to propagate transitive binds:

```cpp
// After processing a bound module, copy its binds to the importing module
for (const auto &transitiveBind : boundModule->binds)
{
    // Avoid duplicate binds
    bool alreadyBound = false;
    for (const auto &existingBind : module.binds)
    {
        if (existingBind.path == transitiveBind.path)
        {
            alreadyBound = true;
            break;
        }
    }
    if (!alreadyBound)
    {
        module.binds.push_back(transitiveBind);
    }
}
```

**File Changed:** `src/frontends/zia/ImportResolver.cpp`

**Code Now Works:**
```rust
// main.zia - now works with minimal bindings
bind "./config";
bind "./game";  // game's transitive deps (utils, maze, etc.) are automatically available
```

---

### PAC-007: Keyboard Events Leak to Terminal Causing System Beeps

**Status:** Fixed
**Severity:** Medium
**Discovered:** During gameplay testing

**Description:**
When holding down arrow keys for movement, the system produces beeping sounds as if the terminal is receiving keyboard events that it cannot process.

**Root Cause:**
In `src/lib/graphics/src/vgfx_platform_macos.m`, the event processing code was calling `[NSApp sendEvent:event]` for ALL events including keyboard events. On macOS, when arrow keys are sent to NSApp without a proper first responder consuming them, the system beeps.

**Fix:**
Modified `vgfx_platform_process_events()` to NOT forward keyboard events to NSApp:

```objc
/* Don't forward keyboard events to NSApp - we handle them directly.
   This prevents the system beep when arrow keys are pressed. */
NSEventType eventType = [event type];
if (eventType != NSEventTypeKeyDown && eventType != NSEventTypeKeyUp)
{
    [NSApp sendEvent:event];
}
```

**File Changed:** `src/lib/graphics/src/vgfx_platform_macos.m`

---

## Notes

- The Viper Graphics API uses `Flip()` for double-buffered rendering (common in game development)
- Entity creation with arguments: `new Entity(args)` calls `init(args)` automatically
- Entity creation without arguments: `new Entity()` creates object, must call `.init()` separately if init has parameters
- Canvas.Poll() must be called each frame to process input events
- Canvas.KeyHeld(keycode) returns Integer (0 or non-zero), not Boolean
- Single-file architecture is currently more reliable than multi-module architecture

