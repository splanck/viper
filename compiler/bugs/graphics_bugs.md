# Graphics Show Demo - Bug Report

## Summary
While developing the graphics demo suite (`demos/viperlang/graphics_show/`), several bugs were discovered in the Viperlang frontend. This document catalogs each issue with reproduction steps.

---

## BUG-001: Runtime Classes Not Registered as Types (FIXED)

**Status:** Fixed in this session
**File:** `src/frontends/viperlang/Sema.cpp`
**Severity:** Critical

### Description
Runtime classes defined in `runtime.def` (e.g., `Viper.Graphics.Canvas`, `Viper.Graphics.Pixels`) were not registered in the Viperlang type system's `typeRegistry_`, causing "Unknown type" errors.

### Error Message
```
error[V3000]: Unknown type: Viper.Graphics.Canvas
```

### Fix Applied
Added runtime class types to `typeRegistry_` in `initRuntimeFunctions()`:
```cpp
typeRegistry_["Viper.Graphics.Canvas"] = types::ptr();
typeRegistry_["Viper.Graphics.Color"] = types::ptr();
typeRegistry_["Viper.Graphics.Pixels"] = types::ptr();
```

---

## BUG-002: Module Import System Not Implemented

**Status:** Open
**Severity:** Critical
**File:** `src/frontends/viperlang/Sema_Decl.cpp` lines 22-31

### Description
The module import system is essentially a stub. The `analyzeImport()` function only adds the import path to an internal set - it does NOT:
1. Load the imported module file
2. Parse the imported module
3. Analyze the imported module
4. Make the imported module's symbols accessible

### Root Cause
```cpp
void Sema::analyzeImport(ImportDecl &decl)
{
    if (decl.path.empty())
    {
        error(decl.loc, "Import path cannot be empty");
        return;
    }
    imports_.insert(decl.path);  // <-- Just stores path, does nothing else!
}
```

### Reproduction
```viper
// colors.viper
module colors;
var BLACK: Integer;
func initColors() { BLACK = 0; }

// main.viper
import "./colors";
func start() {
    colors.initColors();  // Error: Undefined identifier: colors
}
```

### Error Message
```
error[V3000]: Undefined identifier: colors
```

### Required Fix
Implement a proper module loader that:
1. Resolves the import path relative to the current file
2. Parses the imported file
3. Recursively analyzes the imported module
4. Registers the module's public symbols in a namespace accessible via the module name prefix
5. Handles circular imports gracefully

---

## BUG-003: Cannot Use `new` on Runtime Classes

**Status:** Open
**Severity:** Critical

### Description
The `new` keyword cannot be used with runtime classes like `Viper.Graphics.Canvas` or `Viper.Graphics.Pixels`, even though they have `.New` constructors defined in `runtime.def`.

### Reproduction
```viper
var canvas = new Viper.Graphics.Canvas("Title", 800, 600);
var pixels = new Viper.Graphics.Pixels(100, 100);
```

### Error Message
```
error[V3000]: 'new' can only be used with value, entity, or collection types
```

### Expected Behavior
`new` should work with any type that has a `.New` constructor registered in the runtime.

### Workaround
Call the `.New` constructor function directly instead of using the `new` keyword:
```viper
// Instead of: var canvas = new Viper.Graphics.Canvas("Title", 800, 600);
// Use:
var canvas = Viper.Graphics.Canvas.New("Title", 800, 600);
var pixels = Viper.Graphics.Pixels.New(100, 100);
```
This compiles and works correctly.

---

## BUG-004: Type Mismatch in Return Value Contexts

**Status:** Open
**Severity:** Medium

### Description
When a function returns `Number` but the return value involves operations that produce `Integer`, type mismatches occur.

### Reproduction
```viper
entity Star {
    expose Number z;

    expose func getScreenX(centerX: Integer, fov: Number) -> Integer {
        if z <= 0.0 { return -1; }
        // Error: Type mismatch: expected Integer, got Number
        return centerX + Viper.Math.Floor((x * fov) / z);
    }
}
```

### Error Messages
```
error[V3000]: Type mismatch: expected Integer, got Number
```

