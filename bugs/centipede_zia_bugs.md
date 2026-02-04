# Centipede Zia Demo - Bug Documentation

This document tracks bugs encountered while developing the Centipede clone in Zia.

---

## Bug List

### BUG-CZ-001: New Viper.* Runtime Classes Not Linked in Native Compilation

**Severity:** High
**Status:** RESOLVED
**Affects:** Native ARM64 compilation

**Description:** The new game development runtime classes (Viper.Grid2D, Viper.Timer, Viper.SmoothValue) are not linked when compiling to native ARM64. The linker fails with undefined symbols:
- `_Viper.Grid2D.New`, `_Viper.Grid2D.Get`, `_Viper.Grid2D.Set`, `_Viper.Grid2D.InBounds`
- `_Viper.Timer.New`, `_Viper.Timer.Start`, `_Viper.Timer.StartRepeating`, `_Viper.Timer.Update`, `_Viper.Timer.get_IsExpired`
- `_Viper.SmoothValue.New`, `_Viper.SmoothValue.SetImmediate`, `_Viper.SmoothValue.Update`, `_Viper.SmoothValue.get_ValueI64`

**Steps to Reproduce:**
```bash
./build/src/tools/zia/zia demos/zia/centipede/main.zia -o /tmp/centipede.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/centipede.il -run-native
```

**Resolution:** Fixed by:
1. Adding runtime name mapping in `InstrLowering.cpp:lowerCallWithArgs()` to map canonical names (e.g., "Viper.Grid2D.New") to C symbols (e.g., "rt_grid2d_new")
2. Adding `MOpcode::Bl` emission case in `AsmEmitter.cpp` with `mangleCallTarget()`
3. Adding Collections→Arrays dependency in both ARM64 and x86_64 linker code
4. Creating unified `RuntimeComponents.hpp` for shared symbol classification

**Previous Workaround (no longer needed):** Run in VM mode only

### BUG-CZ-002: canvas.KeyHeld() Returns Integer Instead of Boolean

**Severity:** Low
**Status:** RESOLVED
**Affects:** All canvas-based input

**Description:** The `canvas.KeyHeld()` method returns an Integer (0 or 1) instead of Boolean. This causes type errors when used directly in boolean expressions.

**Resolution:** Fixed by changing the return type in `runtime.def` from `i64` to `i1` (boolean). Also fixed `CanvasShouldClose` similarly.

**Previous Workaround (no longer needed):** Compare result to 0

### BUG-CZ-003: Entity init() Cannot Have Parameters

**Severity:** Low
**Status:** NOT A BUG
**Affects:** Entity initialization

**Description:** This was reported as a bug where `new Entity()` couldn't pass arguments to init(). However, this feature works correctly - `new Entity(arg1, arg2, ...)` DOES pass arguments to init().

**Resolution:** Not a bug. Use `new Entity(args...)` syntax to pass constructor arguments:
```zia
entity Segment {
    hide Integer x;
    hide Integer y;

    expose func init(px: Integer, py: Integer) {
        x = px;
        y = py;
    }
}

// Usage - arguments ARE passed to init():
var seg = new Segment(10, 20);
```

The original error likely occurred when using `new Segment()` (0 args) when init() expected parameters.

### BUG-CZ-004: Cannot Return Null for Entity Types

**Severity:** Medium
**Status:** RESOLVED
**Affects:** Entity method return values

**Description:** In Zia, you cannot return `null` from a function that returns an entity type. This prevents patterns like "find first inactive item in pool" from working directly.

**Error Message:**
```
error[V3000]: Type mismatch: expected Particle, got ??
```

**Resolution:** Fixed by updating `isAssignableFrom()` in `Types.cpp` to allow `null` (which has type `Optional[Unknown]`) to be assigned to Entity and Ptr types, since these are reference types that can be null.

**Previous Workaround (no longer needed):** Inline the search and operation instead of returning null

### BUG-CZ-005: canvas.Text() Argument Order Differs from Intuition

**Severity:** Low
**Status:** Documented
**Affects:** Text rendering

**Description:** The `canvas.Text()` method signature is `(x, y, text, color)`, which differs from common patterns where text comes first. Easy to make mistakes when calling.