### Analysis
`Viper.Math.Floor()` returns `Number` (f64) but an `Integer` (i64) is expected. The issue is that `Floor` should return `Integer`, or there should be automatic coercion.

---

## BUG-005: Invalid Operands for Arithmetic with Mixed Entity Fields

**Status:** Open
**Severity:** Medium

### Description
Arithmetic operations between entity fields of different types or between entity fields and literals sometimes fail.

### Reproduction
```viper
var fade = 1.0 - (i * 0.4) / segments.count();
```

### Error Message
```
error[V3000]: Invalid operands for arithmetic operation
```

### Analysis
The issue appears to be related to type inference when mixing `Number` literals, `Integer` variables, and method call results in expressions.

---

## BUG-006: Floor Function Returns Number Instead of Integer

**Status:** Open
**Severity:** Medium
**File:** `src/frontends/viperlang/Sema.cpp` line ~1080

### Description
`Viper.Math.Floor()` is registered as returning `Number` but it should logically return `Integer` since its purpose is to convert a floating-point to its integer floor.

### Current Registration
```cpp
runtimeFunctions_["Viper.Math.Floor"] = types::number();
```

### Suggested Fix
```cpp
runtimeFunctions_["Viper.Math.Floor"] = types::integer();
```

### Related Functions
- `Viper.Math.Ceil` - should also return Integer
- `Viper.Math.Round` - should also return Integer
- `Viper.Math.Trunc` - should also return Integer

---

## BUG-007: Runtime Class Method Calls Not Resolved (FIXED)

**Status:** Fixed in this session
**File:** `src/frontends/viperlang/Sema_Expr.cpp`, `src/frontends/viperlang/Lowerer_Expr.cpp`, `src/frontends/viperlang/Sema.cpp`
**Severity:** Critical

### Description
Method calls on runtime class instances (e.g., `canvas.Poll()`, `canvas.Clear(color)`) were not being resolved correctly. The semantic analyzer wasn't recognizing these as runtime method calls, causing the lowerer to generate broken `call.indirect 0` instructions.

### Root Cause
Three issues:
1. `Viper.Graphics.Canvas.New` returned generic `types::ptr()` instead of a named type that carries the class name
2. The semantic analyzer didn't check for runtime class methods when analyzing FieldExpr call expressions
3. The lowerer didn't prepend the object pointer as the first argument for runtime class method calls

### Fix Applied
1. Added `types::runtimeClass(name)` to create pointer types that carry the class name
2. Added runtime class method resolution in `analyzeCall()` in Sema_Expr.cpp
3. Added object pointer prepending in `lowerCall()` in Lowerer_Expr.cpp
4. Registered all Canvas and Pixels methods in Sema.cpp

---

## Recommendations

### Priority 1 (Blocking)
1. Fix BUG-002 (Module imports) - Required for multi-file projects

### Priority 2 (Important)
2. Fix BUG-006 (Floor return type) - Causes cascading type errors
3. Fix BUG-004 and BUG-005 - Type coercion improvements

### Alternative Approach
If imports cannot be fixed quickly, consider:
1. Creating a "prelude" system that auto-imports common utilities
2. Supporting inline function definitions in entity bodies
3. Adding explicit type conversion functions (e.g., `toInteger()`, `toNumber()`)

---

## Test Files
The following demo files can be used to reproduce these bugs:
- `demos/viperlang/graphics_show/main.viper`
- `demos/viperlang/graphics_show/colors.viper`
- `demos/viperlang/graphics_show/starfield.viper`
- `demos/viperlang/graphics_show/plasma.viper`

## Working Demo
The following standalone demo works with all fixes applied:
- `demos/viperlang/graphics_show/simple_starfield.viper`

To run:
```bash
./build/src/tools/viper/viper demos/viperlang/graphics_show/simple_starfield.viper
```

Controls:
- UP/DOWN: Adjust speed
- SPACE: Toggle warp mode
- ESC/Q: Exit

---

## Environment
- Compiler: Viper toolchain built from `master` branch
- Platform: macOS (Darwin)
- Date: 2025-12-31