**Correct Usage:**
```zia
canvas.Text(100, 200, "Hello", 0x00FFFFFF);  // Correct
// NOT: canvas.Text("Hello", 100, 200, 0x00FFFFFF);  // Wrong!
```

### BUG-CZ-006: 'as' Cast Expression Not Lowered (Crash in Native Compilation)

**Severity:** Critical
**Status:** RESOLVED
**Affects:** Native ARM64/x86_64 compilation with type casts

**Description:** The `as` type cast expression was not being lowered to IL, causing the generated code to use a null pointer (0) instead of the actual value. This resulted in segfaults when calling methods on entities retrieved from lists and cast to a specific type.

**Steps to Reproduce:**
```zia
entity Dog {
    hide Integer age;
    expose func init(a: Integer) { age = a; }
    expose func getAge() -> Integer { return age; }
}

func main() {
    var animals = Viper.Collections.List.New();
    animals.Add(new Dog(5));

    var item = animals[0];
    var dog = item as Dog;  // BUG: 'as' was lowered to 0!
    var age = dog.getAge(); // CRASH: calling method on null pointer
}
```

**Root Cause:** The `lowerExpr()` function in `Lowerer_Expr.cpp` had no case for `ExprKind::As`. The switch statement fell through to the default case which returned `{Value::constInt(0), Type(Type::Kind::I64)}`.

**Resolution:** Added `lowerAs()` function to handle the `as` expression:
1. Added `case ExprKind::As:` to the switch in `lowerExpr()`
2. Implemented `lowerAs()` to lower the source expression and return it with the mapped target type
3. Added unit tests for `as` casts with list elements

**Files Modified:**
- `src/frontends/zia/Lowerer.hpp` - Added `lowerAs()` declaration
- `src/frontends/zia/Lowerer_Expr.cpp` - Added case and implementation
- `src/tests/zia/test_zia_entities.cpp` - Added regression tests

---

## Workarounds Applied

1. ~~**Input helpers**: All keyboard input uses `canvas.KeyHeld(key) != 0` to convert Integer to Boolean~~ (BUG-CZ-002 RESOLVED - KeyHeld now returns Boolean)
2. ~~**Entity initialization**: All entities use parameterless `init()` plus separate `setup()` methods~~ (BUG-CZ-003 NOT A BUG - `new Entity(args)` works correctly)
3. **SmoothValue**: Use `displayScore.Target = score * 1.0;` to convert Integer to Float for SetTarget
4. ~~**Native compilation**: Currently only VM execution is supported~~ (BUG-CZ-001 RESOLVED)

---

## Testing Notes

- **VM Testing** (works):
  ```bash
  ./build/src/tools/zia/zia demos/zia/centipede/main.zia
  ```

- **Native ARM64** (works):
  ```bash
  ./build/src/tools/zia/zia demos/zia/centipede/main.zia -o /tmp/centipede.il
  ./build/src/tools/viper/viper codegen arm64 /tmp/centipede.il -o /tmp/centipede_native
  /tmp/centipede_native
  ```

---

## Code Statistics

- **Total Zia lines:** 2,593
- **Generated IL lines:** 8,067
- **Files:** 12 (config, utils, mushroom, player, centipede, spider, flea, scorpion, particle, popup, game, main)

## Features Implemented

1. **Core Gameplay**
   - Player movement in bottom zone with collision detection
   - Player bullet firing with cooldown
   - Centipede with segmented body and splitting behavior
   - Mushroom field with damage levels (4 health states)

2. **Enemies**
   - **Centipede**: Main enemy, moves across and down, splits when hit
   - **Spider**: Erratic bouncing in player zone, destroys mushrooms
   - **Flea**: Drops from top, creates mushrooms, takes 2 hits
   - **Scorpion**: Moves horizontally, poisons mushrooms

3. **Visual Effects**
   - Particle system with explosions for all enemy deaths
   - Score popups showing points earned
   - Smooth score counter animation
   - Animated menu with moving centipede

4. **Game States**
   - Menu screen with animated centipede
   - Playing state with full gameplay
   - Pause state
   - Game over state with restart option
   - Level complete state

5. **Scoring System**
   - Distance-based spider scoring (close/medium/far)
   - Bonus points for heads vs body segments
   - Extra life every 12,000 points
   - High score tracking

